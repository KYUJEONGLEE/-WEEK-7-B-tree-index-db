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
    EXEC_PLAN_NONE,
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

/*
 * 벤치마크 전용 SELECT 실행기다.
 * SQL 문법은 그대로 두고 mode 플래그로 선형 탐색/인덱스 탐색을 강제할 수 있다.
 */
int executor_execute_select_with_mode(const SelectStatement *stmt, ExecMode mode,
                                      int silent, ExecStats *stats);

/*
 * 같은 SELECT를 선형 탐색과 인덱스 선택 모드로 각각 실행해 비교 출력한다.
 * 데모에서 SQL을 한 번만 입력하고 두 실행 계획의 시간을 함께 볼 때 사용한다.
 */
int executor_execute_select_compare(const SelectStatement *stmt,
                                    ExecMode index_mode, int silent);

/*
 * 지정한 테이블을 메모리에 미리 로드하고 가능한 B+ 트리 인덱스를 빌드한다.
 * 첫 SELECT에 로딩/인덱스 생성 시간이 섞이지 않게 만드는 데 사용한다.
 */
int executor_preload_indexes(const char *table_name, int silent);

/*
 * 일반 executor 출력 여부를 설정한다.
 * 대량 SQL 파일 입력에서는 INSERT 성공 메시지 100만 줄이 병목이 될 수 있어 끌 수 있다.
 */
void executor_set_silent(int silent);

/*
 * 일반 executor의 SELECT 실행 모드를 설정한다.
 * 데모에서 같은 SQL을 선형 탐색 또는 B+ 트리 경로로 강제 비교할 때 사용한다.
 */
void executor_set_mode(ExecMode mode);

/*
 * SELECT 결과 표는 숨기고 실행 계획/행 수/시간 요약만 출력한다.
 * 범위 조회처럼 결과가 매우 많을 때 CMD 데모를 읽기 쉽게 만든다.
 */
void executor_set_summary_only(int summary_only);

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
