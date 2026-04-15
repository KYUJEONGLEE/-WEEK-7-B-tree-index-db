#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#else
#define _POSIX_C_SOURCE 200809L
#endif

#include "player_index.h"
#include "storage.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

static int tests_passed = 0;
static int tests_failed = 0;

static void assert_true(int condition, const char *message) {
    if (condition) {
        tests_passed++;
    } else {
        tests_failed++;
        fprintf(stderr, "  FAIL: %s\n", message);
    }
}

/*
 * 테스트용 CSV 파일을 생성한다.
 */
static void create_test_csv(const char *filename) {
    FILE *fp;
    char path[256];
    struct stat st;

    if (stat("data", &st) != 0) {
        mkdir("data", 0755);
    }

    snprintf(path, sizeof(path), "data/%s.csv", filename);
    fp = fopen(path, "w");
    if (fp == NULL) return;

    fprintf(fp, "id,nickname,game_win_count,game_loss_count,total_game_count\n");
    fprintf(fp, "1,player_1,10,5,15\n");
    fprintf(fp, "2,player_2,20,3,23\n");
    fprintf(fp, "3,player_3,10,8,18\n");  /* game_win_count=10 중복 */
    fprintf(fp, "4,player_4,30,2,32\n");
    fprintf(fp, "5,player_5,20,7,27\n");  /* game_win_count=20 중복 */
    fprintf(fp, "6,player_6,10,1,11\n");  /* game_win_count=10 3번째 */

    fclose(fp);
}

static void cleanup_test_files(const char *filename) {
    char path[256];
    snprintf(path, sizeof(path), "data/%s.csv", filename);
    remove(path);
    snprintf(path, sizeof(path), "data/%s.meta", filename);
    remove(path);
}

/* ─── id 인덱스 빌드 후 검색 ─── */
static void test_id_search(void) {
    TableData table;
    PlayerIndexSet idx;
    RowRef *ref;

    printf("  test_id_search...\n");
    create_test_csv("test_pidx");

    assert_true(storage_load_table("test_pidx", &table) == SUCCESS, "load table");
    assert_true(player_index_build(&table, &idx) == SUCCESS, "build index");

    ref = player_index_search_by_id(&idx, 1);
    assert_true(ref != NULL, "id=1 found");

    ref = player_index_search_by_id(&idx, 3);
    assert_true(ref != NULL, "id=3 found");

    ref = player_index_search_by_id(&idx, 99);
    assert_true(ref == NULL, "id=99 not found");

    player_index_free(&idx);
    storage_free_table(&table);
    cleanup_test_files("test_pidx");
}

/* ─── game_win_count 인덱스 빌드 후 중복 검색 ─── */
static void test_win_count_search(void) {
    TableData table;
    PlayerIndexSet idx;
    POffsetList *list;

    printf("  test_win_count_search...\n");
    create_test_csv("test_pidx2");

    assert_true(storage_load_table("test_pidx2", &table) == SUCCESS, "load table");
    assert_true(player_index_build(&table, &idx) == SUCCESS, "build index");

    /* game_win_count=10 → 3건 (id=1,3,6) */
    list = player_index_search_by_win_count(&idx, 10);
    assert_true(list != NULL, "win_count=10 found");
    assert_true(list->count == 3, "win_count=10 has 3 entries");

    /* game_win_count=20 → 2건 (id=2,5) */
    list = player_index_search_by_win_count(&idx, 20);
    assert_true(list != NULL, "win_count=20 found");
    assert_true(list->count == 2, "win_count=20 has 2 entries");

    /* game_win_count=30 → 1건 */
    list = player_index_search_by_win_count(&idx, 30);
    assert_true(list != NULL, "win_count=30 found");
    assert_true(list->count == 1, "win_count=30 has 1 entry");

    /* game_win_count=999 → 0건 */
    list = player_index_search_by_win_count(&idx, 999);
    assert_true(list == NULL, "win_count=999 not found");

    player_index_free(&idx);
    storage_free_table(&table);
    cleanup_test_files("test_pidx2");
}

/* ─── insert row 후 검색 갱신 ─── */
static void test_insert_row(void) {
    TableData table;
    PlayerIndexSet idx;
    RowRef *ref;
    POffsetList *list;

    printf("  test_insert_row...\n");
    create_test_csv("test_pidx3");

    assert_true(storage_load_table("test_pidx3", &table) == SUCCESS, "load table");
    assert_true(player_index_build(&table, &idx) == SUCCESS, "build index");

    /* 새 row 삽입: id=7, win_count=10, offset=999 */
    assert_true(player_index_insert_row(&idx, 7, 10, 999) == SUCCESS, "insert row");

    ref = player_index_search_by_id(&idx, 7);
    assert_true(ref != NULL, "id=7 found after insert");
    assert_true(ref->offset == 999, "id=7 offset correct");

    list = player_index_search_by_win_count(&idx, 10);
    assert_true(list != NULL, "win_count=10 found after insert");
    assert_true(list->count == 4, "win_count=10 now has 4 entries");

    /* duplicate id insert 실패 */
    assert_true(player_index_insert_row(&idx, 7, 50, 1000) == FAILURE, "dup id fails");

    /* 새 win_count key insert */
    assert_true(player_index_insert_row(&idx, 8, 99, 1001) == SUCCESS, "new win_count");
    list = player_index_search_by_win_count(&idx, 99);
    assert_true(list != NULL && list->count == 1, "win_count=99 has 1 entry");

    player_index_free(&idx);
    storage_free_table(&table);
    cleanup_test_files("test_pidx3");
}

/* ─── meta 기반 auto-id 테스트 ─── */
static void test_meta_auto_id(void) {
    long long next_id;

    printf("  test_meta_auto_id...\n");
    create_test_csv("test_meta");

    /* meta가 없으면 CSV 스캔으로 복구 */
    next_id = storage_get_next_id_from_meta("test_meta");
    assert_true(next_id == 7, "next_id from CSV scan should be 7");

    /* meta에 저장 후 다시 읽기 */
    storage_save_next_id_to_meta("test_meta", 100);
    next_id = storage_get_next_id_from_meta("test_meta");
    assert_true(next_id == 100, "next_id from meta should be 100");

    cleanup_test_files("test_meta");
}

int main(void) {
    printf("=== Player Index Tests ===\n");

    test_id_search();
    test_win_count_search();
    test_insert_row();
    test_meta_auto_id();

    printf("\n=== Results: %d passed, %d failed ===\n", tests_passed, tests_failed);
    return tests_failed > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
