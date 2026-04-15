#include "bptree.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ─────────────────────────────────────────────────────────
 * 내부(static) 함수 선언
 * ───────────────────────────────────────────────────────── */
static void bptree_insert_into_leaf(BPTreeNode *leaf, long long key, void *value);
static void bptree_split_leaf(BPTree *tree, BPTreeNode *leaf);
static void bptree_insert_into_parent(BPTree *tree, BPTreeNode *left,
                                      long long key, BPTreeNode *right);
static void bptree_split_internal(BPTree *tree, BPTreeNode *node);
static void bptree_destroy_node(BPTreeNode *node);

/* ─────────────────────────────────────────────────────────
 * 함수명: bptree_create
 * ─────────────────────────────────────────────────────────
 * 기능: 빈 B+ 트리를 생성하고 반환한다.
 *
 * 핵심 흐름:
 *   1. BPTree 구조체를 malloc
 *   2. root=NULL, count=0, order 설정
 *
 * 개념:
 *   - order는 한 노드의 최대 자식 수
 *   - order-1이 한 노드의 최대 key 수
 *   - 빈 트리는 root가 NULL인 상태
 * ───────────────────────────────────────────────────────── */
BPTree *bptree_create(int order) {
    BPTree *tree;

    tree = (BPTree *)malloc(sizeof(BPTree));
    if (tree == NULL) {
        return NULL;
    }

    tree->root = NULL;
    tree->order = order;
    tree->count = 0;
    return tree;
}

/* ─────────────────────────────────────────────────────────
 * 함수명: bptree_create_node
 * ─────────────────────────────────────────────────────────
 * 기능: 새 B+ 트리 노드를 생성한다.
 *
 * 핵심 흐름:
 *   1. malloc으로 노드 할당
 *   2. type 설정, num_keys=0
 *   3. 모든 포인터(children, values, next, parent)를 NULL로 초기화
 * ───────────────────────────────────────────────────────── */
BPTreeNode *bptree_create_node(NodeType type) {
    BPTreeNode *node;

    node = (BPTreeNode *)malloc(sizeof(BPTreeNode));
    if (node == NULL) {
        return NULL;
    }

    node->type = type;
    node->num_keys = 0;
    memset(node->keys, 0, sizeof(node->keys));
    memset(node->children, 0, sizeof(node->children));
    memset(node->values, 0, sizeof(node->values));
    node->next = NULL;
    node->parent = NULL;
    return node;
}

/* ─────────────────────────────────────────────────────────
 * 함수명: bptree_find_leaf
 * ─────────────────────────────────────────────────────────
 * 기능: 주어진 key가 들어갈(또는 있을) 리프 노드를 탐색한다.
 *
 * 핵심 흐름:
 *   1. root부터 시작
 *   2. 내부 노드이면 key와 비교하며 적절한 자식으로 내려감
 *   3. 리프 도달 시 반환
 *
 * 개념:
 *   - key < keys[i] 인 첫 번째 i를 찾아 children[i]로 이동
 *   - 모든 키보다 크면 children[num_keys]로 이동
 *   - 이것이 B+ 트리 검색의 핵심 경로: root → leaf
 * ───────────────────────────────────────────────────────── */
BPTreeNode *bptree_find_leaf(BPTree *tree, long long key) {
    BPTreeNode *node;
    int i;

    if (tree == NULL || tree->root == NULL) {
        return NULL;
    }

    node = tree->root;
    while (node->type == BPTREE_INTERNAL) {
        /* key < keys[i] 인 첫 번째 위치를 찾아 해당 자식으로 이동 */
        i = 0;
        while (i < node->num_keys && key >= node->keys[i]) {
            i++;
        }
        node = node->children[i];
    }

    return node;
}

