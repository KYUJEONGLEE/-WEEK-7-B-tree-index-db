#include "bptree.h"

#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int bptree_max_keys(void) {
    return BPTREE_ORDER - 1;
}

static int bptree_cut(int length) {
    return (length + 1) / 2;
}

/*
 * 함수명: bptree_create
 * ----------------------------------------
 * 기능: 빈 B+ 트리 객체를 생성한다.
 *
 * 핵심 흐름:
 *   1. BPTree 구조체를 할당한다.
 *   2. root는 아직 없으므로 NULL로 둔다.
 *   3. order와 count를 초기화한다.
 *
 * 개념:
 *   - order는 한 internal node가 가질 수 있는 최대 자식 수다.
 *   - 한 node의 최대 key 수는 order - 1개다.
 *
 * 매개변수:
 *   - order: 요청 차수. 이 구현은 고정 배열을 쓰므로 BPTREE_ORDER로 고정한다.
 *
 * 반환값: 생성된 B+ 트리 포인터, 실패 시 NULL
 */
BPTree *bptree_create(int order) {
    BPTree *tree;

    tree = (BPTree *)calloc(1, sizeof(BPTree));
    if (tree == NULL) {
        fprintf(stderr, "Error: Failed to allocate B+ tree.\n");
        return NULL;
    }

    tree->root = NULL;
    tree->order = order > 0 ? order : BPTREE_ORDER;
    tree->count = 0;
    return tree;
}

/*
 * 함수명: bptree_create_node
 * ----------------------------------------
 * 기능: leaf 또는 internal B+ 트리 node 하나를 생성한다.
 *
 * 핵심 흐름:
 *   1. node 메모리를 0으로 초기화해 할당한다.
 *   2. node type과 key 개수를 설정한다.
 *   3. parent, children, values, next는 NULL 상태로 시작한다.
 *
 * 개념:
 *   - leaf node는 실제 value 포인터를 가진다.
 *   - internal node는 탐색 방향을 정하는 key와 child 포인터만 가진다.
 *
 * 반환값: 생성된 node 포인터, 실패 시 NULL
 */
BPTreeNode *bptree_create_node(NodeType type) {
    BPTreeNode *node;

    node = (BPTreeNode *)calloc(1, sizeof(BPTreeNode));
    if (node == NULL) {
        fprintf(stderr, "Error: Failed to allocate B+ tree node.\n");
        return NULL;
    }

    node->type = type;
    node->num_keys = 0;
    return node;
}

/*
 * 함수명: bptree_find_leaf
 * ----------------------------------------
 * 기능: key가 존재하거나 새로 들어갈 leaf node를 찾는다.
 *
 * 핵심 흐름:
 *   1. root에서 시작한다.
 *   2. internal node에서는 key와 separator key를 비교한다.
 *   3. key가 들어갈 child로 내려가 leaf에 도달하면 반환한다.
 *
 * 개념:
 *   - B+ 트리 검색은 항상 root에서 leaf까지 한 경로만 따라간다.
 *   - key가 separator 이상이면 오른쪽 child로 이동한다.
 *
 * 반환값: leaf node 포인터, 빈 트리면 NULL
 */
BPTreeNode *bptree_find_leaf(BPTree *tree, long long key) {
    BPTreeNode *node;
    int i;

    if (tree == NULL || tree->root == NULL) {
        return NULL;
    }

    node = tree->root;
    while (node->type == BPTREE_INTERNAL) {
        i = 0;
        while (i < node->num_keys && key >= node->keys[i]) {
            i++;
        }
        node = node->children[i];
    }

    return node;
}

/*
 * 함수명: bptree_search
 * ----------------------------------------
 * 기능: key에 해당하는 value 포인터를 검색한다.
 *
 * 핵심 흐름:
 *   1. bptree_find_leaf()로 대상 leaf를 찾는다.
 *   2. leaf 안의 keys[]를 선형 탐색한다.
 *   3. key가 있으면 values[i], 없으면 NULL을 반환한다.
 *
 * 개념:
 *   - WHERE id = ? 와 WHERE game_win_count = ? 모두 이 exact lookup을 쓴다.
 *   - 트리 높이만큼만 내려가므로 전체 행 스캔보다 훨씬 작게 탐색한다.
 *
 * 반환값: value 포인터, key가 없으면 NULL
 */
