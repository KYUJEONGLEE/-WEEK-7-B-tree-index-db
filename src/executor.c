#include "executor.h"

#include "index.h"
#include "storage.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define EXECUTOR_TABLE_CACHE_LIMIT 8
#define EXECUTOR_INDEX_CACHE_LIMIT 8

typedef struct {
    int in_use;
    unsigned long last_used_tick;
    char table_name[MAX_IDENTIFIER_LEN];
    TableData table;
} ExecutorTableCacheEntry;

typedef struct {
    int in_use;
    unsigned long last_used_tick;
    char table_name[MAX_IDENTIFIER_LEN];
    PlayerIndexSet indexes;
} ExecutorIndexCacheEntry;

static ExecutorTableCacheEntry executor_table_cache[EXECUTOR_TABLE_CACHE_LIMIT];
static ExecutorIndexCacheEntry executor_index_cache[EXECUTOR_INDEX_CACHE_LIMIT];
static unsigned long executor_cache_tick = 0;
static int executor_table_cache_hit_count = 0;
static int executor_index_cache_hit_count = 0;

static void executor_touch_cache(unsigned long *last_used_tick) {
    executor_cache_tick++;
    *last_used_tick = executor_cache_tick;
}

static int executor_find_column_index(const char columns[][MAX_IDENTIFIER_LEN],
                                      int col_count, const char *target) {
    int i;

    for (i = 0; i < col_count; i++) {
        if (utils_equals_ignore_case(columns[i], target)) {
            return i;
        }
    }

    return FAILURE;
}

static void executor_clear_table_cache_entry(ExecutorTableCacheEntry *entry) {
    if (entry == NULL || !entry->in_use) {
        return;
    }

    storage_free_table(&entry->table);
    memset(entry, 0, sizeof(*entry));
}

static void executor_clear_index_cache_entry(ExecutorIndexCacheEntry *entry) {
    if (entry == NULL || !entry->in_use) {
        return;
    }

    index_free(&entry->indexes);
    memset(entry, 0, sizeof(*entry));
}

static int executor_choose_table_cache_slot(void) {
    int i;
    int candidate;

    candidate = 0;
    for (i = 0; i < EXECUTOR_TABLE_CACHE_LIMIT; i++) {
        if (!executor_table_cache[i].in_use) {
            return i;
        }

        if (executor_table_cache[i].last_used_tick <
            executor_table_cache[candidate].last_used_tick) {
            candidate = i;
        }
    }

    return candidate;
}

static int executor_choose_index_cache_slot(void) {
    int i;
    int candidate;

    candidate = 0;
    for (i = 0; i < EXECUTOR_INDEX_CACHE_LIMIT; i++) {
        if (!executor_index_cache[i].in_use) {
            return i;
        }

        if (executor_index_cache[i].last_used_tick <
            executor_index_cache[candidate].last_used_tick) {
            candidate = i;
        }
    }

    return candidate;
}

/*
 * 함수명: executor_invalidate_table_cache
 * ------------------------------------------------------------
 * 기능:
 *   INSERT/DELETE 이후 특정 테이블의 table cache와 player index cache를 지운다.
 *
 * 개념:
 *   DELETE는 CSV를 재작성할 수 있어 row offset이 바뀐다.
 *   따라서 B+ Tree를 직접 수정하지 않고 cache invalidate 후 다음 조회 때 rebuild한다.
 */
static void executor_invalidate_table_cache(const char *table_name) {
    int i;

    if (table_name == NULL) {
        return;
    }

    for (i = 0; i < EXECUTOR_TABLE_CACHE_LIMIT; i++) {
        if (executor_table_cache[i].in_use &&
            utils_equals_ignore_case(executor_table_cache[i].table_name,
                                     table_name)) {
            executor_clear_table_cache_entry(&executor_table_cache[i]);
        }
    }

    for (i = 0; i < EXECUTOR_INDEX_CACHE_LIMIT; i++) {
        if (executor_index_cache[i].in_use &&
            utils_equals_ignore_case(executor_index_cache[i].table_name,
                                     table_name)) {
            executor_clear_index_cache_entry(&executor_index_cache[i]);
        }
    }
}

