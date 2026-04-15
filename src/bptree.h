#ifndef BPTREE_H
#define BPTREE_H

#ifndef BPTREE_ORDER
#define BPTREE_ORDER 64
#endif

typedef enum {
    BPTREE_INTERNAL,
    BPTREE_LEAF
} NodeType;

typedef struct BPTreeNode {
    NodeType type;
    int num_keys;
    long long keys[BPTREE_ORDER - 1];
    struct BPTreeNode *children[BPTREE_ORDER];
    void *values[BPTREE_ORDER - 1];
    struct BPTreeNode *next;
    struct BPTreeNode *parent;
} BPTreeNode;

typedef struct {
    BPTreeNode *root;
    int order;
    int count;
} BPTree;

typedef struct {
    int height;
    int node_count;
    int leaf_count;
    int key_count;
} BPTreeStats;

BPTree *bptree_create(int order);
BPTreeNode *bptree_create_node(NodeType type);
BPTreeNode *bptree_find_leaf(BPTree *tree, long long key);
void *bptree_search(BPTree *tree, long long key);
int bptree_insert(BPTree *tree, long long key, void *value);
void bptree_insert_into_leaf(BPTreeNode *leaf, long long key, void *value);
void bptree_split_leaf(BPTree *tree, BPTreeNode *leaf);
void bptree_insert_into_parent(BPTree *tree, BPTreeNode *left,
                               long long key, BPTreeNode *right);
void bptree_split_internal(BPTree *tree, BPTreeNode *node);
void bptree_destroy(BPTree *tree);
void bptree_collect_stats(BPTree *tree, BPTreeStats *stats);

#endif