void *bptree_search(BPTree *tree, long long key) {
    BPTreeNode *leaf;
    int i;

    leaf = bptree_find_leaf(tree, key);
    if (leaf == NULL) {
        return NULL;
    }

    for (i = 0; i < leaf->num_keys; i++) {
        if (leaf->keys[i] == key) {
            return leaf->values[i];
        }
    }

    return NULL;
}

/*
 * 함수명: bptree_insert_into_leaf
 * ----------------------------------------
 * 기능: 공간이 남아 있는 leaf에 key-value를 정렬 순서로 삽입한다.
 *
 * 핵심 흐름:
 *   1. key가 들어갈 위치를 찾는다.
 *   2. 뒤쪽 key/value를 한 칸씩 민다.
 *   3. 빈 위치에 새 key/value를 저장한다.
 *
 * 주의:
 *   - 이 함수는 leaf에 공간이 있을 때만 호출한다.
 */
void bptree_insert_into_leaf(BPTreeNode *leaf, long long key, void *value) {
    int insertion_point;
    int i;

    if (leaf == NULL) {
        return;
    }

    insertion_point = 0;
    while (insertion_point < leaf->num_keys &&
           leaf->keys[insertion_point] < key) {
        insertion_point++;
    }

    for (i = leaf->num_keys; i > insertion_point; i--) {
        leaf->keys[i] = leaf->keys[i - 1];
        leaf->values[i] = leaf->values[i - 1];
    }

    leaf->keys[insertion_point] = key;
    leaf->values[insertion_point] = value;
    leaf->num_keys++;
}

static int bptree_insert_into_leaf_after_splitting(BPTree *tree,
                                                   BPTreeNode *leaf,
                                                   long long key,
                                                   void *value) {
    BPTreeNode *new_leaf;
    long long temp_keys[BPTREE_ORDER];
    void *temp_values[BPTREE_ORDER];
    int insertion_index;
    int split;
    int i;
    int j;

    new_leaf = bptree_create_node(BPTREE_LEAF);
    if (new_leaf == NULL) {
        return FAILURE;
    }

    insertion_index = 0;
    while (insertion_index < bptree_max_keys() &&
           leaf->keys[insertion_index] < key) {
        insertion_index++;
    }

    for (i = 0, j = 0; i < leaf->num_keys; i++, j++) {
        if (j == insertion_index) {
            j++;
        }
        temp_keys[j] = leaf->keys[i];
        temp_values[j] = leaf->values[i];
    }

    temp_keys[insertion_index] = key;
    temp_values[insertion_index] = value;

    split = bptree_cut(BPTREE_ORDER);
    leaf->num_keys = 0;
    for (i = 0; i < split; i++) {
        leaf->keys[i] = temp_keys[i];
        leaf->values[i] = temp_values[i];
        leaf->num_keys++;
    }

    for (i = split, j = 0; i < BPTREE_ORDER; i++, j++) {
        new_leaf->keys[j] = temp_keys[i];
        new_leaf->values[j] = temp_values[i];
        new_leaf->num_keys++;
    }

    new_leaf->next = leaf->next;
    leaf->next = new_leaf;
    new_leaf->parent = leaf->parent;

    bptree_insert_into_parent(tree, leaf, new_leaf->keys[0], new_leaf);
    return SUCCESS;
}

/*
 * 함수명: bptree_split_leaf
 * ----------------------------------------
 * 기능: leaf overflow를 처리하기 위한 공개 설명용 wrapper다.
 *
 * 핵심 흐름:
 *   - 실제 삽입 중 split은 새 key/value가 필요하므로 내부 helper가 처리한다.
 *
 * 개념:
 *   - leaf split에서는 오른쪽 leaf의 첫 key를 부모로 복사한다.
 *   - 그 key는 오른쪽 leaf로 내려가는 길잡이 separator가 된다.
 */
void bptree_split_leaf(BPTree *tree, BPTreeNode *leaf) {
    (void)tree;
    (void)leaf;
}