static int executor_get_cached_table(const char *table_name,
                                     const TableData **out_table) {
    int i;
    int slot;

    if (table_name == NULL || out_table == NULL) {
        return FAILURE;
    }

    for (i = 0; i < EXECUTOR_TABLE_CACHE_LIMIT; i++) {
        if (executor_table_cache[i].in_use &&
            utils_equals_ignore_case(executor_table_cache[i].table_name,
                                     table_name)) {
            executor_touch_cache(&executor_table_cache[i].last_used_tick);
            executor_table_cache_hit_count++;
            *out_table = &executor_table_cache[i].table;
            return SUCCESS;
        }
    }

    slot = executor_choose_table_cache_slot();
    executor_clear_table_cache_entry(&executor_table_cache[slot]);
    if (storage_load_table(table_name, &executor_table_cache[slot].table) != SUCCESS) {
        return FAILURE;
    }

    if (utils_safe_strcpy(executor_table_cache[slot].table_name,
                          sizeof(executor_table_cache[slot].table_name),
                          table_name) != SUCCESS) {
        executor_clear_table_cache_entry(&executor_table_cache[slot]);
        return FAILURE;
    }

    executor_table_cache[slot].in_use = 1;
    executor_touch_cache(&executor_table_cache[slot].last_used_tick);
    *out_table = &executor_table_cache[slot].table;
    return SUCCESS;
}

/*
 * 함수명: executor_get_cached_player_indexes
 * ------------------------------------------------------------
 * 기능:
 *   테이블별 id/game_win_count B+ Tree index set을 가져오거나 새로 만든다.
 *
 * 핵심 흐름:
 *   1. 같은 table_name의 cache가 있으면 재사용한다.
 *   2. 없으면 현재 TableData의 row offset으로 B+ Tree 두 개를 build한다.
 *
 * 개념:
 *   B+ Tree는 id/game_win_count를 row offset으로 바꾸는 지도이다.
 */
static int executor_get_cached_player_indexes(const char *table_name,
                                              const TableData *table,
                                              PlayerIndexSet **out_indexes) {
    int i;
    int slot;

    if (table_name == NULL || table == NULL || out_indexes == NULL) {
        return FAILURE;
    }

    for (i = 0; i < EXECUTOR_INDEX_CACHE_LIMIT; i++) {
        if (executor_index_cache[i].in_use &&
            utils_equals_ignore_case(executor_index_cache[i].table_name,
                                     table_name)) {
            executor_touch_cache(&executor_index_cache[i].last_used_tick);
            executor_index_cache_hit_count++;
            *out_indexes = &executor_index_cache[i].indexes;
            return SUCCESS;
        }
    }

    slot = executor_choose_index_cache_slot();
    executor_clear_index_cache_entry(&executor_index_cache[slot]);
    if (index_build_player_indexes(table,
                                   &executor_index_cache[slot].indexes) != SUCCESS) {
        return FAILURE;
    }

    if (utils_safe_strcpy(executor_index_cache[slot].table_name,
                          sizeof(executor_index_cache[slot].table_name),
                          table_name) != SUCCESS) {
        executor_clear_index_cache_entry(&executor_index_cache[slot]);
        return FAILURE;
    }

    executor_index_cache[slot].in_use = 1;
    executor_touch_cache(&executor_index_cache[slot].last_used_tick);
    *out_indexes = &executor_index_cache[slot].indexes;
    return SUCCESS;
}

static char *executor_duplicate_cell(const char *value) {
    return utils_strdup(value == NULL ? "" : value);
}

static int executor_allocate_result_rows(char ****rows, int row_count) {
    if (rows == NULL) {
        return FAILURE;
    }

    if (row_count <= 0) {
        *rows = NULL;
        return SUCCESS;
    }

    *rows = (char ***)malloc((size_t)row_count * sizeof(char **));
    if (*rows == NULL) {
        fprintf(stderr, "Error: Failed to allocate memory.\n");
        return FAILURE;
    }

    return SUCCESS;
}

static int executor_copy_projected_row(char ***result_rows, int result_index,
                                       char **source_row,
                                       const int *selected_indices,
                                       int selected_count) {
    int i;
    int j;

    result_rows[result_index] =
        (char **)malloc((size_t)selected_count * sizeof(char *));
    if (result_rows[result_index] == NULL) {
        fprintf(stderr, "Error: Failed to allocate memory.\n");
        return FAILURE;
    }

    for (i = 0; i < selected_count; i++) {
        result_rows[result_index][i] =
            executor_duplicate_cell(source_row[selected_indices[i]]);
        if (result_rows[result_index][i] == NULL) {
            for (j = 0; j < i; j++) {
                free(result_rows[result_index][j]);
            }
            free(result_rows[result_index]);
            result_rows[result_index] = NULL;
            return FAILURE;
        }
    }

    return SUCCESS;
}

