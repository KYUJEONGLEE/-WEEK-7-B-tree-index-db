#include "player_index.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * 플레이어 전적 테이블 전용 B+ 트리 인덱스 매니저
 * ─────────────────────────────────────────────────
 * id tree: id(long long) → RowRef* (unique key, 단일 offset)
 * win tree: game_win_count(long long) → POffsetList* (unique key, offset 목록)
 *
 * 왜 duplicate game_win_count를 tree duplicate insert가 아니라 list append로 처리하는가?
 * → B+ 트리 코어를 단순하게 유지하기 위해서다.
 *   트리에서 중복 key를 허용하면 split/search 로직이 복잡해진다.
 *   대신 value를 OffsetList로 두면 같은 트리 구현을 id/game_win_count 모두에 재사용할 수 있다.
 */

/* ─────────────────────────────────────────────────────────
 * RowRef, POffsetList 생성/해제 헬퍼
 * ───────────────────────────────────────────────────────── */

static RowRef *create_row_ref(long offset) {
    RowRef *ref;

    ref = (RowRef *)malloc(sizeof(RowRef));
    if (ref == NULL) {
        return NULL;
    }
    ref->offset = offset;
    return ref;
}

static POffsetList *create_offset_list(long offset) {
    POffsetList *list;
    OffsetNode *node;

    list = (POffsetList *)malloc(sizeof(POffsetList));
    if (list == NULL) {
        return NULL;
    }

    node = (OffsetNode *)malloc(sizeof(OffsetNode));
    if (node == NULL) {
        free(list);
        return NULL;
    }

    node->offset = offset;
    node->next = NULL;
    list->head = node;
    list->tail = node;
    list->count = 1;
    return list;
}

/*
 * 함수명: offset_list_append
 * ─────────────────────────────────────
 * 기능: 기존 POffsetList에 새 offset을 추가한다.
 *
 * 개념:
 *   game_win_count가 같은 여러 row가 있을 때,
 *   B+ 트리 key는 하나만 유지하고 value인 POffsetList에 offset을 계속 추가한다.
 *   이것이 secondary index의 중복 처리 방식이다.
 */
static int offset_list_append(POffsetList *list, long offset) {
    OffsetNode *node;

    node = (OffsetNode *)malloc(sizeof(OffsetNode));
    if (node == NULL) {
        return -1;
    }

    node->offset = offset;
    node->next = NULL;
    list->tail->next = node;
    list->tail = node;
    list->count++;
    return 0;
}

/* ─────────────────────────────────────────────────────────
 * B+ 트리 리프 순회하며 value 해제 (트리 destroy 전에 호출)
 * ───────────────────────────────────────────────────────── */

static void free_id_tree_values(BPTree *tree) {
    BPTreeNode *node;
    int i;

    if (tree == NULL || tree->root == NULL) {
        return;
    }

    /* 가장 왼쪽 리프로 이동 */
    node = tree->root;
    while (node->type == BPTREE_INTERNAL) {
        node = node->children[0];
    }

    /* 리프 연결 리스트를 따라가며 RowRef 해제 */
    while (node != NULL) {
        for (i = 0; i < node->num_keys; i++) {
            free(node->values[i]); /* RowRef* */
            node->values[i] = NULL;
        }
        node = node->next;
    }
}

static void free_win_tree_values(BPTree *tree) {
    BPTreeNode *node;
    int i;
    POffsetList *list;
    OffsetNode *onode;
    OffsetNode *onext;

    if (tree == NULL || tree->root == NULL) {
        return;
    }

    node = tree->root;
    while (node->type == BPTREE_INTERNAL) {
        node = node->children[0];
    }

    while (node != NULL) {
        for (i = 0; i < node->num_keys; i++) {
            list = (POffsetList *)node->values[i];
            if (list != NULL) {
                onode = list->head;
                while (onode != NULL) {
                    onext = onode->next;
                    free(onode);
                    onode = onext;
                }
                free(list);
                node->values[i] = NULL;
            }
        }
        node = node->next;
    }
}

/* ─────────────────────────────────────────────────────────
 * 함수명: player_index_build
 * ─────────────────────────────────────────────────────────
 * 기능: TableData에서 id와 game_win_count 컬럼을 찾아 두 B+ 트리를 빌드한다.
 *
 * 핵심 흐름:
 *   1. 컬럼 이름으로 id, game_win_count 위치를 찾는다
 *   2. 각 row에서 id, game_win_count 문자열을 정수로 변환
 *   3. id_tree에는 RowRef* 삽입 (unique)
 *   4. win_tree에는:
 *      - 기존 key가 있으면 POffsetList에 offset append
 *      - 없으면 새 POffsetList를 만들고 key insert
 * ───────────────────────────────────────────────────────── */