static int bptree_get_left_index(BPTreeNode *parent, BPTreeNode *left) {
    int left_index;

    left_index = 0;
    while (left_index <= parent->num_keys &&
           parent->children[left_index] != left) {
        left_index++;
    }

    return left_index;
}

static void bptree_insert_into_internal(BPTreeNode *parent, int left_index,
                                        long long key, BPTreeNode *right) {
    int i;

    for (i = parent->num_keys; i > left_index; i--) {
        parent->keys[i] = parent->keys[i - 1];
    }
    for (i = parent->num_keys + 1; i > left_index + 1; i--) {
        parent->children[i] = parent->children[i - 1];
    }

    parent->keys[left_index] = key;
    parent->children[left_index + 1] = right;
    parent->num_keys++;
    right->parent = parent;
}

static int bptree_insert_into_internal_after_splitting(BPTree *tree,
                                                       BPTreeNode *old_node,
                                                       int left_index,
                                                       long long key,
                                                       BPTreeNode *right) {
    BPTreeNode *new_node;
    BPTreeNode *child;
    long long temp_keys[BPTREE_ORDER];
    BPTreeNode *temp_children[BPTREE_ORDER + 1];
    long long promote_key;
    int split;
    int i;
    int j;

    for (i = 0, j = 0; i < old_node->num_keys + 1; i++, j++) {
        if (j == left_index + 1) {
            j++;
        }
        temp_children[j] = old_node->children[i];
    }
    temp_children[left_index + 1] = right;

    for (i = 0, j = 0; i < old_node->num_keys; i++, j++) {
        if (j == left_index) {
            j++;
        }
        temp_keys[j] = old_node->keys[i];
    }
    temp_keys[left_index] = key;

    split = bptree_cut(BPTREE_ORDER);
    new_node = bptree_create_node(BPTREE_INTERNAL);
    if (new_node == NULL) {
        return FAILURE;
    }

    old_node->num_keys = 0;
    for (i = 0; i < split - 1; i++) {
        old_node->children[i] = temp_children[i];
        if (old_node->children[i] != NULL) {
            old_node->children[i]->parent = old_node;
        }
        old_node->keys[i] = temp_keys[i];
        old_node->num_keys++;
    }
    old_node->children[i] = temp_children[i];
    if (old_node->children[i] != NULL) {
        old_node->children[i]->parent = old_node;
    }

    promote_key = temp_keys[split - 1];

    for (++i, j = 0; i < BPTREE_ORDER + 1; i++, j++) {
        new_node->children[j] = temp_children[i];
        child = new_node->children[j];
        if (child != NULL) {
            child->parent = new_node;
        }
    }
    for (i = split, j = 0; i < BPTREE_ORDER; i++, j++) {
        new_node->keys[j] = temp_keys[i];
        new_node->num_keys++;
    }

    new_node->parent = old_node->parent;
    bptree_insert_into_parent(tree, old_node, promote_key, new_node);
    return SUCCESS;
}

/*
 * 함수명: bptree_insert_into_parent
 * ----------------------------------------
 * 기능: node split 후 부모에 separator key와 오른쪽 child를 연결한다.
 *
 * 핵심 흐름:
 *   1. 부모가 없으면 새 root를 만든다.
 *   2. 부모에 공간이 있으면 key/right child를 끼워 넣는다.
 *   3. 부모도 가득 찼으면 internal split을 재귀적으로 일으킨다.
 *
 * 개념:
 *   - root split은 트리 높이가 증가하는 유일한 순간이다.
 *   - split 전파 때문에 B+ 트리는 항상 균형을 유지한다.
 */
void bptree_insert_into_parent(BPTree *tree, BPTreeNode *left,
                               long long key, BPTreeNode *right) {
    BPTreeNode *parent;
    BPTreeNode *root;
    int left_index;

    if (tree == NULL || left == NULL || right == NULL) {
        return;
    }

    parent = left->parent;
    if (parent == NULL) {
        root = bptree_create_node(BPTREE_INTERNAL);
        if (root == NULL) {
            return;
        }

        root->keys[0] = key;
        root->children[0] = left;
        root->children[1] = right;
        root->num_keys = 1;
        left->parent = root;
        right->parent = root;
        tree->root = root;
        return;
    }

    left_index = bptree_get_left_index(parent, left);
    if (parent->num_keys < bptree_max_keys()) {
        bptree_insert_into_internal(parent, left_index, key, right);
        return;
    }

    bptree_insert_into_internal_after_splitting(tree, parent, left_index,
                                                key, right);
}

