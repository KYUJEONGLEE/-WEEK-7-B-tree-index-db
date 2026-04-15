#include "bptree.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* ─── 빈 트리 검색 ─── */
static void test_empty_tree_search(void) {
    BPTree *tree;

    printf("  test_empty_tree_search...\n");
    tree = bptree_create(BPTREE_ORDER);
    assert_true(tree != NULL, "create should succeed");
    assert_true(bptree_search(tree, 42) == NULL, "search in empty tree returns NULL");
    bptree_destroy(tree);
}

/* ─── 단일 insert/search ─── */
static void test_single_insert_search(void) {
    BPTree *tree;
    int val = 100;

    printf("  test_single_insert_search...\n");
    tree = bptree_create(BPTREE_ORDER);
    assert_true(bptree_insert(tree, 1, &val) == 0, "insert key=1 succeeds");
    assert_true(bptree_search(tree, 1) == &val, "search key=1 returns correct value");
    assert_true(bptree_search(tree, 2) == NULL, "search key=2 returns NULL");
    bptree_destroy(tree);
}

/* ─── 순차 insert 후 search ─── */
static void test_sequential_insert_search(void) {
    BPTree *tree;
    static int vals[20];
    int i;

    printf("  test_sequential_insert_search...\n");
    tree = bptree_create(4); /* order=4로 split 유도 */

    for (i = 0; i < 20; i++) {
        vals[i] = i * 10;
        assert_true(bptree_insert(tree, i + 1, &vals[i]) == 0, "sequential insert");
    }

    for (i = 0; i < 20; i++) {
        void *result = bptree_search(tree, i + 1);
        assert_true(result == &vals[i], "sequential search matches");
    }

    assert_true(tree->count == 20, "count should be 20");
    bptree_destroy(tree);
}

/* ─── 역순 insert 후 search ─── */
static void test_reverse_insert_search(void) {
    BPTree *tree;
    static int vals[20];
    int i;

    printf("  test_reverse_insert_search...\n");
    tree = bptree_create(4);

    for (i = 19; i >= 0; i--) {
        vals[i] = i * 10;
        assert_true(bptree_insert(tree, i + 1, &vals[i]) == 0, "reverse insert");
    }

    for (i = 0; i < 20; i++) {
        void *result = bptree_search(tree, i + 1);
        assert_true(result == &vals[i], "reverse search matches");
    }

    bptree_destroy(tree);
}

/* ─── leaf split 발생 케이스 ─── */
static void test_leaf_split(void) {
    BPTree *tree;
    static int vals[10];
    int i;

    printf("  test_leaf_split...\n");
    tree = bptree_create(4); /* order=4 → max 3 keys per leaf */

    for (i = 0; i < 10; i++) {
        vals[i] = i;
        bptree_insert(tree, i + 1, &vals[i]);
    }

    /* 10개를 order=4에 넣으면 여러 번 split이 발생해야 한다 */
    for (i = 0; i < 10; i++) {
        assert_true(bptree_search(tree, i + 1) == &vals[i], "post-split search");
    }

    assert_true(tree->root->type == BPTREE_INTERNAL, "root should be internal after splits");
    bptree_destroy(tree);
}

/* ─── internal split 발생 케이스 ─── */
static void test_internal_split(void) {
    BPTree *tree;
    static int vals[50];
    int i;

    printf("  test_internal_split...\n");
    tree = bptree_create(4);

    for (i = 0; i < 50; i++) {
        vals[i] = i;
        bptree_insert(tree, i + 1, &vals[i]);
    }

    for (i = 0; i < 50; i++) {
        assert_true(bptree_search(tree, i + 1) == &vals[i], "post-internal-split search");
    }

    bptree_destroy(tree);
}

/* ─── duplicate key insert 실패 ─── */
static void test_duplicate_insert(void) {
    BPTree *tree;
    int v1 = 10;
    int v2 = 20;

    printf("  test_duplicate_insert...\n");
    tree = bptree_create(BPTREE_ORDER);
    assert_true(bptree_insert(tree, 5, &v1) == 0, "first insert succeeds");
    assert_true(bptree_insert(tree, 5, &v2) == -1, "duplicate insert fails");
    assert_true(bptree_search(tree, 5) == &v1, "original value preserved");
    bptree_destroy(tree);
}

/* ─── 존재하지 않는 key 검색 ─── */
static void test_missing_key_search(void) {
    BPTree *tree;
    int val = 1;

    printf("  test_missing_key_search...\n");
    tree = bptree_create(BPTREE_ORDER);
    bptree_insert(tree, 10, &val);
    assert_true(bptree_search(tree, 99) == NULL, "missing key returns NULL");
    assert_true(bptree_search(tree, 0) == NULL, "key 0 returns NULL");
    assert_true(bptree_search(tree, -1) == NULL, "negative key returns NULL");
    bptree_destroy(tree);
}

/* ─── 대량 insert 안정성 ─── */
static void test_large_insert(void) {
    BPTree *tree;
    int i;
    int count = 10000;

    printf("  test_large_insert (%d keys)...\n", count);
    tree = bptree_create(BPTREE_ORDER);

    for (i = 0; i < count; i++) {
        long long key = (long long)((i * 37) % count) + 1; /* 랜덤 순서 */
        /* 중복 방지: 이미 있으면 스킵 */
        if (bptree_search(tree, key) == NULL) {
            bptree_insert(tree, key, (void *)(long)key);
        }
    }

    /* 모든 키 검색 */
    for (i = 1; i <= count; i++) {
        assert_true(bptree_search(tree, (long long)i) != NULL, "large insert search");
    }

    bptree_destroy(tree);
}

int main(void) {
    printf("=== B+ Tree Core Tests ===\n");

    test_empty_tree_search();
    test_single_insert_search();
    test_sequential_insert_search();
    test_reverse_insert_search();
    test_leaf_split();
    test_internal_split();
    test_duplicate_insert();
    test_missing_key_search();
    test_large_insert();

    printf("\n=== Results: %d passed, %d failed ===\n", tests_passed, tests_failed);
    return tests_failed > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
