#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "parser.h"

typedef enum {
    EXEC_MODE_NORMAL,
    EXEC_MODE_FORCE_LINEAR,
    EXEC_MODE_FORCE_ID_INDEX,
    EXEC_MODE_FORCE_WIN_INDEX
} ExecMode;

typedef enum {
    EXEC_PLAN_FULL_SCAN,
    EXEC_PLAN_LINEAR_SCAN,
    EXEC_PLAN_BPTREE_ID_LOOKUP,
    EXEC_PLAN_BPTREE_WIN_LOOKUP
} ExecPlan;

typedef struct {
    ExecPlan plan_used;
    long matched_rows;
    long scanned_rows;
    double elapsed_ms;
} ExecStats;

/*
 * 파싱이 끝난 SQL 문 하나를 실행한다.
 * 성공 시 SUCCESS, 실패 시 FAILURE를 반환한다.
 */
int executor_execute(const SqlStatement *statement);

int executor_execute_select_with_options(const SelectStatement *stmt,
                                         ExecMode mode, int silent,
                                         ExecStats *stats);

/*
 * 실행 중 유지되는 테이블/인덱스 캐시를 모두 해제한다.
 */
void executor_reset_runtime_state(void);

/*
 * 마지막 초기화 이후 테이블 캐시가 재사용된 횟수를 반환한다.
 */
int executor_get_table_cache_hit_count(void);

/*
 * 마지막 초기화 이후 인덱스 캐시가 재사용된 횟수를 반환한다.
 */
int executor_get_index_cache_hit_count(void);

#endif
