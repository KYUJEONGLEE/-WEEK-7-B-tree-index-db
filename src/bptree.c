#include "bptree.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int bptree_max_keys(const BPTree *tree) {
    return tree->order - 1;
}

static int bptree_child_index(BPTreeNode *parent, BPTreeNode *child) {
    int i;

    for (i = 0; i <= parent->num_keys; i++) {
        if (parent->children[i] == child) {
            return i;
        }
    }

    return FAILURE;
}

/*
 * 함수명: bptree_create
 * ------------------------------------------------------------
 * 기능:
 *   빈 B+ 트리 컨테이너를 만든다.
 *
 * 핵심 흐름:
 *   1. order 값이 구현 가능한 범위인지 확인한다.
 *   2. root가 없는 빈 tree를 할당한다.
 *
 * 개념:
 *   order는 한 internal node가 가질 수 있는 최대 child 수이다.
 *   한 node의 최대 key 수는 order - 1이다.
 *
 * 반환값:
 *   성공 시 BPTree 포인터, 실패 시 NULL.
 */
BPTree *bptree_create(int order) {
    BPTree *tree;

    if (order < BPTREE_MIN_ORDER || order > BPTREE_ORDER) {
        fprintf(stderr, "Error: Invalid B+ tree order.\n");
        return NULL;
    }

    tree = (BPTree *)calloc(1, sizeof(BPTree));
    if (tree == NULL) {
        fprintf(stderr, "Error: Failed to allocate B+ tree.\n");
        return NULL;
    }

    tree->order = order;
    return tree;
}

/*
 * 함수명: bptree_create_node
 * ------------------------------------------------------------
 * 기능:
 *   internal 또는 leaf node 하나를 만든다.
 *
 * 핵심 흐름:
 *   calloc으로 모든 pointer를 NULL로 초기화한 뒤 node type만 설정한다.
 *
 * 개념:
 *   B+ 트리에서 internal node는 길잡이 key와 child pointer를 갖고,
 *   leaf node는 실제 value pointer를 갖는다.
 */
BPTreeNode *bptree_create_node(NodeType type) {
    BPTreeNode *node;

    node = (BPTreeNode *)calloc(1, sizeof(BPTreeNode));
    if (node == NULL) {
        fprintf(stderr, "Error: Failed to allocate B+ tree node.\n");
        return NULL;
    }

    node->type = type;
    return node;
}

/*
 * 함수명: bptree_find_leaf
 * ------------------------------------------------------------
 * 기능:
 *   key가 존재하거나 삽입될 leaf node를 찾는다.
 *
 * 핵심 흐름:
 *   1. root에서 시작한다.
 *   2. internal node에서는 key가 들어갈 child 범위를 고른다.
 *   3. leaf node에 도착하면 반환한다.
 *
 * 개념:
 *   B+ 트리 검색은 항상 root -> leaf 경로를 따른다.
 *   key가 모든 guide key보다 크거나 같으면 가장 오른쪽 child로 이동한다.
 */
BPTreeNode *bptree_find_leaf(BPTree *tree, long long key) {
    BPTreeNode *node;
    int index;

    if (tree == NULL || tree->root == NULL) {
        return NULL;
    }

    node = tree->root;
    while (node->type == BPTREE_INTERNAL) {
        index = 0;
        while (index < node->num_keys && key >= node->keys[index]) {
            index++;
        }
        node = node->children[index];
    }

    return node;
}

/*
 * 함수명: bptree_search
 * ------------------------------------------------------------
 * 기능:
 *   key에 해당하는 value pointer를 B+ 트리에서 찾아 반환한다.
 *
 * 핵심 흐름:
 *   1. bptree_find_leaf로 leaf node까지 내려간다.
 *   2. leaf의 keys 배열에서 exact match를 찾는다.
 *   3. 찾으면 values[i], 없으면 NULL을 반환한다.
 *
 * 개념:
 *   id 인덱스와 game_win_count 인덱스가 모두 이 검색 함수를 공유한다.
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
 * ------------------------------------------------------------
 * 기능:
 *   공간이 있는 leaf node에 key-value를 정렬 위치에 삽입한다.
 *
 * 핵심 흐름:
 *   오른쪽부터 한 칸씩 밀어 key 정렬 순서를 유지한다.
 *
 * 개념:
 *   leaf 내부 key가 정렬되어 있어야 이후 검색과 split이 단순해진다.
 */
static void bptree_insert_into_leaf(BPTreeNode *leaf, long long key, void *value) {
    int index;
    int i;

    index = 0;
    while (index < leaf->num_keys && leaf->keys[index] < key) {
        index++;
    }

    for (i = leaf->num_keys; i > index; i--) {
        leaf->keys[i] = leaf->keys[i - 1];
        leaf->values[i] = leaf->values[i - 1];
    }

    leaf->keys[index] = key;
    leaf->values[index] = value;
    leaf->num_keys++;
}