/*
 * 함수명: bptree_split_internal
 * ----------------------------------------
 * 기능: internal overflow를 처리하기 위한 공개 설명용 wrapper다.
 *
 * 개념:
 *   - internal split에서는 중간 key를 부모로 이동(promote)한다.
 *   - leaf split처럼 오른쪽 첫 key를 복사하지 않는 점이 다르다.
 */
void bptree_split_internal(BPTree *tree, BPTreeNode *node) {
    (void)tree;
    (void)node;
}

/*
 * 함수명: bptree_insert
 * ----------------------------------------
 * 기능: B+ 트리에 unique key-value 한 쌍을 삽입한다.
 *
 * 핵심 흐름:
 *   1. 중복 key가 있으면 실패한다.
 *   2. 빈 트리면 leaf root를 만든다.
 *   3. leaf에 공간이 있으면 바로 정렬 삽입한다.
 *   4. leaf가 가득 차 있으면 split하고 부모로 separator를 올린다.
 *
 * 개념:
 *   - 이 코어는 duplicate key를 허용하지 않는다.
 *   - game_win_count 중복은 index manager가 OffsetList로 처리한다.
 */
int bptree_insert(BPTree *tree, long long key, void *value) {
    BPTreeNode *leaf;

    if (tree == NULL) {
        return FAILURE;
    }

    if (bptree_search(tree, key) != NULL) {
        return FAILURE;
    }

    if (tree->root == NULL) {
        tree->root = bptree_create_node(BPTREE_LEAF);
        if (tree->root == NULL) {
            return FAILURE;
        }
        bptree_insert_into_leaf(tree->root, key, value);
        tree->count++;
        return SUCCESS;
    }

    leaf = bptree_find_leaf(tree, key);
    if (leaf == NULL) {
        return FAILURE;
    }

    if (leaf->num_keys < bptree_max_keys()) {
        bptree_insert_into_leaf(leaf, key, value);
        tree->count++;
        return SUCCESS;
    }

    if (bptree_insert_into_leaf_after_splitting(tree, leaf, key, value) != SUCCESS) {
        return FAILURE;
    }

    tree->count++;
    return SUCCESS;
}

static void bptree_destroy_nodes(BPTreeNode *node) {
    int i;

    if (node == NULL) {
        return;
    }

    if (node->type == BPTREE_INTERNAL) {
        for (i = 0; i <= node->num_keys; i++) {
            bptree_destroy_nodes(node->children[i]);
        }
    }

    free(node);
}

/*
 * 함수명: bptree_destroy
 * ----------------------------------------
 * 기능: B+ 트리 node 메모리를 모두 해제한다.
 *
 * 주의:
 *   - values[]가 가리키는 RowRef/OffsetList는 index manager 소유다.
 *   - 따라서 이 함수는 tree node만 해제한다.
 */
void bptree_destroy(BPTree *tree) {
    if (tree == NULL) {
        return;
    }

    bptree_destroy_nodes(tree->root);
    free(tree);
}

static void bptree_collect_stats_node(BPTreeNode *node, int depth,
                                      BPTreeStats *stats) {
    int i;

    if (node == NULL || stats == NULL) {
        return;
    }

    stats->node_count++;
    stats->key_count += node->num_keys;
    if (depth > stats->height) {
        stats->height = depth;
    }

    if (node->type == BPTREE_LEAF) {
        stats->leaf_count++;
        return;
    }

    for (i = 0; i <= node->num_keys; i++) {
        bptree_collect_stats_node(node->children[i], depth + 1, stats);
    }
}

void bptree_collect_stats(BPTree *tree, BPTreeStats *stats) {
    if (stats == NULL) {
        return;
    }

    memset(stats, 0, sizeof(*stats));
    if (tree == NULL || tree->root == NULL) {
        return;
    }

    bptree_collect_stats_node(tree->root, 1, stats);
}
