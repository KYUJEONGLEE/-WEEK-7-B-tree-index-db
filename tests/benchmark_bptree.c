#include "executor.h"
#include "utils.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static int ensure_data_dir(void) {
    struct stat info;

    if (stat("data", &info) == 0) {
        return S_ISDIR(info.st_mode) ? SUCCESS : FAILURE;
    }

    return mkdir("data", 0755) == 0 || errno == EEXIST ? SUCCESS : FAILURE;
}

static double elapsed_ms(clock_t start, clock_t end) {
    return ((double)(end - start) * 1000.0) / (double)CLOCKS_PER_SEC;
}

static int generate_players(long rows) {
    FILE *fp;
    FILE *meta_fp;
    long id;
    long win_count;
    long loss_count;

    if (ensure_data_dir() != SUCCESS) {
        fprintf(stderr, "failed to prepare data directory\n");
        return FAILURE;
    }

    fp = fopen("data/players.csv", "w");
    if (fp == NULL) {
        fprintf(stderr, "failed to create players.csv\n");
        return FAILURE;
    }

    fputs("id,nickname,game_win_count,game_loss_count,total_game_count\n", fp);
    for (id = 1; id <= rows; id++) {
        win_count = (id * 37) % 100000;
        loss_count = ((id * 17) % 500) + 1;
        fprintf(fp, "%ld,player_%06ld,%ld,%ld,%ld\n",
                id, id, win_count, loss_count, win_count + loss_count);
    }
    fclose(fp);

    meta_fp = fopen("data/players.meta", "w");
    if (meta_fp == NULL) {
        fprintf(stderr, "failed to create players.meta\n");
        return FAILURE;
    }
    fprintf(meta_fp, "%ld\n", rows + 1);
    fclose(meta_fp);
    return SUCCESS;
}

static void prepare_select(SelectStatement *stmt, const char *column,
                           const char *value) {
    memset(stmt, 0, sizeof(*stmt));
    snprintf(stmt->table_name, sizeof(stmt->table_name), "players");
    stmt->column_count = 0;
    stmt->has_where = 1;
    snprintf(stmt->where.column, sizeof(stmt->where.column), "%s", column);
    snprintf(stmt->where.op, sizeof(stmt->where.op), "=");
    snprintf(stmt->where.value, sizeof(stmt->where.value), "%s", value);
}

/*
 * 함수명: run_query_loop
 * ------------------------------------------------------------
 * 기능:
 *   같은 SELECT 조건을 지정한 ExecMode로 반복 실행해 평균 시간을 측정한다.
 *
 * 개념:
 *   benchmark에서는 출력 I/O가 검색 비용을 덮어버릴 수 있으므로 silent=1로
 *   실행한다. 같은 조건을 FORCE_LINEAR와 FORCE_*_INDEX로 나누어 실행하면
 *   결과 수는 같고 실행 경로만 다른 공정 비교가 된다.
 */
static int run_query_loop(const SelectStatement *stmt, ExecMode mode, long queries,
                          ExecStats *last_stats, double *avg_ms) {
    long i;
    clock_t start;
    clock_t end;
    ExecStats stats;

    start = clock();
    for (i = 0; i < queries; i++) {
        if (executor_execute_select_with_options(stmt, mode, 1, &stats) != SUCCESS) {
            return FAILURE;
        }
    }
    end = clock();

    if (last_stats != NULL) {
        *last_stats = stats;
    }
    *avg_ms = elapsed_ms(start, end) / (double)queries;
    return SUCCESS;
}

