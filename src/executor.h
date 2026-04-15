#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "parser.h"

/*
 * 실행 계획: 어떤 경로로 SELECT를 처리했는지 표시
 *
 * 왜 id와 game_win_count만 인덱스 경로로 보내는가?
 *   - id: unique PK → B+ 트리로 O(log n) exact match
 *   - game_win_count: secondary index → B+ 트리로 O(log n) exact match
 *   - nickname, game_loss_count, total_game_count: 인덱스 없음 → 반드시 선형 탐색
 */
typedef enum {
    PLAN_BPTREE_ID_LOOKUP,
    PLAN_BPTREE_WIN_LOOKUP,
    PLAN_LINEAR_SCAN
} ExecPlan;

/*
 * 벤치마크 모드 플래그.
 * 같은 SQL 의미를 유지하면서 실행 경로를 강제로 지정할 수 있게 한다.
 * 이를 통해 같은 조건에 대해 B+ 트리 vs 선형 탐색을 각각 측정할 수 있다.
 */
typedef enum {
    EXEC_MODE_NORMAL,         /* 일반 실행: 규칙에 따라 자동 계획 선택 */
    EXEC_MODE_FORCE_LINEAR,   /* 벤치마크용: 선형 탐색 강제 */
    EXEC_MODE_FORCE_ID_INDEX, /* 벤치마크용: id 인덱스 강제 */
    EXEC_MODE_FORCE_WIN_INDEX /* 벤치마크용: game_win_count 인덱스 강제 */
} ExecMode;

/*
 * 벤치마크용 실행 통계.
 * 데모 시 같은 조건에 대해 다른 실행 경로의 결과 개수와 시간 차이를 함께 보여줄 수 있다.
 */
typedef struct {
    ExecPlan plan_used;     /* 실제 사용한 실행 계획 */
    long matched_rows;      /* 조건에 맞은 row 수 */
    long scanned_rows;      /* 실제 검사한 row 수 */
    double elapsed_ms;      /* 실행 시간(ms) */
} ExecStats;

/*
 * 파싱이 끝난 SQL 문 하나를 실행한다.
 * 성공 시 SUCCESS, 실패 시 FAILURE를 반환한다.
 */
int executor_execute(const SqlStatement *statement);

/*
 * 벤치마크 모드로 SELECT를 실행한다.
 * silent=1이면 결과 출력 없이 내부 처리만 수행한다.
 * stats가 NULL이 아니면 실행 통계를 채운다.
 */
int executor_execute_select_with_mode(const SelectStatement *stmt,
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
