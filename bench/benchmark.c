#include "executor.h"
#include "utils.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static double benchmark_now_ms(void) {
    return ((double)clock() * 1000.0) / (double)CLOCKS_PER_SEC;
}

static const char *benchmark_plan_name(ExecPlan plan) {
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

static int benchmark_ensure_data_dir(void) {
    if (mkdir("data", 0755) == 0 || errno == EEXIST) {
        return SUCCESS;
    }

    fprintf(stderr, "Error: Failed to create data directory.\n");
    return FAILURE;
}

/*
 * 함수명: benchmark_generate_players_csv
 * ----------------------------------------
 * 기능: benchmark용 players CSV와 meta 파일을 빠르게 생성한다.
 *
 * 핵심 흐름:
 *   1. SQL 100만 문장을 만들지 않고 CSV를 직접 쓴다.
 *   2. id는 1부터 순차 증가한다.
 *   3. game_win_count는 반복값을 만들어 secondary index 중복 key를 만든다.
 *
 * 개념:
 *   - 이 benchmark의 목표는 tokenizer/parser 성능이 아니라 B+ 트리 vs 선형 탐색이다.
 *   - 그래서 대용량 데이터 준비는 전용 generator가 담당한다.
 */
static int benchmark_generate_players_csv(long rows, double *elapsed_ms) {
    FILE *csv;
    FILE *meta;
    long i;
    long wins;
    long losses;
    double start_ms;

    if (benchmark_ensure_data_dir() != SUCCESS) {
        return FAILURE;
    }

    start_ms = benchmark_now_ms();
    csv = fopen("data/players.csv", "w");
    if (csv == NULL) {
        fprintf(stderr, "Error: Failed to create players.csv.\n");
        return FAILURE;
    }

    fprintf(csv, "id,nickname,game_win_count,game_loss_count,total_game_count\n");
    for (i = 1; i <= rows; i++) {
        wins = (i * 37) % 100000;
        losses = ((i * 17) % 500) + 1;
        fprintf(csv, "%ld,player_%06ld,%ld,%ld,%ld\n",
                i, i, wins, losses, wins + losses);
    }
    fclose(csv);

    meta = fopen("data/players.meta", "w");
    if (meta == NULL) {
        fprintf(stderr, "Error: Failed to create players.meta.\n");
        return FAILURE;
    }
    fprintf(meta, "%ld\n", rows + 1);
    fclose(meta);

    if (elapsed_ms != NULL) {
        *elapsed_ms = benchmark_now_ms() - start_ms;
    }
    return SUCCESS;
}

static void benchmark_prepare_select(SelectStatement *stmt, const char *column,
                                     const char *value) {
    memset(stmt, 0, sizeof(*stmt));
    snprintf(stmt->table_name, sizeof(stmt->table_name), "players");
    stmt->column_count = 0;
    stmt->has_where = 1;
    snprintf(stmt->where.column, sizeof(stmt->where.column), "%s", column);
    snprintf(stmt->where.op, sizeof(stmt->where.op), "=");
    snprintf(stmt->where.value, sizeof(stmt->where.value), "%s", value);
}

static long benchmark_random_id(long rows) {
    return (long)(rand() % (int)rows) + 1;
}

static int benchmark_build_random_ids(long rows, int queries, long **out_ids) {
    long *ids;
    int i;

    if (rows <= 0 || queries <= 0 || out_ids == NULL) {
        return FAILURE;
    }

    ids = (long *)malloc((size_t)queries * sizeof(long));
    if (ids == NULL) {
        fprintf(stderr, "Error: Failed to allocate benchmark query values.\n");
        return FAILURE;
    }

    for (i = 0; i < queries; i++) {
        ids[i] = benchmark_random_id(rows);
    }

    *out_ids = ids;
    return SUCCESS;
}

/*
 * 함수명: benchmark_run_random_id_select
 * ----------------------------------------
 * 기능: 미리 생성한 랜덤 id 목록을 같은 실행 모드로 조회해 평균 시간을 구한다.
 *
 * 핵심 흐름:
 *   1. executor silent mode로 출력 I/O를 제거한다.
 *   2. 선형 탐색과 B+ 트리가 같은 id 목록을 사용하도록 한다.
 *   3. 전체 시간 / 반복 횟수로 평균 ms를 계산한다.
 */
static int benchmark_run_random_id_select(const long *ids, int queries,
                                          ExecMode mode, ExecStats *stats,
                                          double *average_ms) {
    SelectStatement stmt;
    char value[64];
    int i;
    double start_ms;

    if (ids == NULL || stats == NULL || average_ms == NULL || queries <= 0) {
        return FAILURE;
    }

    start_ms = benchmark_now_ms();
    for (i = 0; i < queries; i++) {
        snprintf(value, sizeof(value), "%ld", ids[i]);
        benchmark_prepare_select(&stmt, "id", value);
        if (executor_execute_select_with_mode(&stmt, mode, 1, stats) != SUCCESS) {
            return FAILURE;
        }
    }
    *average_ms = (benchmark_now_ms() - start_ms) / (double)queries;
    return SUCCESS;
}

/*
 * 함수명: benchmark_run_random_win_select
 * ----------------------------------------
 * 기능: 랜덤 id 목록에서 파생한 game_win_count 값을 조회해 평균 시간을 구한다.
 *
 * 주의:
 *   - 데이터 생성식과 같은 `(id * 37) % 100000`을 사용한다.
 *   - 같은 win_count 목록을 선형 탐색과 B+ 트리에 모두 적용한다.
 */
static int benchmark_run_random_win_select(const long *ids, int queries,
                                           ExecMode mode, ExecStats *stats,
                                           double *average_ms) {
    SelectStatement stmt;
    char value[64];
    long win_value;
    int i;
    double start_ms;

    if (ids == NULL || stats == NULL || average_ms == NULL || queries <= 0) {
        return FAILURE;
    }

    start_ms = benchmark_now_ms();
    for (i = 0; i < queries; i++) {
        win_value = (ids[i] * 37) % 100000;
        snprintf(value, sizeof(value), "%ld", win_value);
        benchmark_prepare_select(&stmt, "game_win_count", value);
        if (executor_execute_select_with_mode(&stmt, mode, 1, stats) != SUCCESS) {
            return FAILURE;
        }
    }

    *average_ms = (benchmark_now_ms() - start_ms) / (double)queries;
    return SUCCESS;
}

static int benchmark_run_random_nickname_select(const long *ids, int queries,
                                                ExecStats *stats,
                                                double *average_ms) {
    SelectStatement stmt;
    char value[64];
    int i;
    double start_ms;

    if (ids == NULL || stats == NULL || average_ms == NULL || queries <= 0) {
        return FAILURE;
    }

    start_ms = benchmark_now_ms();
    for (i = 0; i < queries; i++) {
        snprintf(value, sizeof(value), "player_%06ld", ids[i]);
        benchmark_prepare_select(&stmt, "nickname", value);
        if (executor_execute_select_with_mode(&stmt, EXEC_MODE_NORMAL, 1,
                                              stats) != SUCCESS) {
            return FAILURE;
        }
    }

    *average_ms = (benchmark_now_ms() - start_ms) / (double)queries;
    return SUCCESS;
}

static void benchmark_print_case(const char *label, const ExecStats *stats,
                                 double average_ms) {
    printf("%s\n", label);
    printf("- 실행 계획: %s\n", benchmark_plan_name(stats->plan_used));
    printf("- 결과 행 수: %ld\n", stats->matched_rows);
    printf("- 검사 행 수: %ld\n", stats->scanned_rows);
    printf("- 평균 시간: %.6f ms\n\n", average_ms);
}

int main(int argc, char *argv[]) {
    long rows;
    int queries;
    double generate_ms;
    double id_linear_ms;
    double id_index_ms;
    double win_linear_ms;
    double win_index_ms;
    double nickname_ms;
    SelectStatement stmt;
    ExecStats id_linear_stats;
    ExecStats id_index_stats;
    ExecStats win_linear_stats;
    ExecStats win_index_stats;
    ExecStats nickname_stats;
    long *query_ids;

    rows = argc >= 2 ? atol(argv[1]) : 1000000L;
    queries = argc >= 3 ? atoi(argv[2]) : 1000;
    if (rows <= 0 || queries <= 0) {
        fprintf(stderr, "Usage: %s [rows] [queries]\n", argv[0]);
        return EXIT_FAILURE;
    }

    if (benchmark_generate_players_csv(rows, &generate_ms) != SUCCESS) {
        return EXIT_FAILURE;
    }

    srand(42);
    query_ids = NULL;
    if (benchmark_build_random_ids(rows, queries, &query_ids) != SUCCESS) {
        return EXIT_FAILURE;
    }

    benchmark_prepare_select(&stmt, "id", "1");
    executor_execute_select_with_mode(&stmt, EXEC_MODE_FORCE_ID_INDEX, 1,
                                      &id_index_stats);
    if (benchmark_run_random_id_select(query_ids, queries, EXEC_MODE_FORCE_LINEAR,
                                       &id_linear_stats, &id_linear_ms) != SUCCESS ||
        benchmark_run_random_id_select(query_ids, queries, EXEC_MODE_FORCE_ID_INDEX,
                                       &id_index_stats, &id_index_ms) != SUCCESS) {
        free(query_ids);
        return EXIT_FAILURE;
    }

    benchmark_prepare_select(&stmt, "game_win_count", "0");
    executor_execute_select_with_mode(&stmt, EXEC_MODE_FORCE_WIN_INDEX, 1,
                                      &win_index_stats);
    if (benchmark_run_random_win_select(query_ids, queries, EXEC_MODE_FORCE_LINEAR,
                                        &win_linear_stats, &win_linear_ms) != SUCCESS ||
        benchmark_run_random_win_select(query_ids, queries, EXEC_MODE_FORCE_WIN_INDEX,
                                        &win_index_stats, &win_index_ms) != SUCCESS) {
        free(query_ids);
        return EXIT_FAILURE;
    }

    if (benchmark_run_random_nickname_select(query_ids, queries, &nickname_stats,
                                             &nickname_ms) != SUCCESS) {
        free(query_ids);
        return EXIT_FAILURE;
    }

    printf("========================================================\n");
    printf("성능 테스트 결과 (레코드 수: %ld, 반복: %d)\n", rows, queries);
    printf("========================================================\n");
    printf("데이터 생성 시간: %.2f ms\n\n", generate_ms);
    printf("조회 값 생성: srand(42) 기반 랜덤 id %d개\n", queries);
    printf("첫 번째 랜덤 id: %ld\n\n", query_ids[0]);

    printf("[비교] WHERE game_win_count = 랜덤 값\n\n");
    benchmark_print_case("강제 선형 탐색", &win_linear_stats, win_linear_ms);
    benchmark_print_case("강제 승리 횟수 B+트리", &win_index_stats, win_index_ms);
    if (win_index_stats.matched_rows == win_linear_stats.matched_rows) {
        printf("속도 향상: %.2fx\n\n", win_linear_ms / win_index_ms);
    }

    printf("[비교] WHERE id = 랜덤 값\n\n");
    benchmark_print_case("강제 선형 탐색", &id_linear_stats, id_linear_ms);
    benchmark_print_case("강제 ID B+트리", &id_index_stats, id_index_ms);
    if (id_index_stats.matched_rows == id_linear_stats.matched_rows) {
        printf("속도 향상: %.2fx\n\n", id_linear_ms / id_index_ms);
    }

    printf("[참고] WHERE nickname = 랜덤 값\n\n");
    benchmark_print_case("일반 실행", &nickname_stats, nickname_ms);
    printf("========================================================\n");

    free(query_ids);
    executor_reset_runtime_state();
    return EXIT_SUCCESS;
}
