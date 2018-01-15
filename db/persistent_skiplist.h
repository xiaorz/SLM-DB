#ifndef STORAGE_LEVELDB_DB_PERSISTENT_SKIPLIST_H_
#define STORAGE_LEVELDB_DB_PERSISTENT_SKIPLIST_H_

#include "leveldb/comparator.h"
#include "port/port_posix.h"
#include "leveldb/iterator.h"
#include "nvm_btree.h"

namespace leveldb {
class PersistentSkiplist {
 public:
  struct Node;

  explicit PersistentSkiplist(const Comparator* cmp);
  PersistentSkiplist(const Comparator* cmp, Node* first, Node* last);
  ~PersistentSkiplist();
  size_t ApproximateMemoryUsage();
  Node* Insert(const Slice& key, const Slice& value);
  Node* Find(const Slice& key);

  void Erase(Node* first, Node* last); // erase nodes in range [first, last]

  Node* Head() { return head; }
  Node* Tail() { return tail; }

 private:
  static const size_t max_level = 32;

  size_t current_level;
  size_t current_size;

  const Comparator* comparator;

  Node* head;
  Node* tail;;

  bool Equal(const Slice& a, const Slice& b) const { return (comparator->Compare(a, b) == 0); }
  size_t RandomLevel();
  Node* FindGreaterOrEqual(Slice key);
  Node* MakeNode(Slice key, Slice value, size_t level);
};

struct PersistentSkiplist::Node {
  std::string key;
  std::string value;
  const size_t level;
  std::vector<Node*> next;
  std::vector<Node*> prev;

  Node(const Slice& k, const Slice& v, size_t level) : level(level) {
    key.assign(k.data(), k.size());
    value.assign(v.data(), v.size());
    for (auto i = 0; i < level; i++) {
      next.push_back(NULL);
      prev.push_back(NULL);
    }
  }

  size_t GetSize() { return key.size() + value.size(); }
};

PersistentSkiplist::PersistentSkiplist(const Comparator* cmp)
    : comparator(cmp),
      head(MakeNode(Slice(), Slice(), max_level)),
      tail(MakeNode(Slice(), Slice(), max_level)),
      current_level(0) {
  for (auto i = 0; i < max_level; i++) {
    head->next[i] = tail;
    tail->prev[i] = head;
  }
  clflush((char*)head->next[0], sizeof(Node));
  srand(std::time(NULL));
  current_size = 0;
}

PersistentSkiplist::PersistentSkiplist(const Comparator* cmp, Node* first, Node* last)
    : PersistentSkiplist(cmp) {
  Node* left = first;
  Node* right = last;
  current_level = 0;
  while (cmp->Compare(left->key, last->key) < 0
      && cmp->Compare(first->key, right->key) < 0) {
    // linking first node to head
    head->next[current_level] = left;
    left->prev[current_level] = head;
    // linking last node to tail
    tail->prev[current_level] = right;
    right->next[current_level] = tail;
    if (current_level == 0) {
      clflush((char*)head->next[0], sizeof(void*));
    }
    current_level++;
    while (left->level <= current_level) left = left->next[current_level-1];
    while (right->level <= current_level) right = right->prev[current_level-1];
  }
}

PersistentSkiplist::~PersistentSkiplist() {
  auto node = head;
  while (node != NULL) {
    auto next = node->next[0];
    delete node;
    node = next;
  }
  current_size = 0;
}

size_t PersistentSkiplist::ApproximateMemoryUsage() {
  return current_size;
}

PersistentSkiplist::Node* PersistentSkiplist::Insert(const Slice& key, const Slice& value) {
  Node* node = FindGreaterOrEqual(key);
  auto level = RandomLevel();
  Node* next_node = node;
  Node* prev_node = node->prev[0];
  if (Equal(node->key, key))
    next_node = next_node->next[0];
  Node* new_node = new Node(key, value, level);
  if (level > current_level) current_level = level;
  for (auto i = 0; i < level; i++) {
    while (next_node->level <= i) next_node = next_node->next[i-1];
    while (prev_node->level <= i) prev_node = prev_node->prev[i-1];
    // making forward linking
    new_node->next[i] = next_node;
    next_node->prev[i] = new_node;
    // making backward linking
    new_node->prev[i] = prev_node;
    prev_node->next[i] = new_node;
    if (i == 0) {
      clflush((char*)new_node->next[0], sizeof(void*));
      clflush((char*)next_node->next[0], sizeof(void*));
    }
  }
  current_size += new_node->GetSize();
  return new_node;
}

PersistentSkiplist::Node* PersistentSkiplist::Find(const Slice &key) {
  Node* node = FindGreaterOrEqual(key);
  if (Equal(key, node->key)) {
    return node;
  } else {
    return NULL;
  }
}

void PersistentSkiplist::Erase(Node* first, Node* last) {
  Node* left = first->prev[0];
  Node* right = last->next[0];
  for (int level = 0; level < current_level; level++) {
    left->next[level] = right;
    right->prev[level] = left;
    if (level == 0) {
      clflush((char*)left->next[0], sizeof(void*));
    }
    while (left->level <= level+1) left = left->prev[level];
    while (right->level <= level+1) right = right->next[level];
  }
  while (head->next[current_level] == tail &&
      tail->prev[current_level] == head) {
    current_level--;
  }

}

PersistentSkiplist::Node* PersistentSkiplist::FindGreaterOrEqual(Slice key) {
  Node* node = head;
  for (auto i = current_level; i-- > 0;) {
    while (node->next[i] != tail && comparator->Compare(node->next[i]->key, key) < 0) {
      node = node->next[i];
    }
  }
  return node->next[0];
}

size_t PersistentSkiplist::RandomLevel() {
  static const int level_probability = RAND_MAX / 4;
  size_t result = 1;
  while (rand() < level_probability && result < max_level)
    ++result;
  return result;
}

PersistentSkiplist::Node* PersistentSkiplist::MakeNode(Slice key, Slice value, size_t level) {
  Node* node = new Node(key, value, level);
  clflush((char*)node->key.data(), key.size());
  clflush((char*)node->value.data(), value.size());
  return node;
}

}

#endif //STORAGE_LEVELDB_DB_PERSISTENT_SKIPLIST_H_
