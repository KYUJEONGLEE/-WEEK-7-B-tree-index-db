#ifndef BPTREE_H
#define BPTREE_H

#include "utils.h"

#define BPTREE_ORDER 64
#define BPTREE_MIN_ORDER 3

typedef enum {
    BPTREE_INTERNAL,
    BPTREE_LEAF
} NodeType;

typedef struct BPTreeNode {
    NodeType type;
    int num_keys;
    long long keys[BPTREE_ORDER];
    struct BPTreeNode *children[BPTREE_ORDER + 1];
    void *values[BPTREE_ORDER];
    struct BPTreeNode *next;
    struct BPTreeNode *parent;
} BPTreeNode;

typedef struct {
    BPTreeNode *root;
    int order;
    int count;
} BPTree;

BPTree *bptree_create(int order);
BPTreeNode *bptree_create_node(NodeType type);
BPTreeNode *bptree_find_leaf(BPTree *tree, long long key);
void *bptree_search(BPTree *tree, long long key);
int bptree_insert(BPTree *tree, long long key, void *value);
void bptree_destroy(BPTree *tree);

#endif
