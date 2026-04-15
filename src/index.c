#include "index.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int index_find_column_index(const char columns[][MAX_IDENTIFIER_LEN],
                                   int col_count, const char *target) {
    int i;

    for (i = 0; i < col_count; i++) {
        if (utils_equals_ignore_case(columns[i], target)) {
            return i;
        }
    }

    return FAILURE;
}

static RowRef *index_create_row_ref(long offset) {
    RowRef *ref;

    ref = (RowRef *)malloc(sizeof(RowRef));
    if (ref == NULL) {
        fprintf(stderr, "Error: Failed to allocate row reference.\n");
        return NULL;
    }

    ref->offset = offset;
    return ref;
}

static OffsetList *index_create_offset_list(void) {
    OffsetList *list;

    list = (OffsetList *)calloc(1, sizeof(OffsetList));
    if (list == NULL) {
        fprintf(stderr, "Error: Failed to allocate offset list.\n");
        return NULL;
    }

    return list;
}

/*
 * 함수명: index_offset_list_append
 * ------------------------------------------------------------
 * 기능:
 *   game_win_count key에 매핑되는 row offset 목록 뒤에 offset을 추가한다.
 *
 * 핵심 흐름:
 *   1. 새 linked-list node를 만든다.
 *   2. list가 비어 있으면 head/tail을 모두 새 node로 둔다.
 *   3. 이미 값이 있으면 tail 뒤에 붙인다.
 *
 * 개념:
 *   game_win_count는 여러 player가 같은 값을 가질 수 있다.
 *   그래서 B+ 트리에 duplicate key를 넣지 않고, 하나의 key가 OffsetList를 갖는다.
 */
static int index_offset_list_append(OffsetList *list, long offset) {
    OffsetNode *node;

    if (list == NULL) {
        return FAILURE;
    }

    node = (OffsetNode *)malloc(sizeof(OffsetNode));
    if (node == NULL) {
        fprintf(stderr, "Error: Failed to allocate offset node.\n");
        return FAILURE;
    }

    node->offset = offset;
    node->next = NULL;

    if (list->tail == NULL) {
        list->head = node;
        list->tail = node;
    } else {
        list->tail->next = node;
        list->tail = node;
    }

    list->count++;
    return SUCCESS;
}

static void index_free_offset_list(OffsetList *list) {
    OffsetNode *node;
    OffsetNode *next;

    if (list == NULL) {
        return;
    }

    node = list->head;
    while (node != NULL) {
        next = node->next;
        free(node);
        node = next;
    }

    free(list);
}

static int index_insert_id(PlayerIndexSet *indexes, long long id, long offset) {
    RowRef *ref;

    ref = index_create_row_ref(offset);
    if (ref == NULL) {
        return FAILURE;
    }

    if (bptree_insert(indexes->id_tree, id, ref) != SUCCESS) {
        free(ref);
        fprintf(stderr, "Error: Duplicate id '%lld' in player id index.\n", id);
        return FAILURE;
    }

    return SUCCESS;
}

/*
 * 함수명: index_insert_win_count
 * ------------------------------------------------------------
 * 기능:
 *   game_win_count index에 win_count -> offset 관계를 추가한다.
 *
 * 핵심 흐름:
 *   1. 이미 win_count key가 있으면 기존 OffsetList에 append한다.
 *   2. key가 없으면 새 OffsetList를 만들고 B+ 트리에 insert한다.
 *
 * 개념:
 *   id는 unique이지만 game_win_count는 secondary index라 중복이 자연스럽다.
 *   B+ 트리 core는 duplicate key를 허용하지 않으므로 중복 row는 value list로 관리한다.
 */
static int index_insert_win_count(PlayerIndexSet *indexes, long long win_count,
                                  long offset) {
    OffsetList *list;

    list = (OffsetList *)bptree_search(indexes->win_tree, win_count);
    if (list != NULL) {
        return index_offset_list_append(list, offset);
    }

    list = index_create_offset_list();
    if (list == NULL) {
        return FAILURE;
    }

    if (index_offset_list_append(list, offset) != SUCCESS) {
        index_free_offset_list(list);
        return FAILURE;
    }

    if (bptree_insert(indexes->win_tree, win_count, list) != SUCCESS) {
        index_free_offset_list(list);
        return FAILURE;
    }

    return SUCCESS;
}

/*
 * 함수명: index_build_player_indexes
 * ------------------------------------------------------------
 * 기능:
 *   메모리에 로드된 TableData를 순회하여 id와 game_win_count B+ 트리를 만든다.
 *
 * 핵심 흐름:
 *   1. 테이블에서 id, game_win_count 컬럼 위치를 찾는다.
 *   2. 각 row의 key 값을 정수로 변환한다.
 *   3. id tree에는 RowRef, win tree에는 OffsetList를 채운다.
 *
 * 개념:
 *   B+ 트리는 row 전체가 아니라 CSV row offset만 저장한다.
 *   실제 row 읽기는 storage_read_row_at_offset()이 담당한다.
 */