static int bptree_insert_into_parent(BPTree *tree, BPTreeNode *left,
                                     long long key, BPTreeNode *right);

/*
 * 함수명: bptree_split_leaf
 * ------------------------------------------------------------
 * 기능:
 *   가득 찬 leaf에 새 key-value를 넣기 위해 leaf를 둘로 나눈다.
 *
 * 핵심 흐름:
 *   1. 기존 key들과 새 key를 임시 배열에 정렬해 담는다.
 *   2. 앞 절반은 기존 leaf에, 뒤 절반은 새 leaf에 둔다.
 *   3. leaf linked list를 갱신한다.
 *   4. 오른쪽 leaf의 첫 key를 부모로 올린다.
 *
 * 개념:
 *   B+ 트리 leaf split에서는 오른쪽 leaf의 첫 key를 부모 guide key로
 *   "복사"한다. 실제 데이터 pointer는 leaf에 남아 있다.
 */
static int bptree_split_leaf(BPTree *tree, BPTreeNode *leaf,
                             long long key, void *value) {
    BPTreeNode *right;
    long long temp_keys[BPTREE_ORDER];
    void *temp_values[BPTREE_ORDER];
    int temp_count;
    int insert_index;
    int split;
    int i;
    int j;

    right = bptree_create_node(BPTREE_LEAF);
    if (right == NULL) {
        return FAILURE;
    }

    temp_count = leaf->num_keys + 1;
    insert_index = 0;
    while (insert_index < leaf->num_keys && leaf->keys[insert_index] < key) {
        insert_index++;
    }

    for (i = 0, j = 0; i < temp_count; i++) {
        if (i == insert_index) {
            temp_keys[i] = key;
            temp_values[i] = value;
        } else {
            temp_keys[i] = leaf->keys[j];
            temp_values[i] = leaf->values[j];
            j++;
        }
    }

    split = temp_count / 2;
    leaf->num_keys = 0;
    for (i = 0; i < split; i++) {
        leaf->keys[i] = temp_keys[i];
        leaf->values[i] = temp_values[i];
        leaf->num_keys++;
    }

    right->num_keys = 0;
    for (i = split, j = 0; i < temp_count; i++, j++) {
        right->keys[j] = temp_keys[i];
        right->values[j] = temp_values[i];
        right->num_keys++;
    }

    right->next = leaf->next;
    leaf->next = right;
    right->parent = leaf->parent;

    return bptree_insert_into_parent(tree, leaf, right->keys[0], right);
}

static int bptree_insert_into_internal(BPTreeNode *parent, int left_index,
                                       long long key, BPTreeNode *right) {
    int i;

    for (i = parent->num_keys; i > left_index; i--) {
        parent->keys[i] = parent->keys[i - 1];
        parent->children[i + 1] = parent->children[i];
    }

    parent->keys[left_index] = key;
    parent->children[left_index + 1] = right;
    parent->num_keys++;
    right->parent = parent;
    return SUCCESS;
}

/*
 * 함수명: bptree_split_internal
 * ------------------------------------------------------------
 * 기능:
 *   가득 찬 internal node에 새 child를 넣기 위해 internal node를 나눈다.
 *
 * 핵심 흐름:
 *   1. 기존 key/child와 새 key/child를 임시 배열에 정렬해 담는다.
 *   2. 중간 key를 부모로 promote한다.
 *   3. promote key 왼쪽은 기존 node, 오른쪽은 새 node가 가진다.
 *
 * 개념:
 *   internal split은 leaf split과 다르다. leaf에서는 오른쪽 첫 key를
 *   부모로 복사하지만, internal에서는 중간 key가 부모로 이동한다.
 */
static int bptree_split_internal(BPTree *tree, BPTreeNode *node,
                                 int left_index, long long key,
                                 BPTreeNode *right_child) {
    BPTreeNode *right;
    long long temp_keys[BPTREE_ORDER];
    BPTreeNode *temp_children[BPTREE_ORDER + 1];
    int temp_key_count;
    int promote_index;
    long long promote_key;
    int i;
    int j;

    right = bptree_create_node(BPTREE_INTERNAL);
    if (right == NULL) {
        return FAILURE;
    }

    temp_key_count = node->num_keys + 1;
    for (i = 0; i <= node->num_keys; i++) {
        temp_children[i] = node->children[i];
    }
    for (i = 0; i < node->num_keys; i++) {
        temp_keys[i] = node->keys[i];
    }

    for (i = temp_key_count; i > left_index + 1; i--) {
        temp_children[i] = temp_children[i - 1];
    }
    temp_children[left_index + 1] = right_child;

    for (i = temp_key_count - 1; i > left_index; i--) {
        temp_keys[i] = temp_keys[i - 1];
    }
    temp_keys[left_index] = key;

    promote_index = temp_key_count / 2;
    promote_key = temp_keys[promote_index];

    node->num_keys = 0;
    for (i = 0; i < promote_index; i++) {
        node->keys[i] = temp_keys[i];
        node->children[i] = temp_children[i];
        if (node->children[i] != NULL) {
            node->children[i]->parent = node;
        }
        node->num_keys++;
    }
    node->children[promote_index] = temp_children[promote_index];
    if (node->children[promote_index] != NULL) {
        node->children[promote_index]->parent = node;
    }

    right->num_keys = 0;
    for (i = promote_index + 1, j = 0; i < temp_key_count; i++, j++) {
        right->keys[j] = temp_keys[i];
        right->children[j] = temp_children[i];
        if (right->children[j] != NULL) {
            right->children[j]->parent = right;
        }
        right->num_keys++;
    }
    right->children[right->num_keys] = temp_children[temp_key_count];
    if (right->children[right->num_keys] != NULL) {
        right->children[right->num_keys]->parent = right;
    }
    right->parent = node->parent;

    return bptree_insert_into_parent(tree, node, promote_key, right);
}

