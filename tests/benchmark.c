/*
 * 벤치마크 전용 실행 파일
 * ─────────────────────────────────────────────────
 * 1,000,000건 플레이어 전적 데이터에 대해
 * B+ 트리 인덱스 vs 선형 탐색 성능을 비교한다.
 *
 * 비교 항목:
 *   1. WHERE id = ? → B+ 트리 vs 선형 탐색
 *   2. WHERE game_win_count = ? → B+ 트리 vs 선형 탐색
 *   3. WHERE nickname = ? → 선형 탐색 (인덱스 없음)
 *
 * 측정 원칙:
 *   - 같은 데이터셋, 같은 검색 조건, 다른 실행 경로
 *   - 두 실행의 matched row count가 같아야 공정한 비교
 *   - silent 모드로 I/O 제거하여 순수 검색 성능만 측정
 */

#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#else
#define _POSIX_C_SOURCE 200809L
#endif

#include "executor.h"
#include "player_index.h"
#include "storage.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define NUM_ROWS 1000000
#define NUM_QUERIES 1000
#define TABLE_NAME "players"

static double time_diff_ms(struct timespec *start, struct timespec *end) {
    return (end->tv_sec - start->tv_sec) * 1000.0 +
           (end->tv_nsec - start->tv_nsec) / 1000000.0;
}

/*
 * 벤치마크용 대량 CSV 데이터를 빠르게 생성한다.
 *
 * 왜 SQL INSERT가 아니라 직접 CSV를 생성하는가?
 *   1,000,000개의 SQL 문을 tokenizer/parser까지 다 태우면 benchmark 목적이 흐려진다.
 *   이번 benchmark는 "SQL 파싱 속도"가 아니라 "B+ 트리 vs 선형 탐색" 비교가 중심이다.
 */
static double generate_data(int num_rows) {
    FILE *fp;
    char path[MAX_PATH_LEN];
    int i;
    int win, loss, total;
    struct timespec start, end;
    struct stat st;

    if (stat("data", &st) != 0) {
        mkdir("data", 0755);
    }

    snprintf(path, sizeof(path), "data/%s.csv", TABLE_NAME);

    clock_gettime(CLOCK_MONOTONIC, &start);

    fp = fopen(path, "w");
    if (fp == NULL) {
        fprintf(stderr, "Error: Cannot create data file.\n");
        return -1;
    }

    fprintf(fp, "id,nickname,game_win_count,game_loss_count,total_game_count\n");
    for (i = 1; i <= num_rows; i++) {
        win = (i * 37) % 100000;
        loss = ((i * 17) % 500) + 1;
        total = win + loss;
        fprintf(fp, "%d,player_%06d,%d,%d,%d\n", i, i, win, loss, total);
    }

    fclose(fp);

    /* meta 파일도 생성 */
    storage_save_next_id_to_meta(TABLE_NAME, (long long)(num_rows + 1));

    clock_gettime(CLOCK_MONOTONIC, &end);
    return time_diff_ms(&start, &end);
}

/*
 * 벤치마크 측정 루프
 *
 * 같은 SELECT 조건을 지정된 ExecMode로 num_queries회 반복 실행하고
 * 평균 시간과 통계를 반환한다.
 */
static double run_benchmark(const SelectStatement *stmt, ExecMode mode,
                            int num_queries, ExecStats *avg_stats) {
    ExecStats stats;
    double total_ms;
    int i;

    memset(avg_stats, 0, sizeof(*avg_stats));
    total_ms = 0;

    for (i = 0; i < num_queries; i++) {
        memset(&stats, 0, sizeof(stats));
        executor_execute_select_with_mode(stmt, mode, 1, &stats);
        total_ms += stats.elapsed_ms;
        avg_stats->plan_used = stats.plan_used;
        avg_stats->matched_rows = stats.matched_rows;
        avg_stats->scanned_rows += stats.scanned_rows;
    }

    avg_stats->elapsed_ms = total_ms / num_queries;
    avg_stats->scanned_rows /= num_queries;
    return total_ms;
}

