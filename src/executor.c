#include "executor.h"

#include "index.h"
#include "storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define EXECUTOR_TABLE_CACHE_LIMIT 8
#define EXECUTOR_INDEX_CACHE_LIMIT 16

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
static int executor_silent_output = 0;
static int executor_summary_only = 0;
static ExecMode executor_default_mode = EXEC_MODE_NORMAL;

static double executor_now_ms(void) {
    return ((double)clock() * 1000.0) / (double)CLOCKS_PER_SEC;
}

void executor_set_silent(int silent) {
    executor_silent_output = silent ? 1 : 0;
}

void executor_set_mode(ExecMode mode) {
    executor_default_mode = mode;
}

void executor_set_summary_only(int summary_only) {
    executor_summary_only = summary_only ? 1 : 0;
}

static const char *executor_plan_name(ExecPlan plan) {
    switch (plan) {
        case EXEC_PLAN_FULL_SCAN:
            return "전체 조회";
        case EXEC_PLAN_LINEAR_SCAN:
            return "선형 탐색";
        case EXEC_PLAN_BPTREE_ID_LOOKUP:
            return "ID B+트리 조회";
        case EXEC_PLAN_BPTREE_WIN_LOOKUP:
            return "승리 횟수 B+트리 조회";
        default:
            return "없음";
    }
}

static const char *executor_plan_description(ExecPlan plan) {
    switch (plan) {
        case EXEC_PLAN_FULL_SCAN:
            return "WHERE 없음: 전체 테이블 출력";
        case EXEC_PLAN_LINEAR_SCAN:
            return "인덱스 미사용: row를 처음부터 끝까지 비교";
        case EXEC_PLAN_BPTREE_ID_LOOKUP:
            return "B+ 트리 사용: id -> row index";
        case EXEC_PLAN_BPTREE_WIN_LOOKUP:
            return "B+ 트리 사용: game_win_count -> row index list";
        default:
            return "실행 계획 없음";
    }
}

/*
 * 캐시 엔트리를 최근 사용 시점으로 갱신한다.
 */
static void executor_touch_cache(unsigned long *last_used_tick) {
    executor_cache_tick++;
    *last_used_tick = executor_cache_tick;
}

/*
 * 메모리에 올라온 테이블 스키마에서 컬럼 이름을 대소문자 무시로 찾는다.
 * 컬럼 인덱스를 반환하고, 없으면 FAILURE를 반환한다.
 */
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

/*
 * 테이블 캐시 엔트리 하나를 비우고 소유한 메모리를 해제한다.
 */
static void executor_clear_table_cache_entry(ExecutorTableCacheEntry *entry) {
    if (entry == NULL || !entry->in_use) {
        return;
    }

    storage_free_table(&entry->table);
    memset(entry, 0, sizeof(*entry));
}

/*
 * 인덱스 캐시 엔트리 하나를 비우고 소유한 메모리를 해제한다.
 */
static void executor_clear_index_cache_entry(ExecutorIndexCacheEntry *entry) {
    if (entry == NULL || !entry->in_use) {
        return;
    }

    index_free_player_indexes(&entry->indexes);
    memset(entry, 0, sizeof(*entry));
}

/*
 * 테이블 캐시에 새 엔트리를 둘 슬롯을 고른다.
 * 비어 있는 슬롯이 우선이며, 없으면 가장 오래 사용하지 않은 슬롯을 반환한다.
 */
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

/*
 * 인덱스 캐시에 새 엔트리를 둘 슬롯을 고른다.
 * 비어 있는 슬롯이 우선이며, 없으면 가장 오래 사용하지 않은 슬롯을 반환한다.
 */
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
 * 같은 테이블을 참조하는 테이블 캐시와 인덱스 캐시를 모두 무효화한다.
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

/*
 * 같은 실행 중이면 메모리의 테이블 캐시를 재사용하고,
 * 없으면 storage 계층에서 한 번 읽어 캐시에 넣는다.
 */
