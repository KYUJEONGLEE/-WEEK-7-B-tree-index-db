#include "bptree.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>

static int assert_true(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "[FAIL] %s\n", message);
        return FAILURE;
    }
    return SUCCESS;
}

int main(void) {
    BPTree *tree;
    long values[6000];
    int i;

    tree = bptree_create(BPTREE_ORDER);
    if (assert_true(tree != NULL, "bptree_create should allocate tree") != SUCCESS ||
        assert_true(bptree_search(tree, 1) == NULL,
                    "empty tree search should return NULL") != SUCCESS) {
        return EXIT_FAILURE;
    }

    values[1] = 100;
    if (assert_true(bptree_insert(tree, 1, &values[1]) == SUCCESS,
                    "single insert should succeed") != SUCCESS ||
        assert_true(*(long *)bptree_search(tree, 1) == 100,
                    "single search should return inserted value") != SUCCESS ||
        assert_true(bptree_insert(tree, 1, &values[1]) == FAILURE,
                    "duplicate key insert should fail") != SUCCESS) {
        bptree_destroy(tree);
        return EXIT_FAILURE;
    }

    for (i = 2; i <= 3000; i++) {
        values[i] = i * 10L;
        if (assert_true(bptree_insert(tree, i, &values[i]) == SUCCESS,
                        "ascending insert should succeed") != SUCCESS) {
            bptree_destroy(tree);
            return EXIT_FAILURE;
        }
    }

    for (i = 1; i <= 3000; i++) {
        if (assert_true(bptree_search(tree, i) == &values[i],
                        "ascending search should find every key") != SUCCESS) {
            bptree_destroy(tree);
            return EXIT_FAILURE;
        }
    }

    if (assert_true(bptree_search(tree, 999999) == NULL,
                    "missing key should return NULL") != SUCCESS) {
        bptree_destroy(tree);
        return EXIT_FAILURE;
    }

    bptree_destroy(tree);
    tree = bptree_create(BPTREE_ORDER);
    if (tree == NULL) {
        return EXIT_FAILURE;
    }

    for (i = 5000; i >= 1; i--) {
        values[i] = i * 20L;
        if (assert_true(bptree_insert(tree, i, &values[i]) == SUCCESS,
                        "descending insert should succeed") != SUCCESS) {
            bptree_destroy(tree);
            return EXIT_FAILURE;
        }
    }

    for (i = 1; i <= 5000; i++) {
        if (assert_true(bptree_search(tree, i) == &values[i],
                        "descending search should find every key") != SUCCESS) {
            bptree_destroy(tree);
            return EXIT_FAILURE;
        }
    }

    bptree_destroy(tree);
    puts("[PASS] bptree");
    return EXIT_SUCCESS;
}
