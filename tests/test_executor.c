#include "executor.h"
#include "storage.h"

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

static void prepare_player_insert(SqlStatement *statement, const char *nickname,
                                  const char *wins, const char *losses) {
    memset(statement, 0, sizeof(*statement));
    statement->type = SQL_INSERT;
    snprintf(statement->insert.table_name, sizeof(statement->insert.table_name),
             "players");
    statement->insert.column_count = 3;
    snprintf(statement->insert.columns[0], sizeof(statement->insert.columns[0]),
             "nickname");
    snprintf(statement->insert.columns[1], sizeof(statement->insert.columns[1]),
             "game_win_count");
    snprintf(statement->insert.columns[2], sizeof(statement->insert.columns[2]),
             "game_loss_count");
    snprintf(statement->insert.values[0], sizeof(statement->insert.values[0]),
             "%s", nickname);
    snprintf(statement->insert.values[1], sizeof(statement->insert.values[1]),
             "%s", wins);
    snprintf(statement->insert.values[2], sizeof(statement->insert.values[2]),
             "%s", losses);
}

static void prepare_select(SqlStatement *statement, const char *column,
                           const char *value) {
    memset(statement, 0, sizeof(*statement));
    statement->type = SQL_SELECT;
    snprintf(statement->select.table_name, sizeof(statement->select.table_name),
             "players");
    statement->select.column_count = 1;
    snprintf(statement->select.columns[0], sizeof(statement->select.columns[0]),
             "nickname");
    statement->select.has_where = 1;
    snprintf(statement->select.where.column, sizeof(statement->select.where.column),
             "%s", column);
    snprintf(statement->select.where.op, sizeof(statement->select.where.op), "=");
    snprintf(statement->select.where.value, sizeof(statement->select.where.value),
             "%s", value);
}

static void prepare_delete(SqlStatement *statement, const char *nickname) {
    memset(statement, 0, sizeof(*statement));
    statement->type = SQL_DELETE;
    snprintf(statement->delete_stmt.table_name,
             sizeof(statement->delete_stmt.table_name), "players");
    statement->delete_stmt.has_where = 1;
    snprintf(statement->delete_stmt.where.column,
             sizeof(statement->delete_stmt.where.column), "nickname");
    snprintf(statement->delete_stmt.where.op,
             sizeof(statement->delete_stmt.where.op), "=");
    snprintf(statement->delete_stmt.where.value,
             sizeof(statement->delete_stmt.where.value), "%s", nickname);
}

int main(void) {
    SqlStatement statement;
    ExecStats stats;
    char ***rows;
    int row_count;
    int col_count;

    remove("data/players.csv");
    remove("data/players.meta");
    executor_reset_runtime_state();

    prepare_player_insert(&statement, "player_1", "10", "3");
    if (assert_true(executor_execute(&statement) == SUCCESS,
                    "executor should insert first player") != SUCCESS) {
        return EXIT_FAILURE;
    }

    prepare_player_insert(&statement, "player_2", "10", "5");
    if (assert_true(executor_execute(&statement) == SUCCESS,
                    "executor should insert second player") != SUCCESS) {
        return EXIT_FAILURE;
    }

    prepare_player_insert(&statement, "player_3", "20", "1");
    if (assert_true(executor_execute(&statement) == SUCCESS,
                    "executor should insert third player") != SUCCESS) {
        return EXIT_FAILURE;
    }

    rows = storage_select("players", &row_count, &col_count);
    if (assert_true(rows != NULL, "players should be readable") != SUCCESS ||
        assert_true(row_count == 3, "players row count should be 3") != SUCCESS ||
        assert_true(strcmp(rows[0][0], "1") == 0, "first id should be 1") != SUCCESS ||
        assert_true(strcmp(rows[0][4], "13") == 0,
                    "total_game_count should be wins + losses") != SUCCESS) {
        storage_free_rows(rows, row_count, col_count);
        return EXIT_FAILURE;
    }
    storage_free_rows(rows, row_count, col_count);

    prepare_select(&statement, "id", "2");
    if (assert_true(executor_execute_select_with_mode(&statement.select,
                                                      EXEC_MODE_NORMAL, 1,
                                                      &stats) == SUCCESS,
                    "id SELECT should execute") != SUCCESS ||
        assert_true(stats.plan_used == EXEC_PLAN_BPTREE_ID_LOOKUP,
                    "WHERE id = ? should use id B+ tree") != SUCCESS ||
        assert_true(stats.matched_rows == 1, "id lookup should match one row") != SUCCESS) {
        return EXIT_FAILURE;
    }

    prepare_select(&statement, "game_win_count", "10");
    if (assert_true(executor_execute_select_with_mode(&statement.select,
                                                      EXEC_MODE_NORMAL, 1,
                                                      &stats) == SUCCESS,
                    "win_count SELECT should execute") != SUCCESS ||
        assert_true(stats.plan_used == EXEC_PLAN_BPTREE_WIN_LOOKUP,
                    "WHERE game_win_count = ? should use win B+ tree") != SUCCESS ||
        assert_true(stats.matched_rows == 2,
                    "win_count lookup should match duplicate rows") != SUCCESS) {
        return EXIT_FAILURE;
    }

    if (assert_true(executor_get_index_cache_hit_count() >= 1,
                    "second indexed SELECT should reuse player index cache") != SUCCESS) {
        return EXIT_FAILURE;
    }

    prepare_select(&statement, "nickname", "player_3");
    if (assert_true(executor_execute_select_with_mode(&statement.select,
                                                      EXEC_MODE_NORMAL, 1,
                                                      &stats) == SUCCESS,
                    "nickname SELECT should execute") != SUCCESS ||
        assert_true(stats.plan_used == EXEC_PLAN_LINEAR_SCAN,
                    "nickname lookup should stay linear") != SUCCESS ||
        assert_true(stats.scanned_rows == 3,
                    "linear lookup should scan all rows") != SUCCESS) {
        return EXIT_FAILURE;
    }

    prepare_delete(&statement, "player_2");
    if (assert_true(executor_execute(&statement) == SUCCESS,
                    "delete should invalidate caches and succeed") != SUCCESS) {
        return EXIT_FAILURE;
    }

    prepare_select(&statement, "id", "2");
    if (assert_true(executor_execute_select_with_mode(&statement.select,
                                                      EXEC_MODE_NORMAL, 1,
                                                      &stats) == SUCCESS,
                    "id SELECT after delete should execute") != SUCCESS ||
        assert_true(stats.plan_used == EXEC_PLAN_BPTREE_ID_LOOKUP,
                    "id SELECT after delete should still use B+ tree") != SUCCESS ||
        assert_true(stats.matched_rows == 0,
                    "deleted id should not be found after cache rebuild") != SUCCESS) {
        return EXIT_FAILURE;
    }

    executor_reset_runtime_state();
    puts("[PASS] executor");
    return EXIT_SUCCESS;
}