/* ─────────────────────────────────────────────────────────
 * 함수명: bptree_search
 * ─────────────────────────────────────────────────────────
 * 기능: key에 해당하는 값을 B+ 트리에서 찾아 반환한다.
 *
 * 핵심 흐름:
 *   1. root에서 시작하여 리프 노드까지 내려간다 (bptree_find_leaf)
 *   2. 리프 노드의 keys[]에서 해당 key를 선형 탐색한다
 *   3. 찾으면 values[i] 반환, 못 찾으면 NULL 반환
 *
 * 개념:
 *   - B+ 트리의 검색은 항상 root → leaf 경로를 따른다
 *   - 시간복잡도: O(log_m n), m은 차수, n은 전체 키 수
 *   - WHERE id = 42 또는 WHERE game_win_count = 42 → 이 함수가 호출됨
 * ───────────────────────────────────────────────────────── */
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

/* ─────────────────────────────────────────────────────────
 * 함수명: bptree_insert_into_leaf
 * ─────────────────────────────────────────────────────────
 * 기능: leaf에 key를 정렬 위치에 삽입한다 (공간이 있을 때).
 *
 * 핵심 흐름:
 *   1. 삽입할 정렬 위치를 찾는다
 *   2. 기존 key/value를 오른쪽으로 밀어낸다
 *   3. 해당 위치에 key/value를 삽입한다
 *
 * 주의: 이 함수는 leaf에 빈 공간이 있다는 전제 하에 호출된다.
 * ───────────────────────────────────────────────────────── */
static void bptree_insert_into_leaf(BPTreeNode *leaf, long long key, void *value) {
    int i;
    int insert_pos;

    /* 정렬 위치 탐색: key보다 큰 첫 번째 위치 */
    insert_pos = 0;
    while (insert_pos < leaf->num_keys && leaf->keys[insert_pos] < key) {
        insert_pos++;
    }

    /* 기존 원소를 오른쪽으로 이동 */
    for (i = leaf->num_keys; i > insert_pos; i--) {
        leaf->keys[i] = leaf->keys[i - 1];
        leaf->values[i] = leaf->values[i - 1];
    }

    leaf->keys[insert_pos] = key;
    leaf->values[insert_pos] = value;
    leaf->num_keys++;
}

/* ─────────────────────────────────────────────────────────
 * 함수명: bptree_split_leaf
 * ─────────────────────────────────────────────────────────
 * 기능: 가득 찬 leaf 노드를 분할한다.
 *
 * 핵심 흐름:
 *   1. 새 leaf 생성
 *   2. 키/값의 뒤 절반을 새 노드로 이동
 *   3. leaf linked list 갱신 (leaf→next = new, new→next = old_next)
 *   4. 새 노드의 첫 key를 부모로 올림
 *
 * 개념:
 *   - 왜 leaf overflow 시 바로 split 하는가?
 *     → B+ 트리에서 노드가 order-1개 이상의 key를 가질 수 없기 때문.
 *     → 오버플로가 발생하면 반드시 분할하여 트리 균형을 유지해야 한다.
 *   - 리프 분할 시 **새 노드의 첫 번째 key**가 부모로 올라감
 *     (내부 노드 분할과 구분됨: 내부 분할은 중간 key가 promote)
 * ───────────────────────────────────────────────────────── */
static void bptree_split_leaf(BPTree *tree, BPTreeNode *leaf) {
    BPTreeNode *new_leaf;
    int split;
    int i;
    long long up_key;

    new_leaf = bptree_create_node(BPTREE_LEAF);
    if (new_leaf == NULL) {
        return;
    }

    /* 분할 위치: 절반 지점 */
    split = (tree->order - 1) / 2;

    /* 뒤 절반을 새 leaf로 이동 */
    for (i = split; i < leaf->num_keys; i++) {
        new_leaf->keys[i - split] = leaf->keys[i];
        new_leaf->values[i - split] = leaf->values[i];
        leaf->values[i] = NULL;
    }
    new_leaf->num_keys = leaf->num_keys - split;
    leaf->num_keys = split;

    /* leaf linked list 갱신 */
    new_leaf->next = leaf->next;
    leaf->next = new_leaf;
    new_leaf->parent = leaf->parent;

    /* 왜 오른쪽 leaf의 첫 key를 부모로 올리는가?
     * → 리프 분할에서는 새 리프의 모든 키가 그대로 리프에 남아야 하므로,
     *   새 리프의 첫 번째 key를 "복사"하여 부모의 길잡이 키로 사용한다.
     *   (내부 노드 분할과 달리 key가 "이동"이 아니라 "복사"됨) */
    up_key = new_leaf->keys[0];
    bptree_insert_into_parent(tree, leaf, up_key, new_leaf);
}