static int executor_get_cached_table(const char *table_name,
                                     const TableData **out_table) {
    int i;
    int slot;

    if (table_name == NULL || out_table == NULL) {
        return FAILURE;
    }

    for (i = 0; i < EXECUTOR_TABLE_CACHE_LIMIT; i++) {
        if (!executor_table_cache[i].in_use) {
            continue;
        }

        if (utils_equals_ignore_case(executor_table_cache[i].table_name,
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
        fprintf(stderr, "Error: Table name is too long.\n");
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
 * ----------------------------------------
 * 기능: players 전용 id/game_win_count B+ 트리 인덱스 세트를 가져온다.
 *
 * 핵심 흐름:
 *   1. 같은 테이블의 인덱스가 캐시에 있으면 재사용한다.
 *   2. 없으면 TableData의 id, game_win_count 컬럼으로 두 B+ 트리를 빌드한다.
 *   3. INSERT/DELETE 후에는 cache invalidate로 stale row index 문제를 피한다.
 *
 * 개념:
 *   - DELETE는 CSV를 재작성해 row index가 바뀔 수 있으므로 직접 tree delete를 하지 않는다.
 *   - 다음 조회 때 다시 빌드하는 방식이 가장 단순하고 안전하다.
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
        if (!executor_index_cache[i].in_use) {
            continue;
        }

        if (utils_equals_ignore_case(executor_index_cache[i].table_name, table_name)) {
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
        fprintf(stderr, "Error: Identifier is too long.\n");
        executor_clear_index_cache_entry(&executor_index_cache[slot]);
        return FAILURE;
    }

    executor_index_cache[slot].in_use = 1;
    executor_touch_cache(&executor_index_cache[slot].last_used_tick);
    *out_indexes = &executor_index_cache[slot].indexes;
    return SUCCESS;
}

/*
 * 결과 셀 문자열 하나를 복제한다.
 * NULL 값은 빈 문자열로 처리하며 반환된 메모리는 호출자가 소유한다.
 */
static char *executor_duplicate_cell(const char *value) {
    return utils_strdup(value == NULL ? "" : value);
}

/*
 * SELECT 결과를 담을 바깥쪽 행 배열을 할당한다.
 * 성공 시 rows에 저장하고 SUCCESS를 반환한다.
 */
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

/*
 * 원본 행에서 선택된 컬럼만 복사해 결과 행으로 만든다.
 * 새 결과 행이 모두 할당되면 SUCCESS를 반환한다.
 */
static int executor_copy_projected_row(char ***result_rows, int result_index,
                                       char **source_row,
                                       const int *selected_indices,
                                       int selected_count) {
    int i;

    result_rows[result_index] = (char **)malloc((size_t)selected_count * sizeof(char *));
    if (result_rows[result_index] == NULL) {
        fprintf(stderr, "Error: Failed to allocate memory.\n");
        return FAILURE;
    }

    for (i = 0; i < selected_count; i++) {
        result_rows[result_index][i] =
            executor_duplicate_cell(source_row[selected_indices[i]]);
        if (result_rows[result_index][i] == NULL) {
            int j;
            for (j = 0; j < i; j++) {
                free(result_rows[result_index][j]);
                result_rows[result_index][j] = NULL;
            }
            free(result_rows[result_index]);
            result_rows[result_index] = NULL;
            return FAILURE;
        }
    }

    return SUCCESS;
}

/*
 * executor 내부 헬퍼가 만든 조회 결과 테이블을 해제한다.
 */
static void executor_free_result_rows(char ***rows, int row_count, int col_count) {
    storage_free_rows(rows, row_count, col_count);
}

/*
 * SELECT 표 출력용 가로 경계선을 한 줄 출력한다.
 */
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

/*
 * 표시 폭을 고려해 MySQL 스타일 표 형태로 조회 결과를 출력한다.
 */
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

/*
 * SELECT 대상 컬럼을 원본 테이블 인덱스와 출력 헤더로 변환한다.
 * 요청된 컬럼이 모두 존재하면 SUCCESS를 반환한다.
 */
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
                fprintf(stderr, "Error: Column name is too long.\n");
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
            fprintf(stderr, "Error: Column name is too long.\n");
            return FAILURE;
        }
    }

    *selected_count = stmt->column_count;
    return SUCCESS;
}