int index_build_player_indexes(const TableData *table, PlayerIndexSet *indexes) {
    int id_index;
    int win_index;
    int i;
    long long id;
    long long win_count;

    if (table == NULL || indexes == NULL) {
        return FAILURE;
    }

    memset(indexes, 0, sizeof(*indexes));
    id_index = index_find_column_index(table->columns, table->col_count, "id");
    win_index = index_find_column_index(table->columns, table->col_count,
                                        "game_win_count");

    if (id_index != FAILURE) {
        indexes->id_tree = bptree_create(BPTREE_ORDER);
        if (indexes->id_tree == NULL) {
            index_free(indexes);
            return FAILURE;
        }
        indexes->has_id_index = 1;
    }

    if (win_index != FAILURE) {
        indexes->win_tree = bptree_create(BPTREE_ORDER);
        if (indexes->win_tree == NULL) {
            index_free(indexes);
            return FAILURE;
        }
        indexes->has_win_index = 1;
    }

    if (!indexes->has_id_index && !indexes->has_win_index) {
        return SUCCESS;
    }

    for (i = 0; i < table->row_count; i++) {
        if (table->offsets == NULL) {
            fprintf(stderr, "Error: Player indexes require row offsets.\n");
            index_free(indexes);
            return FAILURE;
        }

        if (indexes->has_id_index) {
            if (!utils_is_integer(table->rows[i][id_index])) {
                fprintf(stderr, "Error: id index requires integer id values.\n");
                index_free(indexes);
                return FAILURE;
            }
            id = utils_parse_integer(table->rows[i][id_index]);
            if (index_insert_id(indexes, id, table->offsets[i]) != SUCCESS) {
                index_free(indexes);
                return FAILURE;
            }
        }

        if (indexes->has_win_index) {
            if (!utils_is_integer(table->rows[i][win_index])) {
                fprintf(stderr,
                        "Error: game_win_count index requires integer values.\n");
                index_free(indexes);
                return FAILURE;
            }
            win_count = utils_parse_integer(table->rows[i][win_index]);
            if (index_insert_win_count(indexes, win_count, table->offsets[i]) != SUCCESS) {
                index_free(indexes);
                return FAILURE;
            }
        }
    }

    return SUCCESS;
}

RowRef *index_search_by_id(PlayerIndexSet *indexes, long long id) {
    if (indexes == NULL || !indexes->has_id_index || indexes->id_tree == NULL) {
        return NULL;
    }

    return (RowRef *)bptree_search(indexes->id_tree, id);
}

OffsetList *index_search_by_win_count(PlayerIndexSet *indexes,
                                      long long win_count) {
    if (indexes == NULL || !indexes->has_win_index || indexes->win_tree == NULL) {
        return NULL;
    }

    return (OffsetList *)bptree_search(indexes->win_tree, win_count);
}

/*
 * 함수명: index_insert_row
 * ------------------------------------------------------------
 * 기능:
 *   INSERT로 새로 생긴 row를 이미 캐시된 player index set에 반영한다.
 *
 * 핵심 흐름:
 *   id tree가 있으면 unique id -> RowRef를 추가한다.
 *   win tree가 있으면 game_win_count -> OffsetList에 offset을 추가한다.
 *
 * 개념:
 *   캐시가 없을 때는 굳이 즉시 build하지 않고 다음 SELECT에서 lazy build한다.
 */
int index_insert_row(PlayerIndexSet *indexes, long long id,
                     long long win_count, long offset) {
    if (indexes == NULL) {
        return FAILURE;
    }

    if (indexes->has_id_index && indexes->id_tree != NULL) {
        if (index_insert_id(indexes, id, offset) != SUCCESS) {
            return FAILURE;
        }
    }

    if (indexes->has_win_index && indexes->win_tree != NULL) {
        if (index_insert_win_count(indexes, win_count, offset) != SUCCESS) {
            return FAILURE;
        }
    }

    return SUCCESS;
}

static void index_free_tree_values(BPTree *tree, int free_offset_lists) {
    BPTreeNode *leaf;
    int i;

    if (tree == NULL || tree->root == NULL) {
        return;
    }

    leaf = tree->root;
    while (leaf->type == BPTREE_INTERNAL) {
        leaf = leaf->children[0];
    }

    while (leaf != NULL) {
        for (i = 0; i < leaf->num_keys; i++) {
            if (free_offset_lists) {
                index_free_offset_list((OffsetList *)leaf->values[i]);
            } else {
                free(leaf->values[i]);
            }
            leaf->values[i] = NULL;
        }
        leaf = leaf->next;
    }
}

void index_free(PlayerIndexSet *indexes) {
    if (indexes == NULL) {
        return;
    }

    if (indexes->id_tree != NULL) {
        index_free_tree_values(indexes->id_tree, 0);
        bptree_destroy(indexes->id_tree);
    }

    if (indexes->win_tree != NULL) {
        index_free_tree_values(indexes->win_tree, 1);
        bptree_destroy(indexes->win_tree);
    }

    memset(indexes, 0, sizeof(*indexes));
}
