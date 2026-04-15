#include "index.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int index_find_column_index(const TableData *table, const char *target) {
    int i;

    if (table == NULL || target == NULL) {
        return FAILURE;
    }

    for (i = 0; i < table->col_count; i++) {
        if (utils_equals_ignore_case(table->columns[i], target)) {
            return i;
        }
    }

    return FAILURE;
}

static RowRef *index_create_row_ref(int row_index) {
    RowRef *ref;

    ref = (RowRef *)malloc(sizeof(RowRef));
    if (ref == NULL) {
        fprintf(stderr, "Error: Failed to allocate row reference.\n");
        return NULL;
    }

    ref->row_index = row_index;
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
 * 함수명: index_append_offset
 * ----------------------------------------
 * 기능: game_win_count secondary index의 value 목록에 row index를 추가한다.
 *
 * 핵심 흐름:
 *   1. OffsetNode를 새로 만든다.
 *   2. tail 뒤에 붙여 삽입 순서를 유지한다.
 *   3. count를 증가시킨다.
 *
 * 개념:
 *   - game_win_count는 같은 값을 가진 플레이어가 여러 명일 수 있다.
 *   - B+ 트리 코어에는 duplicate key를 넣지 않고, value를 linked list로 둔다.
 */
static int index_append_offset(OffsetList *list, int row_index) {
    OffsetNode *node;

    if (list == NULL) {
        return FAILURE;
    }

    node = (OffsetNode *)malloc(sizeof(OffsetNode));
    if (node == NULL) {
        fprintf(stderr, "Error: Failed to allocate offset node.\n");
        return FAILURE;
    }

    node->row_index = row_index;
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

static void index_free_leaf_values(BPTree *tree, int free_offset_lists) {
    BPTreeNode *node;
    int i;

    if (tree == NULL || tree->root == NULL) {
        return;
    }

    node = tree->root;
    while (node->type == BPTREE_INTERNAL) {
        node = node->children[0];
    }

    while (node != NULL) {
        for (i = 0; i < node->num_keys; i++) {
            if (free_offset_lists) {
                index_free_offset_list((OffsetList *)node->values[i]);
            } else {
                free(node->values[i]);
            }
            node->values[i] = NULL;
        }
        node = node->next;
    }
}

/*
 * 함수명: index_insert_row
 * ----------------------------------------
 * 기능: 플레이어 한 행의 id와 game_win_count를 두 B+ 트리에 반영한다.
 *
 * 핵심 흐름:
 *   1. id tree에는 unique id -> RowRef(row_index)를 삽입한다.
 *   2. win tree에서 같은 game_win_count key를 찾는다.
 *   3. 있으면 기존 OffsetList에 append, 없으면 새 list를 tree에 삽입한다.
 *
 * 개념:
 *   - id는 PK 성격이라 duplicate를 허용하지 않는다.
 *   - game_win_count는 secondary index라 duplicate key를 list value로 해결한다.
 */
int index_insert_row(PlayerIndexSet *indexes, long long id,
                     long long game_win_count, int row_index) {
    RowRef *ref;
    OffsetList *win_offsets;

    if (indexes == NULL || indexes->id_tree == NULL || indexes->win_tree == NULL) {
        return FAILURE;
    }

    if (bptree_search(indexes->id_tree, id) != NULL) {
        fprintf(stderr, "Error: Duplicate id '%lld' in B+ tree index.\n", id);
        return FAILURE;
    }

    ref = index_create_row_ref(row_index);
    if (ref == NULL) {
        return FAILURE;
    }

    if (bptree_insert(indexes->id_tree, id, ref) != SUCCESS) {
        free(ref);
        return FAILURE;
    }

    win_offsets = (OffsetList *)bptree_search(indexes->win_tree, game_win_count);
    if (win_offsets != NULL) {
        return index_append_offset(win_offsets, row_index);
    }

    win_offsets = index_create_offset_list();
    if (win_offsets == NULL) {
        return FAILURE;
    }
    if (index_append_offset(win_offsets, row_index) != SUCCESS) {
        index_free_offset_list(win_offsets);
        return FAILURE;
    }
    if (bptree_insert(indexes->win_tree, game_win_count, win_offsets) != SUCCESS) {
        index_free_offset_list(win_offsets);
        return FAILURE;
    }

    return SUCCESS;
}

/*
 * 함수명: index_build_player_indexes
 * ----------------------------------------
 * 기능: TableData 전체를 읽어 players 전용 id/win_count B+ 트리를 만든다.
 *
 * 핵심 흐름:
 *   1. id, game_win_count 컬럼 위치를 찾는다.
 *   2. 각 row의 문자열 값을 정수로 검증/변환한다.
 *   3. row index를 value로 두 tree에 삽입한다.
 *
 * 주의:
 *   - 이 index manager는 players 스키마 전용이다.
 *   - nickname, game_loss_count, total_game_count는 인덱싱하지 않는다.
 */
int index_build_player_indexes(const TableData *table, PlayerIndexSet *out_indexes) {
    int id_index;
    int win_index;
    int i;
    long long id;
    long long win_count;

    if (table == NULL || out_indexes == NULL) {
        return FAILURE;
    }

    memset(out_indexes, 0, sizeof(*out_indexes));
    id_index = index_find_column_index(table, "id");
    win_index = index_find_column_index(table, "game_win_count");
    if (id_index == FAILURE || win_index == FAILURE) {
        fprintf(stderr,
                "Error: B+ tree index requires id and game_win_count columns.\n");
        return FAILURE;
    }

    out_indexes->id_tree = bptree_create(BPTREE_ORDER);
    out_indexes->win_tree = bptree_create(BPTREE_ORDER);
    if (out_indexes->id_tree == NULL || out_indexes->win_tree == NULL) {
        index_free_player_indexes(out_indexes);
        return FAILURE;
    }

    for (i = 0; i < table->row_count; i++) {
        if (!utils_is_integer(table->rows[i][id_index]) ||
            !utils_is_integer(table->rows[i][win_index])) {
            fprintf(stderr, "Error: Indexed columns must contain integer values.\n");
            index_free_player_indexes(out_indexes);
            return FAILURE;
        }

        id = utils_parse_integer(table->rows[i][id_index]);
        win_count = utils_parse_integer(table->rows[i][win_index]);
        if (index_insert_row(out_indexes, id, win_count, i) != SUCCESS) {
            index_free_player_indexes(out_indexes);
            return FAILURE;
        }
    }

    return SUCCESS;
}

int index_search_by_id(const PlayerIndexSet *indexes, long long id,
                       int *out_row_index) {
    RowRef *ref;

    if (indexes == NULL || indexes->id_tree == NULL || out_row_index == NULL) {
        return FAILURE;
    }

    ref = (RowRef *)bptree_search(indexes->id_tree, id);
    if (ref == NULL) {
        return FAILURE;
    }

    *out_row_index = ref->row_index;
    return SUCCESS;
}

const OffsetList *index_search_by_win_count(const PlayerIndexSet *indexes,
                                            long long game_win_count) {
    if (indexes == NULL || indexes->win_tree == NULL) {
        return NULL;
    }

    return (const OffsetList *)bptree_search(indexes->win_tree, game_win_count);
}

static int index_win_key_matches(long long key, const char *op, long long value) {
    if (strcmp(op, "=") == 0) {
        return key == value;
    }
    if (strcmp(op, "!=") == 0) {
        return key != value;
    }
    if (strcmp(op, ">") == 0) {
        return key > value;
    }
    if (strcmp(op, ">=") == 0) {
        return key >= value;
    }
    if (strcmp(op, "<") == 0) {
        return key < value;
    }
    if (strcmp(op, "<=") == 0) {
        return key <= value;
    }

    return 0;
}

static int index_append_result_row_index(int **items, int *count, int *capacity,
                                         int row_index) {
    int *new_items;

    if (items == NULL || count == NULL || capacity == NULL) {
        return FAILURE;
    }

    if (*count >= *capacity) {
        *capacity = *capacity == 0 ? 16 : (*capacity * 2);
        new_items = (int *)realloc(*items, (size_t)(*capacity) * sizeof(int));
        if (new_items == NULL) {
            fprintf(stderr, "Error: Failed to allocate row index result array.\n");
            return FAILURE;
        }
        *items = new_items;
    }

    (*items)[*count] = row_index;
    (*count)++;
    return SUCCESS;
}

/*
 * 함수명: index_collect_win_count_row_indexes
 * ----------------------------------------
 * 기능: game_win_count B+ 트리에서 조건에 맞는 row index들을 수집한다.
 *
 * 핵심 흐름:
 *   1. exact match는 bptree_search()로 바로 찾는다.
 *   2. 범위 조건은 가장 왼쪽 leaf부터 next 포인터를 따라 순회한다.
 *   3. 조건에 맞는 key의 OffsetList를 펼쳐 결과 row index 배열에 담는다.
 *
 * 개념:
 *   - B+ 트리는 leaf들이 연결되어 있어 범위 조건을 leaf scan으로 처리할 수 있다.
 *   - game_win_count는 중복 key가 많으므로 각 key의 OffsetList를 펼쳐야 한다.
 */
int index_collect_win_count_row_indexes(const PlayerIndexSet *indexes, const char *op,
                                        long long game_win_count,
                                        int **row_indexes, int *count) {
    BPTreeNode *leaf;
    const OffsetList *list;
    OffsetNode *offset_node;
    int capacity;
    int i;

    if (indexes == NULL || indexes->win_tree == NULL || op == NULL ||
        row_indexes == NULL || count == NULL) {
        return FAILURE;
    }

    *row_indexes = NULL;
    *count = 0;
    capacity = 0;

    if (strcmp(op, "=") == 0) {
        list = index_search_by_win_count(indexes, game_win_count);
        if (list == NULL) {
            return SUCCESS;
        }

        offset_node = list->head;
        while (offset_node != NULL) {
            if (index_append_result_row_index(row_indexes, count, &capacity,
                                              offset_node->row_index) != SUCCESS) {
                free(*row_indexes);
                *row_indexes = NULL;
                *count = 0;
                return FAILURE;
            }
            offset_node = offset_node->next;
        }
        return SUCCESS;
    }

    leaf = indexes->win_tree->root;
    if (leaf == NULL) {
        return SUCCESS;
    }

    while (leaf->type == BPTREE_INTERNAL) {
        leaf = leaf->children[0];
    }

    while (leaf != NULL) {
        for (i = 0; i < leaf->num_keys; i++) {
            if (!index_win_key_matches(leaf->keys[i], op, game_win_count)) {
                continue;
            }

            list = (const OffsetList *)leaf->values[i];
            offset_node = list == NULL ? NULL : list->head;
            while (offset_node != NULL) {
                if (index_append_result_row_index(row_indexes, count, &capacity,
                                                  offset_node->row_index) != SUCCESS) {
                    free(*row_indexes);
                    *row_indexes = NULL;
                    *count = 0;
                    return FAILURE;
                }
                offset_node = offset_node->next;
            }
        }

        if ((strcmp(op, "<") == 0 || strcmp(op, "<=") == 0) &&
            leaf->num_keys > 0 && leaf->keys[leaf->num_keys - 1] > game_win_count) {
            break;
        }

        leaf = leaf->next;
    }

    return SUCCESS;
}

void index_free_player_indexes(PlayerIndexSet *indexes) {
    if (indexes == NULL) {
        return;
    }

    if (indexes->id_tree != NULL) {
        index_free_leaf_values(indexes->id_tree, 0);
        bptree_destroy(indexes->id_tree);
        indexes->id_tree = NULL;
    }

    if (indexes->win_tree != NULL) {
        index_free_leaf_values(indexes->win_tree, 1);
        bptree_destroy(indexes->win_tree);
        indexes->win_tree = NULL;
    }
}