int main(int argc, char **argv) {
    long rows;
    long queries;
    SelectStatement id_stmt;
    SelectStatement win_stmt;
    SelectStatement nickname_stmt;
    ExecStats id_index_stats;
    ExecStats id_linear_stats;
    ExecStats win_index_stats;
    ExecStats win_linear_stats;
    ExecStats nickname_stats;
    double id_index_avg;
    double id_linear_avg;
    double win_index_avg;
    double win_linear_avg;
    double nickname_avg;
    clock_t start;
    clock_t end;
    double generation_ms;
    char id_value[32];
    char win_value[32];
    char nickname_value[64];
    long target_id;
    long target_win;

    rows = argc > 1 ? atol(argv[1]) : 1000000L;
    queries = argc > 2 ? atol(argv[2]) : 1000L;
    if (rows < 1 || queries < 1) {
        fprintf(stderr, "usage: benchmark_bptree [rows] [queries]\n");
        return EXIT_FAILURE;
    }

    start = clock();
    if (generate_players(rows) != SUCCESS) {
        return EXIT_FAILURE;
    }
    end = clock();
    generation_ms = elapsed_ms(start, end);

    target_id = rows / 2;
    if (target_id < 1) {
        target_id = 1;
    }
    target_win = (target_id * 37) % 100000;

    snprintf(id_value, sizeof(id_value), "%ld", target_id);
    snprintf(win_value, sizeof(win_value), "%ld", target_win);
    snprintf(nickname_value, sizeof(nickname_value), "player_%06ld", target_id);

    prepare_select(&id_stmt, "id", id_value);
    prepare_select(&win_stmt, "game_win_count", win_value);
    prepare_select(&nickname_stmt, "nickname", nickname_value);

    executor_reset_runtime_state();
    executor_execute_select_with_options(&id_stmt, EXEC_MODE_NORMAL, 1, &id_index_stats);
    executor_execute_select_with_options(&win_stmt, EXEC_MODE_NORMAL, 1, &win_index_stats);

    if (run_query_loop(&id_stmt, EXEC_MODE_FORCE_ID_INDEX, queries,
                       &id_index_stats, &id_index_avg) != SUCCESS ||
        run_query_loop(&id_stmt, EXEC_MODE_FORCE_LINEAR, queries,
                       &id_linear_stats, &id_linear_avg) != SUCCESS ||
        run_query_loop(&win_stmt, EXEC_MODE_FORCE_WIN_INDEX, queries,
                       &win_index_stats, &win_index_avg) != SUCCESS ||
        run_query_loop(&win_stmt, EXEC_MODE_FORCE_LINEAR, queries,
                       &win_linear_stats, &win_linear_avg) != SUCCESS ||
        run_query_loop(&nickname_stmt, EXEC_MODE_NORMAL, queries,
                       &nickname_stats, &nickname_avg) != SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("========================================================\n");
    printf("B+ Tree benchmark (rows: %ld, queries: %ld)\n", rows, queries);
    printf("========================================================\n");
    printf("Data generation: %.3f ms\n\n", generation_ms);

    printf("[Compare] WHERE game_win_count = %s\n", win_value);
    printf("FORCE_LINEAR\n");
    printf("- plan used: %d\n", win_linear_stats.plan_used);
    printf("- matched rows: %ld\n", win_linear_stats.matched_rows);
    printf("- scanned rows: %ld\n", win_linear_stats.scanned_rows);
    printf("- average: %.6f ms\n\n", win_linear_avg);
    printf("FORCE_WIN_INDEX\n");
    printf("- plan used: %d\n", win_index_stats.plan_used);
    printf("- matched rows: %ld\n", win_index_stats.matched_rows);
    printf("- scanned rows: %ld\n", win_index_stats.scanned_rows);
    printf("- average: %.6f ms\n", win_index_avg);
    printf("speedup: %.2fx\n\n", win_linear_avg / win_index_avg);

    printf("[Compare] WHERE id = %s\n", id_value);
    printf("FORCE_LINEAR\n");
    printf("- plan used: %d\n", id_linear_stats.plan_used);
    printf("- matched rows: %ld\n", id_linear_stats.matched_rows);
    printf("- scanned rows: %ld\n", id_linear_stats.scanned_rows);
    printf("- average: %.6f ms\n\n", id_linear_avg);
    printf("FORCE_ID_INDEX\n");
    printf("- plan used: %d\n", id_index_stats.plan_used);
    printf("- matched rows: %ld\n", id_index_stats.matched_rows);
    printf("- scanned rows: %ld\n", id_index_stats.scanned_rows);
    printf("- average: %.6f ms\n", id_index_avg);
    printf("speedup: %.2fx\n\n", id_linear_avg / id_index_avg);

    printf("[Reference] WHERE nickname = '%s'\n", nickname_value);
    printf("- plan used: %d\n", nickname_stats.plan_used);
    printf("- matched rows: %ld\n", nickname_stats.matched_rows);
    printf("- scanned rows: %ld\n", nickname_stats.scanned_rows);
    printf("- average: %.6f ms\n", nickname_avg);
    printf("========================================================\n");

    executor_reset_runtime_state();
    return EXIT_SUCCESS;
}
