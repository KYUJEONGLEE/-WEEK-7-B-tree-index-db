#include "bptree.h"

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
    long values[64];
    int i;

    tree = bptree_create(4);
    if (assert_true(tree != NULL, "bptree_create should create tree") != SUCCESS ||
        assert_true(bptree_search(tree, 1) == NULL,
                    "empty tree search should return NULL") != SUCCESS) {
        return EXIT_FAILURE;
    }

    for (i = 0; i < 64; i++) {
        values[i] = i * 10L;
    }

    for (i = 1; i <= 20; i++) {
        if (assert_true(bptree_insert(tree, i, &values[i]) == SUCCESS,
                        "ascending insert should succeed") != SUCCESS) {
            return EXIT_FAILURE;
        }
    }

    for (i = 1; i <= 20; i++) {
        if (assert_true(bptree_search(tree, i) == &values[i],
                        "ascending search should find inserted value") != SUCCESS) {
            return EXIT_FAILURE;
        }
    }

    if (assert_true(bptree_insert(tree, 10, &values[10]) == FAILURE,
                    "duplicate key insert should fail") != SUCCESS ||
        assert_true(bptree_search(tree, 999) == NULL,
                    "missing key search should return NULL") != SUCCESS) {
        return EXIT_FAILURE;
    }

    bptree_destroy(tree);

    tree = bptree_create(4);
    if (tree == NULL) {
        return EXIT_FAILURE;
    }

    for (i = 20; i >= 1; i--) {
        if (assert_true(bptree_insert(tree, i, &values[i]) == SUCCESS,
                        "descending insert should succeed") != SUCCESS) {
            return EXIT_FAILURE;
        }
    }

    for (i = 1; i <= 20; i++) {
        if (assert_true(bptree_search(tree, i) == &values[i],
                        "descending search should find inserted value") != SUCCESS) {
            return EXIT_FAILURE;
        }
    }

    bptree_destroy(tree);
    puts("[PASS] bptree");
    return EXIT_SUCCESS;
}