/* ─────────────────────────────────────────────────────────
 * 함수명: bptree_insert_into_parent
 * ─────────────────────────────────────────────────────────
 * 기능: 분할 후 부모에 key와 새 자식(right)을 추가한다.
 *
 * 핵심 흐름:
 *   1. 부모가 없으면 새 root 생성
 *   2. 부모에 공간이 있으면 정렬 위치에 삽입
 *   3. 부모도 가득 차면 internal split
 *
 * 개념:
 *   - split 후 key가 부모로 올라가고, 부모도 가득 차면 재귀적 split 발생
 *   - root split 시 트리 높이가 1 증가한다
 * ───────────────────────────────────────────────────────── */
static void bptree_insert_into_parent(BPTree *tree, BPTreeNode *left,
                                      long long key, BPTreeNode *right) {
    BPTreeNode *parent;
    int i;
    int insert_pos;

    parent = left->parent;

    /* 부모가 없으면 새 root 생성 → 트리 높이 증가 */
    if (parent == NULL) {
        BPTreeNode *new_root = bptree_create_node(BPTREE_INTERNAL);
        if (new_root == NULL) {
            return;
        }
        new_root->keys[0] = key;
        new_root->children[0] = left;
        new_root->children[1] = right;
        new_root->num_keys = 1;
        left->parent = new_root;
        right->parent = new_root;
        tree->root = new_root;
        return;
    }

    /* 부모에서 left 자식의 위치를 찾는다 */
    insert_pos = 0;
    while (insert_pos <= parent->num_keys && parent->children[insert_pos] != left) {
        insert_pos++;
    }

    /* key와 right 자식을 정렬 위치에 삽입 */
    for (i = parent->num_keys; i > insert_pos; i--) {
        parent->keys[i] = parent->keys[i - 1];
        parent->children[i + 1] = parent->children[i];
    }
    parent->keys[insert_pos] = key;
    parent->children[insert_pos + 1] = right;
    parent->num_keys++;
    right->parent = parent;

    /* 부모가 가득 찼으면 internal split */
    if (parent->num_keys >= tree->order - 1) {
        bptree_split_internal(tree, parent);
    }
}

/* ─────────────────────────────────────────────────────────
 * 함수명: bptree_split_internal
 * ─────────────────────────────────────────────────────────
 * 기능: 가득 찬 internal node를 분할한다.
 *
 * 핵심 흐름:
 *   1. 중간 key를 찾는다 (promote할 key)
 *   2. 새 internal 노드를 만들고 중간 이후의 키/자식을 이동
 *   3. 중간 key를 부모로 올린다 (promote)
 *
 * 개념:
 *   - internal split에서는 리프처럼 "새 노드의 첫 key 복사"가 아니라
 *     "중간 key를 부모로 이동"하는 점이 핵심 차이
 *   - 중간 key는 새 노드에도, 기존 노드에도 남지 않고 오직 부모로만 간다
 * ───────────────────────────────────────────────────────── */
static void bptree_split_internal(BPTree *tree, BPTreeNode *node) {
    BPTreeNode *new_node;
    int split;
    int i;
    long long up_key;

    new_node = bptree_create_node(BPTREE_INTERNAL);
    if (new_node == NULL) {
        return;
    }

    /* 중간 위치: 이 key가 부모로 올라간다 */
    split = node->num_keys / 2;
    up_key = node->keys[split];

    /* 중간 이후의 키/자식을 새 노드로 이동
     * 주의: 중간 key 자체는 promote되므로 새 노드에 포함하지 않는다 */
    for (i = split + 1; i < node->num_keys; i++) {
        new_node->keys[i - split - 1] = node->keys[i];
    }
    for (i = split + 1; i <= node->num_keys; i++) {
        new_node->children[i - split - 1] = node->children[i];
        if (node->children[i] != NULL) {
            node->children[i]->parent = new_node;
            node->children[i] = NULL;
        }
    }
    new_node->num_keys = node->num_keys - split - 1;
    node->num_keys = split;
    new_node->parent = node->parent;

    bptree_insert_into_parent(tree, node, up_key, new_node);
}