int player_index_build(const TableData *table, PlayerIndexSet *index_set) {
    int id_col;
    int win_col;
    int i;
    long long id_val;
    long long win_val;
    RowRef *ref;
    POffsetList *existing_list;
    POffsetList *new_list;

    if (table == NULL || index_set == NULL) {
        return FAILURE;
    }

    /* 컬럼 위치 탐색 */
    id_col = -1;
    win_col = -1;
    for (i = 0; i < table->col_count; i++) {
        if (strcmp(table->columns[i], "id") == 0) {
            id_col = i;
        } else if (strcmp(table->columns[i], "game_win_count") == 0) {
            win_col = i;
        }
    }

    if (id_col < 0 || win_col < 0) {
        fprintf(stderr, "Error: Required columns (id, game_win_count) not found.\n");
        return FAILURE;
    }

    index_set->id_tree = bptree_create(BPTREE_ORDER);
    index_set->win_tree = bptree_create(BPTREE_ORDER);
    if (index_set->id_tree == NULL || index_set->win_tree == NULL) {
        player_index_free(index_set);
        return FAILURE;
    }

    for (i = 0; i < table->row_count; i++) {
        id_val = atoll(table->rows[i][id_col]);
        win_val = atoll(table->rows[i][win_col]);

        /* id tree: RowRef 삽입 (unique key) */
        ref = create_row_ref(table->offsets[i]);
        if (ref == NULL) {
            player_index_free(index_set);
            return FAILURE;
        }

        /* duplicate id 방어: id는 PK이므로 중복이 있으면 데이터 오류 */
        if (bptree_insert(index_set->id_tree, id_val, ref) != 0) {
            free(ref);
            fprintf(stderr, "Warning: Duplicate id %lld found during index build.\n",
                    id_val);
            /* 빌드는 계속 진행 (중복 id 스킵) */
            continue;
        }

        /* win tree: 기존 key가 있으면 list append, 없으면 신규 insert
         *
         * 왜 tree duplicate가 아니라 list append인가?
         * → 같은 game_win_count를 가진 row가 여러 개 있을 수 있다.
         *   B+ 트리에서는 key를 unique로 유지하고,
         *   value인 POffsetList에 여러 offset을 추가하는 방식으로 중복을 관리한다. */
        existing_list = (POffsetList *)bptree_search(index_set->win_tree, win_val);
        if (existing_list != NULL) {
            if (offset_list_append(existing_list, table->offsets[i]) != 0) {
                player_index_free(index_set);
                return FAILURE;
            }
        } else {
            new_list = create_offset_list(table->offsets[i]);
            if (new_list == NULL) {
                player_index_free(index_set);
                return FAILURE;
            }
            if (bptree_insert(index_set->win_tree, win_val, new_list) != 0) {
                free(new_list->head);
                free(new_list);
                player_index_free(index_set);
                return FAILURE;
            }
        }
    }

    return SUCCESS;
}

/* ─────────────────────────────────────────────────────────
 * 함수명: player_index_search_by_id
 * ─────────────────────────────────────────────────────────
 * 기능: id B+ 트리에서 exact match 검색.
 *       root부터 leaf까지 내려가 해당 id의 RowRef*를 반환한다.
 * ───────────────────────────────────────────────────────── */
RowRef *player_index_search_by_id(const PlayerIndexSet *index_set, long long id) {
    if (index_set == NULL || index_set->id_tree == NULL) {
        return NULL;
    }
    return (RowRef *)bptree_search(index_set->id_tree, id);
}

/* ─────────────────────────────────────────────────────────
 * 함수명: player_index_search_by_win_count
 * ─────────────────────────────────────────────────────────
 * 기능: game_win_count B+ 트리에서 exact match 검색.
 *       해당 승리 횟수를 가진 모든 row의 offset 목록(POffsetList*)을 반환한다.
 * ───────────────────────────────────────────────────────── */
POffsetList *player_index_search_by_win_count(const PlayerIndexSet *index_set,
                                              long long win_count) {
    if (index_set == NULL || index_set->win_tree == NULL) {
        return NULL;
    }
    return (POffsetList *)bptree_search(index_set->win_tree, win_count);
}

/* ─────────────────────────────────────────────────────────
 * 함수명: player_index_insert_row
 * ─────────────────────────────────────────────────────────
 * 기능: 새 row의 id, game_win_count, offset을 두 tree에 반영한다.
 *
 * 핵심 흐름:
 *   1. id tree에 RowRef 삽입 (duplicate이면 실패)
 *   2. win tree에서 해당 win_count 검색
 *      - 있으면 POffsetList에 append
 *      - 없으면 새 POffsetList 생성 후 insert
 * ───────────────────────────────────────────────────────── */
int player_index_insert_row(PlayerIndexSet *index_set, long long id,
                            long long win_count, long offset) {
    RowRef *ref;
    POffsetList *existing_list;
    POffsetList *new_list;

    if (index_set == NULL) {
        return FAILURE;
    }

    /* id tree insert */
    ref = create_row_ref(offset);
    if (ref == NULL) {
        return FAILURE;
    }
    if (bptree_insert(index_set->id_tree, id, ref) != 0) {
        free(ref);
        fprintf(stderr, "Error: Duplicate id %lld.\n", id);
        return FAILURE;
    }

    /* win tree insert or append */
    existing_list = (POffsetList *)bptree_search(index_set->win_tree, win_count);
    if (existing_list != NULL) {
        if (offset_list_append(existing_list, offset) != 0) {
            return FAILURE;
        }
    } else {
        new_list = create_offset_list(offset);
        if (new_list == NULL) {
            return FAILURE;
        }
        if (bptree_insert(index_set->win_tree, win_count, new_list) != 0) {
            free(new_list->head);
            free(new_list);
            return FAILURE;
        }
    }

    return SUCCESS;
}

/* ─────────────────────────────────────────────────────────
 * 함수명: player_index_free
 * ─────────────────────────────────────────────────────────
 * 기능: tree 노드뿐 아니라 RowRef, POffsetList, OffsetNode도 모두 해제한다.
 * ───────────────────────────────────────────────────────── */
void player_index_free(PlayerIndexSet *index_set) {
    if (index_set == NULL) {
        return;
    }

    if (index_set->id_tree != NULL) {
        free_id_tree_values(index_set->id_tree);
        bptree_destroy(index_set->id_tree);
        index_set->id_tree = NULL;
    }

    if (index_set->win_tree != NULL) {
        free_win_tree_values(index_set->win_tree);
        bptree_destroy(index_set->win_tree);
        index_set->win_tree = NULL;
    }
}