static void executor_free_result_rows(char ***rows, int row_count, int col_count) {
    storage_free_rows(rows, row_count, col_count);
}

static void executor_print_border(const int *widths, int col_count) {
    int i;
    int j;

    for (i = 0; i < col_count; i++) {
        putchar('+');
        for (j = 0; j < widths[i] + 2; j++) {
            putchar('-');
        }
    }
    puts("+");
}

static void executor_print_table(char headers[][MAX_IDENTIFIER_LEN], int header_count,
                                 char ***rows, int row_count) {
    int widths[MAX_COLUMNS];
    int i;
    int j;
    int cell_width;

    for (i = 0; i < header_count; i++) {
        widths[i] = utils_display_width(headers[i]);
    }

    for (i = 0; i < row_count; i++) {
        for (j = 0; j < header_count; j++) {
            cell_width = utils_display_width(rows[i][j]);
            if (cell_width > widths[j]) {
                widths[j] = cell_width;
            }
        }
    }

    executor_print_border(widths, header_count);
    for (i = 0; i < header_count; i++) {
        printf("| ");
        utils_print_padded(stdout, headers[i], widths[i]);
        putchar(' ');
    }
    puts("|");
    executor_print_border(widths, header_count);

    for (i = 0; i < row_count; i++) {
        for (j = 0; j < header_count; j++) {
            printf("| ");
            utils_print_padded(stdout, rows[i][j], widths[j]);
            putchar(' ');
        }
        puts("|");
    }

    executor_print_border(widths, header_count);
}

static int executor_prepare_projection(const SelectStatement *stmt,
                                       const TableData *table,
                                       int selected_indices[],
                                       char headers[][MAX_IDENTIFIER_LEN],
                                       int *selected_count) {
    int i;
    int column_index;

    if (stmt == NULL || table == NULL || selected_indices == NULL ||
        headers == NULL || selected_count == NULL) {
        return FAILURE;
    }

    if (stmt->column_count == 0) {
        for (i = 0; i < table->col_count; i++) {
            selected_indices[i] = i;
            if (utils_safe_strcpy(headers[i], sizeof(headers[i]),
                                  table->columns[i]) != SUCCESS) {
                return FAILURE;
            }
        }
        *selected_count = table->col_count;
        return SUCCESS;
    }

    for (i = 0; i < stmt->column_count; i++) {
        column_index = executor_find_column_index(table->columns, table->col_count,
                                                  stmt->columns[i]);
        if (column_index == FAILURE) {
            fprintf(stderr, "Error: Column '%s' not found.\n", stmt->columns[i]);
            return FAILURE;
        }

        selected_indices[i] = column_index;
        if (utils_safe_strcpy(headers[i], sizeof(headers[i]),
                              table->columns[column_index]) != SUCCESS) {
            return FAILURE;
        }
    }

    *selected_count = stmt->column_count;
    return SUCCESS;
}

static int executor_compare_with_operator(const char *lhs, const char *op,
                                          const char *rhs) {
    int comparison;

    comparison = utils_compare_values(lhs, rhs);
    if (strcmp(op, "=") == 0) {
        return comparison == 0;
    }
    if (strcmp(op, "!=") == 0) {
        return comparison != 0;
    }
    if (strcmp(op, ">") == 0) {
        return comparison > 0;
    }
    if (strcmp(op, "<") == 0) {
        return comparison < 0;
    }
    if (strcmp(op, ">=") == 0) {
        return comparison >= 0;
    }
    if (strcmp(op, "<=") == 0) {
        return comparison <= 0;
    }

    fprintf(stderr, "Error: Unsupported WHERE operator '%s'.\n", op);
    return FAILURE;
}

static int executor_collect_all_rows(const TableData *table,
                                     const int *selected_indices,
                                     int selected_count,
                                     char ****out_rows,
                                     int *out_row_count) {
    int i;
    char ***result_rows;

    if (executor_allocate_result_rows(&result_rows, table->row_count) != SUCCESS) {
        return FAILURE;
    }

    for (i = 0; i < table->row_count; i++) {
        if (executor_copy_projected_row(result_rows, i, table->rows[i],
                                        selected_indices, selected_count) != SUCCESS) {
            executor_free_result_rows(result_rows, i, selected_count);
            return FAILURE;
        }
    }

    *out_rows = result_rows;
    *out_row_count = table->row_count;
    return SUCCESS;
}

