#include "index.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int assert_true(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "[FAIL] %s\n", message);
        return FAILURE;
    }
    return SUCCESS;
}

static void free_table(TableData *table) {
    storage_free_table(table);
}

int main(void) {
    TableData table;
    PlayerIndexSet indexes;
    RowRef *ref;
    OffsetList *list;

    memset(&table, 0, sizeof(table));
    table.row_count = 3;
    table.col_count = 5;
    snprintf(table.columns[0], sizeof(table.columns[0]), "id");
    snprintf(table.columns[1], sizeof(table.columns[1]), "nickname");
    snprintf(table.columns[2], sizeof(table.columns[2]), "game_win_count");
    snprintf(table.columns[3], sizeof(table.columns[3]), "game_loss_count");
    snprintf(table.columns[4], sizeof(table.columns[4]), "total_game_count");

    table.rows = (char ***)calloc(3, sizeof(char **));
    table.offsets = (long *)calloc(3, sizeof(long));
    if (table.rows == NULL || table.offsets == NULL) {
        return EXIT_FAILURE;
    }

    table.offsets[0] = 10;
    table.offsets[1] = 30;
    table.offsets[2] = 50;

    table.rows[0] = (char **)calloc(5, sizeof(char *));
    table.rows[1] = (char **)calloc(5, sizeof(char *));
    table.rows[2] = (char **)calloc(5, sizeof(char *));
    if (table.rows[0] == NULL || table.rows[1] == NULL || table.rows[2] == NULL) {
        free_table(&table);
        return EXIT_FAILURE;
    }

    table.rows[0][0] = utils_strdup("1");
    table.rows[0][1] = utils_strdup("player_1");
    table.rows[0][2] = utils_strdup("10");
    table.rows[0][3] = utils_strdup("2");
    table.rows[0][4] = utils_strdup("12");
    table.rows[1][0] = utils_strdup("2");
    table.rows[1][1] = utils_strdup("player_2");
    table.rows[1][2] = utils_strdup("20");
    table.rows[1][3] = utils_strdup("3");
    table.rows[1][4] = utils_strdup("23");
    table.rows[2][0] = utils_strdup("3");
    table.rows[2][1] = utils_strdup("player_3");
    table.rows[2][2] = utils_strdup("10");
    table.rows[2][3] = utils_strdup("4");
    table.rows[2][4] = utils_strdup("14");

    if (assert_true(index_build_player_indexes(&table, &indexes) == SUCCESS,
                    "player indexes should build") != SUCCESS) {
        free_table(&table);
        return EXIT_FAILURE;
    }

    ref = index_search_by_id(&indexes, 2);
    list = index_search_by_win_count(&indexes, 10);
    if (assert_true(ref != NULL && ref->offset == 30,
                    "id index should return row offset") != SUCCESS ||
        assert_true(list != NULL && list->count == 2,
                    "win count index should keep duplicate offsets") != SUCCESS ||
        assert_true(index_insert_row(&indexes, 2, 99, 70) == FAILURE,
                    "duplicate id insert should fail") != SUCCESS ||
        assert_true(index_insert_row(&indexes, 4, 10, 70) == SUCCESS,
                    "new row should update both indexes") != SUCCESS) {
        index_free(&indexes);
        free_table(&table);
        return EXIT_FAILURE;
    }

    ref = index_search_by_id(&indexes, 4);
    list = index_search_by_win_count(&indexes, 10);
    if (assert_true(ref != NULL && ref->offset == 70,
                    "inserted id should be searchable") != SUCCESS ||
        assert_true(list != NULL && list->count == 3,
                    "win count list should grow after insert") != SUCCESS) {
        index_free(&indexes);
        free_table(&table);
        return EXIT_FAILURE;
    }

    index_free(&indexes);
    free_table(&table);
    puts("[PASS] index");
    return EXIT_SUCCESS;
}