/*
 * WHERE가 없는 SELECT를 위해 모든 행을 결과 행 배열로 복사한다.
 * 성공 시 out_rows의 소유권은 호출자에게 있다.
 */
static int executor_collect_all_rows(const TableData *table,
                                     const int *selected_indices, int selected_count,
                                     char ****out_rows, int *out_row_count) {
    int i;
    char ***result_rows;

    if (table == NULL || selected_indices == NULL || out_rows == NULL ||
        out_row_count == NULL) {
        return FAILURE;
    }

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

static int executor_compare_with_operator(const char *lhs, const char *op,
                                          const char *rhs) {
    int comparison;

    if (lhs == NULL || op == NULL || rhs == NULL) {
        return FAILURE;
    }

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

    return FAILURE;
}

/*
 * 함수명: executor_collect_linear_rows
 * ----------------------------------------
 * 기능: 인덱스를 쓰지 않고 TableData.rows를 직접 순회해 WHERE를 평가한다.
 *
 * 핵심 흐름:
 *   1. WHERE 컬럼 위치를 찾는다.
 *   2. 모든 row를 순회하면서 조건을 비교한다.
 *   3. 조건을 만족하는 row만 projection 결과로 복사한다.
 *
 * 개념:
 *   - nickname, game_loss_count, total_game_count는 의도적으로 선형 탐색이다.
 *   - benchmark에서 B+ 트리와 비교하기 위한 기준 경로가 된다.
 */
static int executor_collect_linear_rows(const SelectStatement *stmt,
                                        const TableData *table,
                                        const int *selected_indices,
                                        int selected_count,
                                        char ****out_rows, int *out_row_count,
                                        long *scanned_rows) {
    int where_index;
    int i;
    int matches;
    char ***result_rows;

    if (stmt == NULL || table == NULL || selected_indices == NULL ||
        out_rows == NULL || out_row_count == NULL) {
        return FAILURE;
    }

    where_index = executor_find_column_index(table->columns, table->col_count,
                                             stmt->where.column);
    if (where_index == FAILURE) {
        fprintf(stderr, "Error: Column '%s' not found.\n", stmt->where.column);
        return FAILURE;
    }

    if (executor_allocate_result_rows(&result_rows, table->row_count) != SUCCESS) {
        return FAILURE;
    }

    *out_row_count = 0;
    if (scanned_rows != NULL) {
        *scanned_rows = 0;
    }

    for (i = 0; i < table->row_count; i++) {
        if (scanned_rows != NULL) {
            (*scanned_rows)++;
        }

        matches = executor_compare_with_operator(table->rows[i][where_index],
                                                 stmt->where.op,
                                                 stmt->where.value);
        if (matches == FAILURE) {
            executor_free_result_rows(result_rows, *out_row_count, selected_count);
            return FAILURE;
        }

        if (!matches) {
            continue;
        }

        if (executor_copy_projected_row(result_rows, *out_row_count, table->rows[i],
                                        selected_indices, selected_count) != SUCCESS) {
            executor_free_result_rows(result_rows, *out_row_count, selected_count);
            return FAILURE;
        }
        (*out_row_count)++;
    }

    *out_rows = result_rows;
    return SUCCESS;
}

/*
 * 함수명: executor_collect_id_indexed_rows
 * ----------------------------------------
 * 기능: WHERE id = ? 조건을 id B+ 트리로 exact lookup 한다.
 *
 * 개념:
 *   - id는 unique key라 결과가 0건 또는 1건이다.
 *   - tree value는 TableData.rows의 row index라 파일을 다시 읽지 않는다.
 */
static int executor_collect_id_indexed_rows(const SelectStatement *stmt,
                                            const TableData *table,
                                            PlayerIndexSet *indexes,
                                            const int *selected_indices,
    int selected_count,
    char ****out_rows,
    int *out_row_count) {
    long long id;
    int row_index;
    char ***result_rows;

    if (stmt == NULL || table == NULL || indexes == NULL ||
        out_rows == NULL || out_row_count == NULL) {
        return FAILURE;
    }

    if (!utils_is_integer(stmt->where.value)) {
        *out_rows = NULL;
        *out_row_count = 0;
        return SUCCESS;
    }

    id = utils_parse_integer(stmt->where.value);
    if (index_search_by_id(indexes, id, &row_index) != SUCCESS) {
        *out_rows = NULL;
        *out_row_count = 0;
        return SUCCESS;
    }

    if (row_index < 0 || row_index >= table->row_count) {
        return FAILURE;
    }

    if (executor_allocate_result_rows(&result_rows, 1) != SUCCESS) {
        return FAILURE;
    }

    if (executor_copy_projected_row(result_rows, 0, table->rows[row_index],
                                    selected_indices, selected_count) != SUCCESS) {
        executor_free_result_rows(result_rows, 0, selected_count);
        return FAILURE;
    }

    *out_rows = result_rows;
    *out_row_count = 1;
    return SUCCESS;
}

/*
 * 함수명: executor_collect_win_indexed_rows
 * ----------------------------------------
 * 기능: WHERE game_win_count = ? 조건을 secondary B+ 트리로 exact lookup 한다.
 *
 * 개념:
 *   - 같은 승리 횟수를 가진 row가 여러 개일 수 있다.
 *   - B+ 트리 key는 unique이고, value OffsetList가 중복 row index를 담는다.
 */
static int executor_collect_win_indexed_rows(const SelectStatement *stmt,
                                             const TableData *table,
                                             PlayerIndexSet *indexes,
                                             const int *selected_indices,
    int selected_count,
    char ****out_rows,
    int *out_row_count) {
    long long win_count;
    int *row_indexes;
    char ***result_rows;
    int i;
    int row_index_count;

    if (stmt == NULL || table == NULL || indexes == NULL ||
        out_rows == NULL || out_row_count == NULL) {
        return FAILURE;
    }

    if (!utils_is_integer(stmt->where.value)) {
        *out_rows = NULL;
        *out_row_count = 0;
        return SUCCESS;
    }

    win_count = utils_parse_integer(stmt->where.value);
    row_indexes = NULL;
    row_index_count = 0;
    if (index_collect_win_count_row_indexes(indexes, stmt->where.op, win_count,
                                            &row_indexes,
                                            &row_index_count) != SUCCESS) {
        return FAILURE;
    }

    if (row_index_count == 0) {
        *out_rows = NULL;
        *out_row_count = 0;
        return SUCCESS;
    }

    if (executor_allocate_result_rows(&result_rows, row_index_count) != SUCCESS) {
        free(row_indexes);
        return FAILURE;
    }

    for (i = 0; i < row_index_count; i++) {
        if (row_indexes[i] < 0 || row_indexes[i] >= table->row_count) {
            executor_free_result_rows(result_rows, i, selected_count);
            free(row_indexes);
            return FAILURE;
        }

        if (executor_copy_projected_row(result_rows, i, table->rows[row_indexes[i]],
                                        selected_indices, selected_count) != SUCCESS) {
            executor_free_result_rows(result_rows, i, selected_count);
            free(row_indexes);
            return FAILURE;
        }
    }

    free(row_indexes);
    *out_rows = result_rows;
    *out_row_count = row_index_count;
    return SUCCESS;
}

/*
 * INSERT 문 하나를 스토리지 계층으로 실행하고 결과 메시지를 출력한다.
 * 성공하면 해당 테이블의 재사용 캐시를 무효화한다.
 */
static int executor_execute_insert(const InsertStatement *stmt) {
    if (stmt == NULL) {
        return FAILURE;
    }

    if (storage_insert(stmt->table_name, stmt) != SUCCESS) {
        return FAILURE;
    }

    executor_invalidate_table_cache(stmt->table_name);
    if (!executor_silent_output) {
        printf("[성공] %s 테이블에 1행을 삽입했습니다.\n", stmt->table_name);
    }
    return SUCCESS;
}

static int executor_table_supports_player_indexes(const TableData *table) {
    return executor_find_column_index(table->columns, table->col_count, "id") != FAILURE &&
           executor_find_column_index(table->columns, table->col_count,
                                      "game_win_count") != FAILURE;
}

static ExecPlan executor_choose_select_plan(const SelectStatement *stmt,
                                            const TableData *table,
                                            ExecMode mode) {
    int supports_player_indexes;

    if (stmt == NULL || table == NULL || !stmt->has_where) {
        return EXEC_PLAN_FULL_SCAN;
    }

    supports_player_indexes = executor_table_supports_player_indexes(table);
    if (mode == EXEC_MODE_FORCE_LINEAR || !supports_player_indexes) {
        return EXEC_PLAN_LINEAR_SCAN;
    }

    if (mode == EXEC_MODE_FORCE_ID_INDEX) {
        if (strcmp(stmt->where.op, "=") == 0 &&
            utils_equals_ignore_case(stmt->where.column, "id")) {
            return EXEC_PLAN_BPTREE_ID_LOOKUP;
        }
        return EXEC_PLAN_LINEAR_SCAN;
    }

    if (mode == EXEC_MODE_FORCE_WIN_INDEX) {
        if (utils_equals_ignore_case(stmt->where.column, "game_win_count")) {
            return EXEC_PLAN_BPTREE_WIN_LOOKUP;
        }
        return EXEC_PLAN_LINEAR_SCAN;
    }

    /*
     * 일반 SQL 실행에서는 id exact match와 game_win_count 조건을 B+ 트리로 보낸다.
     * 다른 컬럼은 의도적으로 선형 탐색을 사용해 스펙의 비교 기준을 명확히 한다.
     */
    if (strcmp(stmt->where.op, "=") == 0 &&
        utils_equals_ignore_case(stmt->where.column, "id")) {
        return EXEC_PLAN_BPTREE_ID_LOOKUP;
    }
    if (utils_equals_ignore_case(stmt->where.column, "game_win_count")) {
        return EXEC_PLAN_BPTREE_WIN_LOOKUP;
    }

    return EXEC_PLAN_LINEAR_SCAN;
}

/*
 * 함수명: executor_execute_select_with_mode
 * ----------------------------------------
 * 기능: SELECT를 실행하되 benchmark용 mode/silent/stats를 지원한다.
 *
 * 핵심 흐름:
 *   1. projection을 준비한다.
 *   2. 실행 계획을 고른다: full scan, linear scan, id B+ tree, win B+ tree.
 *   3. 결과 row를 모으고, silent가 아니면 표를 출력한다.
 *
 * 개념:
 *   - SQL 문법은 그대로 유지하고 mode 플래그만 바꿔 공정한 benchmark를 만든다.
 *   - print I/O는 benchmark 시간 왜곡이 크기 때문에 silent 모드에서 제거한다.
 */
int executor_execute_select_with_mode(const SelectStatement *stmt, ExecMode mode,
                                      int silent, ExecStats *stats) {
    const TableData *table;
    PlayerIndexSet *indexes;
    int selected_indices[MAX_COLUMNS];
    char headers[MAX_COLUMNS][MAX_IDENTIFIER_LEN];
    int selected_count;
    char ***result_rows;
    int result_row_count;
    int status;
    ExecPlan plan;
    long scanned_rows;
    double start_ms;
    double elapsed_ms;

    if (stmt == NULL) {
        return FAILURE;
    }

    if (stats != NULL) {
        memset(stats, 0, sizeof(*stats));
        stats->plan_used = EXEC_PLAN_NONE;
    }

    start_ms = executor_now_ms();

    if (executor_get_cached_table(stmt->table_name, &table) != SUCCESS) {
        return FAILURE;
    }

    status = executor_prepare_projection(stmt, table, selected_indices, headers,
                                         &selected_count);
    if (status != SUCCESS) {
        return FAILURE;
    }

    result_rows = NULL;
    result_row_count = 0;
    scanned_rows = 0;
    plan = executor_choose_select_plan(stmt, table, mode);

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
            status = executor_collect_id_indexed_rows(stmt, table, indexes,
                                                      selected_indices,
                                                      selected_count,
                                                      &result_rows,
                                                      &result_row_count);
            scanned_rows = result_row_count > 0 ? 1 : 0;
        } else {
            status = executor_collect_win_indexed_rows(stmt, table, indexes,
                                                       selected_indices,
                                                       selected_count,
                                                       &result_rows,
                                                       &result_row_count);
            scanned_rows = result_row_count;
        }
    }

    if (status != SUCCESS) {
        return FAILURE;
    }

    elapsed_ms = executor_now_ms() - start_ms;
    if (stats != NULL) {
        stats->plan_used = plan;
        stats->matched_rows = result_row_count;
        stats->scanned_rows = scanned_rows;
        stats->elapsed_ms = elapsed_ms;
    }

    if (!silent) {
        printf("\n[실행 계획] %s\n", executor_plan_name(plan));
        printf("       %s\n", executor_plan_description(plan));
        printf("       결과 행=%d, 검사 행=%ld, 소요 시간=%.3f ms\n\n",
               result_row_count, scanned_rows, elapsed_ms);
        if (executor_summary_only) {
            printf("[요약] 결과 표 출력은 생략했습니다. 총 %d행을 조회했습니다.\n",
                   result_row_count);
        } else {
            executor_print_table(headers, selected_count, result_rows, result_row_count);
            printf("%d행을 조회했습니다.\n", result_row_count);
        }
    }

    executor_free_result_rows(result_rows, result_row_count, selected_count);
    return SUCCESS;
}