static const char *plan_name(ExecPlan plan) {
    switch (plan) {
        case PLAN_BPTREE_ID_LOOKUP:  return "BPTREE_ID_LOOKUP";
        case PLAN_BPTREE_WIN_LOOKUP: return "BPTREE_WIN_LOOKUP";
        case PLAN_LINEAR_SCAN:       return "LINEAR_SCAN";
        default:                     return "UNKNOWN";
    }
}

int main(void) {
    double gen_time;
    struct timespec ts_start, ts_end;
    double build_ms;
    TableData table;
    PlayerIndexSet pindex;
    SelectStatement stmt;
    ExecStats stats_linear, stats_index;
    double total_linear, total_index;

    printf("========================================================\n");
    printf("B+ Tree Benchmark (order=%d)\n", BPTREE_ORDER);
    printf("========================================================\n\n");

    /* ─── 데이터 생성 ─── */
    printf("[1] Generating %d rows...\n", NUM_ROWS);
    gen_time = generate_data(NUM_ROWS);
    printf("    Data generation: %.2f ms (%.2f sec)\n\n", gen_time, gen_time / 1000.0);

    /* ─── 인덱스 빌드 시간 측정 ─── */
    printf("[2] Building indexes...\n");
    if (storage_load_table(TABLE_NAME, &table) != SUCCESS) {
        fprintf(stderr, "Error: Failed to load table.\n");
        return EXIT_FAILURE;
    }

    clock_gettime(CLOCK_MONOTONIC, &ts_start);
    if (player_index_build(&table, &pindex) != SUCCESS) {
        fprintf(stderr, "Error: Failed to build index.\n");
        storage_free_table(&table);
        return EXIT_FAILURE;
    }
    clock_gettime(CLOCK_MONOTONIC, &ts_end);

    build_ms = time_diff_ms(&ts_start, &ts_end);
    printf("    B+ tree build (id + win_count): %.2f ms\n", build_ms);
    printf("    id tree keys: %d\n", pindex.id_tree->count);
    printf("    win tree keys: %d\n", pindex.win_tree->count);

    player_index_free(&pindex);
    storage_free_table(&table);
    printf("\n");

    /* ─── Warm-up ─── */
    printf("[3] Warm-up run...\n");
    memset(&stmt, 0, sizeof(stmt));
    stmt.has_where = 1;
    utils_safe_strcpy(stmt.table_name, sizeof(stmt.table_name), TABLE_NAME);
    utils_safe_strcpy(stmt.where.column, sizeof(stmt.where.column), "id");
    utils_safe_strcpy(stmt.where.op, sizeof(stmt.where.op), "=");
    utils_safe_strcpy(stmt.where.value, sizeof(stmt.where.value), "500000");
    executor_execute_select_with_mode(&stmt, EXEC_MODE_NORMAL, 1, NULL);
    printf("    Done.\n\n");

    /* ─── Compare: WHERE id = 500000 ─── */
    printf("========================================================\n");
    printf("[Compare] WHERE id = 500000\n");
    printf("========================================================\n\n");

    memset(&stmt, 0, sizeof(stmt));
    stmt.has_where = 1;
    utils_safe_strcpy(stmt.table_name, sizeof(stmt.table_name), TABLE_NAME);
    utils_safe_strcpy(stmt.where.column, sizeof(stmt.where.column), "id");
    utils_safe_strcpy(stmt.where.op, sizeof(stmt.where.op), "=");
    utils_safe_strcpy(stmt.where.value, sizeof(stmt.where.value), "500000");

    total_linear = run_benchmark(&stmt, EXEC_MODE_FORCE_LINEAR, NUM_QUERIES, &stats_linear);
    total_index = run_benchmark(&stmt, EXEC_MODE_FORCE_ID_INDEX, NUM_QUERIES, &stats_index);

    printf("  FORCE_LINEAR:\n");
    printf("    plan: %s\n", plan_name(stats_linear.plan_used));
    printf("    matched rows: %ld\n", stats_linear.matched_rows);
    printf("    average: %.3f ms (%d queries)\n", stats_linear.elapsed_ms, NUM_QUERIES);
    printf("    total: %.2f ms\n\n", total_linear);

    printf("  FORCE_ID_INDEX:\n");
    printf("    plan: %s\n", plan_name(stats_index.plan_used));
    printf("    matched rows: %ld\n", stats_index.matched_rows);
    printf("    average: %.3f ms (%d queries)\n", stats_index.elapsed_ms, NUM_QUERIES);
    printf("    total: %.2f ms\n\n", total_index);

    if (stats_index.elapsed_ms > 0) {
        printf("  speedup: %.2fx\n\n", stats_linear.elapsed_ms / stats_index.elapsed_ms);
    }

    /* ─── Compare: WHERE game_win_count = 120 ─── */
    printf("========================================================\n");
    printf("[Compare] WHERE game_win_count = 120\n");
    printf("========================================================\n\n");

    memset(&stmt, 0, sizeof(stmt));
    stmt.has_where = 1;
    utils_safe_strcpy(stmt.table_name, sizeof(stmt.table_name), TABLE_NAME);
    utils_safe_strcpy(stmt.where.column, sizeof(stmt.where.column), "game_win_count");
    utils_safe_strcpy(stmt.where.op, sizeof(stmt.where.op), "=");
    utils_safe_strcpy(stmt.where.value, sizeof(stmt.where.value), "120");

    total_linear = run_benchmark(&stmt, EXEC_MODE_FORCE_LINEAR, NUM_QUERIES, &stats_linear);
    total_index = run_benchmark(&stmt, EXEC_MODE_FORCE_WIN_INDEX, NUM_QUERIES, &stats_index);

    printf("  FORCE_LINEAR:\n");
    printf("    plan: %s\n", plan_name(stats_linear.plan_used));
    printf("    matched rows: %ld\n", stats_linear.matched_rows);
    printf("    average: %.3f ms (%d queries)\n", stats_linear.elapsed_ms, NUM_QUERIES);
    printf("    total: %.2f ms\n\n", total_linear);

    printf("  FORCE_WIN_INDEX:\n");
    printf("    plan: %s\n", plan_name(stats_index.plan_used));
    printf("    matched rows: %ld\n", stats_index.matched_rows);
    printf("    average: %.3f ms (%d queries)\n", stats_index.elapsed_ms, NUM_QUERIES);
    printf("    total: %.2f ms\n\n", total_index);

    if (stats_index.elapsed_ms > 0) {
        printf("  speedup: %.2fx\n\n", stats_linear.elapsed_ms / stats_index.elapsed_ms);
    }

    /* ─── Compare: WHERE nickname = 'player_500000' (linear only) ─── */
    printf("========================================================\n");
    printf("[Reference] WHERE nickname = 'player_500000' (LINEAR only)\n");
    printf("========================================================\n\n");

    memset(&stmt, 0, sizeof(stmt));
    stmt.has_where = 1;
    utils_safe_strcpy(stmt.table_name, sizeof(stmt.table_name), TABLE_NAME);
    utils_safe_strcpy(stmt.where.column, sizeof(stmt.where.column), "nickname");
    utils_safe_strcpy(stmt.where.op, sizeof(stmt.where.op), "=");
    utils_safe_strcpy(stmt.where.value, sizeof(stmt.where.value), "player_500000");

    total_linear = run_benchmark(&stmt, EXEC_MODE_FORCE_LINEAR, NUM_QUERIES, &stats_linear);

    printf("  FORCE_LINEAR:\n");
    printf("    plan: %s\n", plan_name(stats_linear.plan_used));
    printf("    matched rows: %ld\n", stats_linear.matched_rows);
    printf("    average: %.3f ms (%d queries)\n", stats_linear.elapsed_ms, NUM_QUERIES);
    printf("    total: %.2f ms\n\n", total_linear);

    /* ─── 정리 ─── */
    executor_reset_runtime_state();

    printf("========================================================\n");
    printf("Benchmark complete.\n");
    printf("========================================================\n");

    return EXIT_SUCCESS;
}