/*
 * 함수명: executor_collect_linear_rows
 * ------------------------------------------------------------
 * 기능:
 *   인덱스 대상이 아닌 WHERE 조건을 처음 row부터 끝 row까지 비교한다.
 *
 * 개념:
 *   nickname, game_loss_count, total_game_count는 의도적으로 linear scan을 쓴다.
 *   benchmark에서 B+ Tree 경로와 비교하기 위한 기준 경로이다.
 */
static int executor_collect_linear_rows(const SelectStatement *stmt,
                                        const TableData *table,
                                        const int *selected_indices,
                                        int selected_count,
                                        char ****out_rows,
                                        int *out_row_count,
                                        long *scanned_rows) {
    int where_index;
    int i;
    int matches;
    int result_count;
    char ***result_rows;

    where_index = executor_find_column_index(table->columns, table->col_count,
                                             stmt->where.column);
    if (where_index == FAILURE) {
        fprintf(stderr, "Error: Column '%s' not found.\n", stmt->where.column);
        return FAILURE;
    }

    if (executor_allocate_result_rows(&result_rows, table->row_count) != SUCCESS) {
        return FAILURE;
    }

    result_count = 0;
    for (i = 0; i < table->row_count; i++) {
        matches = executor_compare_with_operator(table->rows[i][where_index],
                                                 stmt->where.op,
                                                 stmt->where.value);
        if (matches == FAILURE) {
            executor_free_result_rows(result_rows, result_count, selected_count);
            return FAILURE;
        }

        if (matches) {
            if (executor_copy_projected_row(result_rows, result_count, table->rows[i],
                                            selected_indices,
                                            selected_count) != SUCCESS) {
                executor_free_result_rows(result_rows, result_count, selected_count);
                return FAILURE;
            }
            result_count++;
        }
    }

    if (scanned_rows != NULL) {
        *scanned_rows = table->row_count;
    }
    *out_rows = result_rows;
    *out_row_count = result_count;
    return SUCCESS;
}

static int executor_collect_single_offset_row(const char *table_name,
                                             long offset,
                                             int table_col_count,
                                             const int *selected_indices,
                                             int selected_count,
                                             char ****out_rows,
                                             int *out_row_count) {
    char ***result_rows;
    char **full_row;

    if (executor_allocate_result_rows(&result_rows, 1) != SUCCESS) {
        return FAILURE;
    }

    if (storage_read_row_at_offset(table_name, offset, table_col_count,
                                   &full_row) != SUCCESS) {
        free(result_rows);
        return FAILURE;
    }

    if (executor_copy_projected_row(result_rows, 0, full_row,
                                    selected_indices, selected_count) != SUCCESS) {
        storage_free_row(full_row, table_col_count);
        free(result_rows);
        return FAILURE;
    }

    storage_free_row(full_row, table_col_count);
    *out_rows = result_rows;
    *out_row_count = 1;
    return SUCCESS;
}

static int executor_collect_id_index_rows(const SelectStatement *stmt,
                                          const TableData *table,
                                          PlayerIndexSet *indexes,
                                          const int *selected_indices,
                                          int selected_count,
                                          char ****out_rows,
                                          int *out_row_count,
                                          long *scanned_rows) {
    RowRef *ref;
    long long id;

    id = utils_parse_integer(stmt->where.value);
    ref = index_search_by_id(indexes, id);
    if (ref == NULL) {
        *out_rows = NULL;
        *out_row_count = 0;
        if (scanned_rows != NULL) {
            *scanned_rows = 0;
        }
        return SUCCESS;
    }

    if (scanned_rows != NULL) {
        *scanned_rows = 1;
    }
    return executor_collect_single_offset_row(stmt->table_name, ref->offset,
                                              table->col_count,
                                              selected_indices, selected_count,
                                              out_rows, out_row_count);
}