/*
 * SELECT 문 하나를 실행하고 표 형태로 출력한 뒤 결과 메모리를 정리한다.
 * 같은 실행 안에서는 테이블과 players 전용 B+ 트리 인덱스를 재사용한다.
 */
static int executor_execute_select(const SelectStatement *stmt) {
    return executor_execute_select_with_mode(stmt, executor_default_mode,
                                             executor_silent_output, NULL);
}

/*
 * DELETE 문 하나를 실행하고 삭제된 행 수를 출력한다.
 * 성공하면 해당 테이블의 재사용 캐시를 무효화한다.
 */
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
    if (!executor_silent_output) {
        printf("[성공] %s 테이블에서 %d행을 삭제했습니다.\n",
               stmt->table_name, deleted_count);
    }
    return SUCCESS;
}

/*
 * 파싱된 SQL 문을 받아 statement.type에 따라 INSERT, SELECT, DELETE로 분기한다.
 */
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

/*
 * 실행기 런타임 캐시를 모두 해제하고 통계를 초기화한다.
 */
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

/*
 * 마지막 초기화 이후 발생한 테이블 캐시 히트 수를 반환한다.
 */
int executor_get_table_cache_hit_count(void) {
    return executor_table_cache_hit_count;
}

/*
 * 마지막 초기화 이후 발생한 인덱스 캐시 히트 수를 반환한다.
 */
int executor_get_index_cache_hit_count(void) {
    return executor_index_cache_hit_count;
}