/* ─────────────────────────────────────────────────────────
 * 함수명: bptree_insert
 * ─────────────────────────────────────────────────────────
 * 기능: key-value 쌍을 B+ 트리에 삽입한다.
 *
 * 핵심 흐름:
 *   1. 트리가 비어있으면 새 리프를 root로 생성 후 삽입
 *   2. 이미 같은 key가 있으면 실패 (-1 반환)
 *   3. 삽입할 leaf를 탐색
 *   4. 공간이 있으면 정렬 위치에 삽입
 *   5. 가득 찼으면 먼저 삽입 후 leaf split
 *
 * 개념:
 *   - 트리 코어는 duplicate key를 허용하지 않는다
 *   - 왜? id는 unique PK이므로 당연히 중복 불가.
 *     game_win_count는 중복이 가능하지만, 트리에서는 key를 unique로 유지하고
 *     value를 OffsetList로 두어 중복 row를 관리한다 (index manager 레이어 책임).
 *   - split 후 부모로 key가 올라가고, 부모도 가득 차면 재귀 split
 *   - root split 시 트리 높이 증가
 * ───────────────────────────────────────────────────────── */
int bptree_insert(BPTree *tree, long long key, void *value) {
    BPTreeNode *leaf;

    if (tree == NULL) {
        return -1;
    }

    /* 빈 트리: 첫 번째 리프를 root로 생성 */
    if (tree->root == NULL) {
        leaf = bptree_create_node(BPTREE_LEAF);
        if (leaf == NULL) {
            return -1;
        }
        leaf->keys[0] = key;
        leaf->values[0] = value;
        leaf->num_keys = 1;
        tree->root = leaf;
        tree->count = 1;
        return 0;
    }

    /* duplicate key 방어: 이미 같은 key가 있으면 실패 */
    if (bptree_search(tree, key) != NULL) {
        return -1;
    }

    /* 삽입할 leaf 탐색 */
    leaf = bptree_find_leaf(tree, key);
    if (leaf == NULL) {
        return -1;
    }

    /* leaf에 삽입 */
    bptree_insert_into_leaf(leaf, key, value);
    tree->count++;

    /* leaf가 가득 찼으면 split
     * order-1이 최대 key 수이므로, num_keys가 order-1 이상이면 overflow */
    if (leaf->num_keys >= tree->order - 1) {
        bptree_split_leaf(tree, leaf);
    }

    return 0;
}

/* ─────────────────────────────────────────────────────────
 * 함수명: bptree_destroy_node (내부용)
 * ─────────────────────────────────────────────────────────
 * 기능: 노드 하나와 그 하위 자식들을 재귀적으로 해제한다.
 *       리프의 values는 해제하지 않는다 (상위 레이어 책임).
 * ───────────────────────────────────────────────────────── */
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

/* ─────────────────────────────────────────────────────────
 * 함수명: bptree_destroy
 * ─────────────────────────────────────────────────────────
 * 기능: B+ 트리의 모든 노드 메모리를 해제한다.
 *
 * 핵심 흐름:
 *   자식부터 재귀 해제 후 root와 tree 구조체를 free한다.
 *
 * 주의:
 *   리프 노드의 values[] 포인터가 가리키는 데이터(RowRef, OffsetList 등)는
 *   이 함수에서 해제하지 않는다. 상위 index manager가 먼저 해제해야 한다.
 * ───────────────────────────────────────────────────────── */
void bptree_destroy(BPTree *tree) {
    if (tree == NULL) {
        return;
    }

    bptree_destroy_node(tree->root);
    free(tree);
}