static int executor_collect_win_index_rows(const SelectStatement *stmt,
                                           const TableData *table,
                                           PlayerIndexSet *indexes,
                                           const int *selected_indices,
                                           int selected_count,
                                           char ****out_rows,
                                           int *out_row_count,
                                           long *scanned_rows) {
    OffsetList *list;
    OffsetNode *node;
    char ***result_rows;
    char **full_row;
    long long win_count;
    int result_count;

    win_count = utils_parse_integer(stmt->where.value);
    list = index_search_by_win_count(indexes, win_count);
    if (list == NULL || list->count <= 0) {
        *out_rows = NULL;
        *out_row_count = 0;
        if (scanned_rows != NULL) {
            *scanned_rows = 0;
        }
        return SUCCESS;
    }

    if (list->count > INT_MAX) {
        return FAILURE;
    }

    if (executor_allocate_result_rows(&result_rows, (int)list->count) != SUCCESS) {
        return FAILURE;
    }

    result_count = 0;
    node = list->head;
    while (node != NULL) {
        if (storage_read_row_at_offset(stmt->table_name, node->offset,
                                       table->col_count, &full_row) != SUCCESS) {
            executor_free_result_rows(result_rows, result_count, selected_count);
            return FAILURE;
        }

        if (executor_copy_projected_row(result_rows, result_count, full_row,
                                        selected_indices, selected_count) != SUCCESS) {
            storage_free_row(full_row, table->col_count);
            executor_free_result_rows(result_rows, result_count, selected_count);
            return FAILURE;
        }

        storage_free_row(full_row, table->col_count);
        result_count++;
        node = node->next;
    }

    if (scanned_rows != NULL) {
        *scanned_rows = list->count;
    }
    *out_rows = result_rows;
    *out_row_count = result_count;
    return SUCCESS;
}

/*
 * 함수명: executor_choose_plan
 * ------------------------------------------------------------
 * 기능:
 *   SELECT WHERE 조건과 benchmark mode를 보고 실행 계획을 고른다.
 *
 * 개념:
 *   명세에 따라 id와 game_win_count의 equality 조건만 B+ Tree로 보낸다.
 *   나머지 WHERE 조건은 성능 비교를 위해 선형 탐색으로 유지한다.
 */
static int executor_choose_plan(const SelectStatement *stmt,
                                const TableData *table,
                                ExecMode mode,
                                ExecPlan *plan) {
    int is_id_lookup;
    int is_win_lookup;

    if (!stmt->has_where) {
        *plan = EXEC_PLAN_FULL_SCAN;
        return SUCCESS;
    }

    is_id_lookup = utils_equals_ignore_case(stmt->where.column, "id") &&
                   strcmp(stmt->where.op, "=") == 0 &&
                   utils_is_integer(stmt->where.value) &&
                   executor_find_column_index(table->columns, table->col_count,
                                              "id") != FAILURE;
    is_win_lookup = utils_equals_ignore_case(stmt->where.column,
                                             "game_win_count") &&
                    strcmp(stmt->where.op, "=") == 0 &&
                    utils_is_integer(stmt->where.value) &&
                    executor_find_column_index(table->columns, table->col_count,
                                               "game_win_count") != FAILURE;

    if (mode == EXEC_MODE_FORCE_LINEAR) {
        *plan = EXEC_PLAN_LINEAR_SCAN;
        return SUCCESS;
    }

    if (mode == EXEC_MODE_FORCE_ID_INDEX) {
        if (!is_id_lookup) {
            fprintf(stderr, "Error: FORCE_ID_INDEX requires WHERE id = integer.\n");
            return FAILURE;
        }
        *plan = EXEC_PLAN_BPTREE_ID_LOOKUP;
        return SUCCESS;
    }

    if (mode == EXEC_MODE_FORCE_WIN_INDEX) {
        if (!is_win_lookup) {
            fprintf(stderr,
                    "Error: FORCE_WIN_INDEX requires WHERE game_win_count = integer.\n");
            return FAILURE;
        }
        *plan = EXEC_PLAN_BPTREE_WIN_LOOKUP;
        return SUCCESS;
    }

    if (is_id_lookup) {
        *plan = EXEC_PLAN_BPTREE_ID_LOOKUP;
    } else if (is_win_lookup) {
        *plan = EXEC_PLAN_BPTREE_WIN_LOOKUP;
    } else {
        *plan = EXEC_PLAN_LINEAR_SCAN;
    }

    return SUCCESS;
}

