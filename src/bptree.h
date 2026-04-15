#ifndef BPTREE_H
#define BPTREE_H

/*
 * B+ 트리(B-Plus Tree) 코어 자료구조 및 인터페이스
 * ─────────────────────────────────────────────────
 * B+ 트리란?
 * - 모든 실제 데이터(레코드 포인터)는 리프 노드에만 존재한다.
 * - 내부 노드는 탐색을 위한 "길잡이 키"만 갖는다.
 * - 리프 노드들은 연결 리스트로 서로 연결되어 범위 검색이 빠르다.
 * - 차수(order) = 한 노드가 가질 수 있는 최대 자식 수.
 *   예: order=4이면 키는 최대 3개, 자식 포인터는 최대 4개.
 *
 * 이 구현은 학습용 인메모리 B+ 트리이며, 디스크 페이지 관리는 하지 않는다.
 * 트리 코어는 duplicate key를 허용하지 않는다.
 */

#define BPTREE_ORDER 128  /* 벤치마크용. 학습 시 4로 줄여도 됨 */

/* 노드 타입: 내부(internal) 노드 vs 리프(leaf) 노드 */
typedef enum { BPTREE_INTERNAL, BPTREE_LEAF } NodeType;

/* B+ 트리 노드 */
typedef struct BPTreeNode {
    NodeType type;                              /* 내부 노드인지 리프 노드인지 */
    int num_keys;                               /* 현재 저장된 키 개수 */
    long long keys[BPTREE_ORDER - 1];           /* 키 배열 (최대 order-1개) */
    struct BPTreeNode *children[BPTREE_ORDER];  /* 자식 포인터 (내부 노드용) */
    void *values[BPTREE_ORDER - 1];             /* 리프 노드용 값 포인터 */
    struct BPTreeNode *next;                    /* 다음 리프 노드 (linked list) */
    struct BPTreeNode *parent;                  /* 부모 노드 */
} BPTreeNode;

/* B+ 트리 */
typedef struct {
    BPTreeNode *root;
    int order;       /* 차수: 한 노드의 최대 자식 수 */
    int count;       /* 전체 key 개수 */
} BPTree;

/*
 * 빈 B+ 트리를 생성하고 반환한다.
 * order는 한 노드의 최대 자식 수이며, order-1이 최대 key 수다.
 */
BPTree *bptree_create(int order);

/*
 * 새 노드를 생성한다.
 */
BPTreeNode *bptree_create_node(NodeType type);

/*
 * 주어진 key가 들어갈(또는 있을) 리프 노드를 탐색한다.
 * root부터 시작하여, 내부 노드에서는 key 비교로 적절한 자식을 선택해 내려간다.
 */
BPTreeNode *bptree_find_leaf(BPTree *tree, long long key);

/*
 * key에 해당하는 값을 B+ 트리에서 찾아 반환한다.
 * 찾으면 values[i], 못 찾으면 NULL.
 * 시간복잡도: O(log n)
 */
void *bptree_search(BPTree *tree, long long key);

/*
 * key-value 쌍을 B+ 트리에 삽입한다.
 * 이미 같은 key가 있으면 실패(-1)를 반환한다.
 * 성공 시 0을 반환한다.
 */
int bptree_insert(BPTree *tree, long long key, void *value);

/*
 * 모든 노드 메모리를 해제한다.
 * value 포인터는 해제하지 않는다 (상위 레이어 책임).
 */
void bptree_destroy(BPTree *tree);

#endif
