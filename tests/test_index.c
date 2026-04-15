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

static char *copy_text(const char *text) {
    char *copy = (char *)malloc(strlen(text) + 1);
    if (copy != NULL) {
        strcpy(copy, text);
    }
    return copy;
}

static int fill_row(TableData *table, int row, const char *id,
                    const char *nickname, const char *wins,
                    const char *losses, const char *total) {
    table->rows[row] = (char **)malloc(5 * sizeof(char *));
    if (table->rows[row] == NULL) {
        return FAILURE;
    }

    table->rows[row][0] = copy_text(id);
    table->rows[row][1] = copy_text(nickname);
    table->rows[row][2] = copy_text(wins);
    table->rows[row][3] = copy_text(losses);
    table->rows[row][4] = copy_text(total);
    return table->rows[row][0] != NULL && table->rows[row][1] != NULL &&
           table->rows[row][2] != NULL && table->rows[row][3] != NULL &&
           table->rows[row][4] != NULL ? SUCCESS : FAILURE;
}

int main(void) {
    TableData table;
    PlayerIndexSet indexes;
    const OffsetList *list;
    int row_index;

    memset(&table, 0, sizeof(table));
    table.row_count = 4;
    table.col_count = 5;
    snprintf(table.columns[0], sizeof(table.columns[0]), "id");
    snprintf(table.columns[1], sizeof(table.columns[1]), "nickname");
    snprintf(table.columns[2], sizeof(table.columns[2]), "game_win_count");
    snprintf(table.columns[3], sizeof(table.columns[3]), "game_loss_count");
    snprintf(table.columns[4], sizeof(table.columns[4]), "total_game_count");

    table.rows = (char ***)calloc((size_t)table.row_count, sizeof(char **));
    table.offsets = (long *)malloc((size_t)table.row_count * sizeof(long));
    if (table.rows == NULL || table.offsets == NULL) {
        return EXIT_FAILURE;
    }

    table.offsets[0] = 100;
    table.offsets[1] = 200;
    table.offsets[2] = 300;
    table.offsets[3] = 400;
    if (fill_row(&table, 0, "1", "player_1", "10", "2", "12") != SUCCESS ||
        fill_row(&table, 1, "2", "player_2", "20", "3", "23") != SUCCESS ||
        fill_row(&table, 2, "3", "player_3", "10", "4", "14") != SUCCESS ||
        fill_row(&table, 3, "4", "player_4", "30", "5", "35") != SUCCESS) {
        storage_free_table(&table);
        return EXIT_FAILURE;
    }

    if (assert_true(index_build_player_indexes(&table, &indexes) == SUCCESS,
                    "index build should succeed") != SUCCESS ||
        assert_true(index_search_by_id(&indexes, 2, &row_index) == SUCCESS,
                    "id search should find row") != SUCCESS ||
        assert_true(row_index == 1, "id search should return row index") != SUCCESS) {
        storage_free_table(&table);
        return EXIT_FAILURE;
    }

    list = index_search_by_win_count(&indexes, 10);
    if (assert_true(list != NULL, "win_count search should return list") != SUCCESS ||
        assert_true(list->count == 2, "duplicate win_count should map to two rows") != SUCCESS ||
        assert_true(index_insert_row(&indexes, 2, 99, 999) == FAILURE,
                    "duplicate id insert should fail") != SUCCESS ||
        assert_true(index_insert_row(&indexes, 5, 10, 4) == SUCCESS,
                    "new row should update both indexes") != SUCCESS ||
        assert_true(index_search_by_id(&indexes, 5, &row_index) == SUCCESS &&
                    row_index == 4,
                    "new id should be searchable") != SUCCESS ||
        assert_true(index_search_by_win_count(&indexes, 10)->count == 3,
                    "new duplicate win_count should append to list") != SUCCESS) {
        index_free_player_indexes(&indexes);
        storage_free_table(&table);
        return EXIT_FAILURE;
    }

    index_free_player_indexes(&indexes);
    storage_free_table(&table);
    puts("[PASS] index");
    return EXIT_SUCCESS;
}