int executor_execute_select_with_options(const SelectStatement *stmt,
                                         ExecMode mode, int silent,
                                         ExecStats *stats) {
    const TableData *table;
    PlayerIndexSet *indexes;
    int selected_indices[MAX_COLUMNS];
    char headers[MAX_COLUMNS][MAX_IDENTIFIER_LEN];
    int selected_count;
    char ***result_rows;
    int result_row_count;
    ExecPlan plan;
    long scanned_rows;
    int status;
    clock_t started;
    clock_t ended;

    if (stats != NULL) {
        memset(stats, 0, sizeof(*stats));
    }

    if (stmt == NULL) {
        return FAILURE;
    }

    if (executor_get_cached_table(stmt->table_name, &table) != SUCCESS) {
        return FAILURE;
    }

    if (executor_prepare_projection(stmt, table, selected_indices, headers,
                                    &selected_count) != SUCCESS) {
        return FAILURE;
    }

    if (executor_choose_plan(stmt, table, mode, &plan) != SUCCESS) {
        return FAILURE;
    }

    result_rows = NULL;
    result_row_count = 0;
    scanned_rows = 0;
    started = clock();

    if (plan == EXEC_PLAN_FULL_SCAN) {
        status = executor_collect_all_rows(table, selected_indices, selected_count,
                                           &result_rows, &result_row_count);
        scanned_rows = table->row_count;
    } else if (plan == EXEC_PLAN_LINEAR_SCAN) {
        status = executor_collect_linear_rows(stmt, table, selected_indices,
                                              selected_count, &result_rows,
                                              &result_row_count, &scanned_rows);
    } else {
        if (executor_get_cached_player_indexes(stmt->table_name, table,
                                               &indexes) != SUCCESS) {
            return FAILURE;
        }

        if (plan == EXEC_PLAN_BPTREE_ID_LOOKUP) {
            status = executor_collect_id_index_rows(stmt, table, indexes,
                                                    selected_indices,
                                                    selected_count, &result_rows,
                                                    &result_row_count,
                                                    &scanned_rows);
        } else {
            status = executor_collect_win_index_rows(stmt, table, indexes,
                                                     selected_indices,
                                                     selected_count, &result_rows,
                                                     &result_row_count,
                                                     &scanned_rows);
        }
    }

    ended = clock();
    if (status != SUCCESS) {
        return FAILURE;
    }

    if (!silent) {
        executor_print_table(headers, selected_count, result_rows, result_row_count);
        printf("%d row%s selected.\n", result_row_count,
               result_row_count == 1 ? "" : "s");
    }

    if (stats != NULL) {
        stats->plan_used = plan;
        stats->matched_rows = result_row_count;
        stats->scanned_rows = scanned_rows;
        stats->elapsed_ms =
            ((double)(ended - started) * 1000.0) / (double)CLOCKS_PER_SEC;
    }

    executor_free_result_rows(result_rows, result_row_count, selected_count);
    return SUCCESS;
}

static int executor_execute_insert(const InsertStatement *stmt) {
    StorageInsertResult result;

    if (stmt == NULL) {
        return FAILURE;
    }

    if (storage_insert_with_result(stmt->table_name, stmt, &result) != SUCCESS) {
        return FAILURE;
    }

    executor_invalidate_table_cache(stmt->table_name);
    printf("1 row inserted into %s.\n", stmt->table_name);
    return SUCCESS;
}

static int executor_execute_select(const SelectStatement *stmt) {
    return executor_execute_select_with_options(stmt, EXEC_MODE_NORMAL, 0, NULL);
}

static int executor_execute_delete(const DeleteStatement *stmt) {
    int deleted_count;

    if (stmt == NULL) {
        return FAILURE;
    }

    deleted_count = 0;
    if (storage_delete(stmt->table_name, stmt, &deleted_count) != SUCCESS) {
        return FAILURE;
    }

    executor_invalidate_table_cache(stmt->table_name);
    printf("%d row%s deleted from %s.\n", deleted_count,
           deleted_count == 1 ? "" : "s", stmt->table_name);
    return SUCCESS;
}

int executor_execute(const SqlStatement *statement) {
    if (statement == NULL) {
        return FAILURE;
    }

    switch (statement->type) {
        case SQL_INSERT:
            return executor_execute_insert(&statement->insert);
        case SQL_SELECT:
            return executor_execute_select(&statement->select);
        case SQL_DELETE:
            return executor_execute_delete(&statement->delete_stmt);
        default:
            fprintf(stderr, "Error: Unsupported SQL statement type.\n");
            return FAILURE;
    }
}

void executor_reset_runtime_state(void) {
    int i;

    for (i = 0; i < EXECUTOR_TABLE_CACHE_LIMIT; i++) {
        executor_clear_table_cache_entry(&executor_table_cache[i]);
    }

    for (i = 0; i < EXECUTOR_INDEX_CACHE_LIMIT; i++) {
        executor_clear_index_cache_entry(&executor_index_cache[i]);
    }

    executor_cache_tick = 0;
    executor_table_cache_hit_count = 0;
    executor_index_cache_hit_count = 0;
}

int executor_get_table_cache_hit_count(void) {
    return executor_table_cache_hit_count;
}

int executor_get_index_cache_hit_count(void) {
    return executor_index_cache_hit_count;
}