/*
 * 함수명: bptree_insert_into_parent
 * ------------------------------------------------------------
 * 기능:
 *   split 후 생긴 right node와 guide key를 부모 node에 연결한다.
 *
 * 핵심 흐름:
 *   1. 부모가 없으면 새 root를 만든다.
 *   2. 부모에 공간이 있으면 key와 right child를 삽입한다.
 *   3. 부모도 가득 찼으면 internal split을 재귀적으로 수행한다.
 *
 * 개념:
 *   root split이 발생하면 B+ 트리 높이가 1 증가한다.
 */
static int bptree_insert_into_parent(BPTree *tree, BPTreeNode *left,
                                     long long key, BPTreeNode *right) {
    BPTreeNode *parent;
    BPTreeNode *new_root;
    int left_index;

    parent = left->parent;
    if (parent == NULL) {
        new_root = bptree_create_node(BPTREE_INTERNAL);
        if (new_root == NULL) {
            return FAILURE;
        }

        new_root->keys[0] = key;
        new_root->children[0] = left;
        new_root->children[1] = right;
        new_root->num_keys = 1;
        left->parent = new_root;
        right->parent = new_root;
        tree->root = new_root;
        return SUCCESS;
    }

    left_index = bptree_child_index(parent, left);
    if (left_index == FAILURE) {
        return FAILURE;
    }

    if (parent->num_keys < bptree_max_keys(tree)) {
        return bptree_insert_into_internal(parent, left_index, key, right);
    }

    return bptree_split_internal(tree, parent, left_index, key, right);
}

/*
 * 함수명: bptree_insert
 * ------------------------------------------------------------
 * 기능:
 *   B+ 트리에 key-value 한 쌍을 삽입한다.
 *
 * 핵심 흐름:
 *   1. 빈 tree라면 root leaf를 만든다.
 *   2. duplicate key를 먼저 거부한다.
 *   3. leaf에 공간이 있으면 바로 삽입한다.
 *   4. leaf가 가득 찼으면 split 후 부모에 guide key를 반영한다.
 *
 * 개념:
 *   이번 프로젝트에서 duplicate id는 허용하지 않는다.
 *   game_win_count 중복은 tree duplicate가 아니라 OffsetList append로 해결한다.
 */
int bptree_insert(BPTree *tree, long long key, void *value) {
    BPTreeNode *leaf;

    if (tree == NULL || value == NULL) {
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

    if (bptree_search(tree, key) != NULL) {
        return FAILURE;
    }

    leaf = bptree_find_leaf(tree, key);
    if (leaf == NULL) {
        return FAILURE;
    }

    if (leaf->num_keys < bptree_max_keys(tree)) {
        bptree_insert_into_leaf(leaf, key, value);
    } else if (bptree_split_leaf(tree, leaf, key, value) != SUCCESS) {
        return FAILURE;
    }

    tree->count++;
    return SUCCESS;
}

static void bptree_destroy_node(BPTreeNode *node) {
    int i;

    if (node == NULL) {
        return;
    }

    if (node->type == BPTREE_INTERNAL) {
        for (i = 0; i <= node->num_keys; i++) {
            bptree_destroy_node(node->children[i]);
        }
    }

    free(node);
}

/*
 * 함수명: bptree_destroy
 * ------------------------------------------------------------
 * 기능:
 *   B+ 트리 node 메모리를 모두 해제한다.
 *
 * 핵심 흐름:
 *   internal node는 child를 먼저 해제한 뒤 자신을 해제한다.
 *
 * 주의:
 *   B+ 트리 core는 value 포인터의 소유자가 아니다.
 *   RowRef나 OffsetList는 index manager가 별도로 해제해야 한다.
 */
void bptree_destroy(BPTree *tree) {
    if (tree == NULL) {
        return;
    }

    bptree_destroy_node(tree->root);
    free(tree);
}
