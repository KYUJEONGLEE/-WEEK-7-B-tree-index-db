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

static void prepare_player_insert(SqlStatement *statement, const char *table_name,
                                  const char *nickname, const char *wins,
                                  const char *losses) {
    memset(statement, 0, sizeof(*statement));
    statement->type = SQL_INSERT;
    snprintf(statement->insert.table_name, sizeof(statement->insert.table_name),
             "%s", table_name);
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

static void prepare_select(SelectStatement *stmt, const char *table_name,
                           const char *column, const char *value) {
    memset(stmt, 0, sizeof(*stmt));
    snprintf(stmt->table_name, sizeof(stmt->table_name), "%s", table_name);
    stmt->column_count = 1;
    snprintf(stmt->columns[0], sizeof(stmt->columns[0]), "nickname");
    stmt->has_where = 1;
    snprintf(stmt->where.column, sizeof(stmt->where.column), "%s", column);
    snprintf(stmt->where.op, sizeof(stmt->where.op), "=");
    snprintf(stmt->where.value, sizeof(stmt->where.value), "%s", value);
}

static void prepare_delete(SqlStatement *statement, const char *table_name,
                           const char *nickname) {
    memset(statement, 0, sizeof(*statement));
    statement->type = SQL_DELETE;
    snprintf(statement->delete_stmt.table_name,
             sizeof(statement->delete_stmt.table_name), "%s", table_name);
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
    SelectStatement select_stmt;
    ExecStats stats;
    char ***rows;
    int row_count;
    int col_count;

    remove("data/executor_players.csv");
    remove("data/executor_players.meta");
    executor_reset_runtime_state();

    prepare_player_insert(&statement, "executor_players", "player_1", "10", "2");
    if (assert_true(executor_execute(&statement) == SUCCESS,
                    "executor should insert first player") != SUCCESS) {
        return EXIT_FAILURE;
    }

    prepare_player_insert(&statement, "executor_players", "player_2", "20", "3");
    if (assert_true(executor_execute(&statement) == SUCCESS,
                    "executor should insert second player") != SUCCESS) {
        return EXIT_FAILURE;
    }

    prepare_player_insert(&statement, "executor_players", "player_3", "10", "4");
    if (assert_true(executor_execute(&statement) == SUCCESS,
                    "executor should insert third player") != SUCCESS) {
        return EXIT_FAILURE;
    }

    prepare_select(&select_stmt, "executor_players", "id", "2");
    if (assert_true(executor_execute_select_with_options(
                        &select_stmt, EXEC_MODE_NORMAL, 1, &stats) == SUCCESS,
                    "id SELECT should execute") != SUCCESS ||
        assert_true(stats.plan_used == EXEC_PLAN_BPTREE_ID_LOOKUP,
                    "WHERE id should use B+ tree plan") != SUCCESS ||
        assert_true(stats.matched_rows == 1,
                    "id lookup should match one row") != SUCCESS) {
        return EXIT_FAILURE;
    }

    prepare_select(&select_stmt, "executor_players", "game_win_count", "10");
    if (assert_true(executor_execute_select_with_options(
                        &select_stmt, EXEC_MODE_NORMAL, 1, &stats) == SUCCESS,
                    "win count SELECT should execute") != SUCCESS ||
        assert_true(stats.plan_used == EXEC_PLAN_BPTREE_WIN_LOOKUP,
                    "WHERE game_win_count should use B+ tree plan") != SUCCESS ||
        assert_true(stats.matched_rows == 2,
                    "win count lookup should return duplicate-key rows") != SUCCESS ||
        assert_true(executor_get_index_cache_hit_count() >= 1,
                    "repeated indexed SELECT should reuse player index cache") != SUCCESS) {
        return EXIT_FAILURE;
    }

    prepare_select(&select_stmt, "executor_players", "nickname", "player_2");
    if (assert_true(executor_execute_select_with_options(
                        &select_stmt, EXEC_MODE_NORMAL, 1, &stats) == SUCCESS,
                    "nickname SELECT should execute") != SUCCESS ||
        assert_true(stats.plan_used == EXEC_PLAN_LINEAR_SCAN,
                    "nickname should use linear scan") != SUCCESS ||
        assert_true(stats.scanned_rows == 3,
                    "linear scan should inspect all rows") != SUCCESS) {
        return EXIT_FAILURE;
    }

    prepare_delete(&statement, "executor_players", "player_2");
    if (assert_true(executor_execute(&statement) == SUCCESS,
                    "executor should delete player_2") != SUCCESS) {
        return EXIT_FAILURE;
    }

    rows = storage_select("executor_players", &row_count, &col_count);
    if (assert_true(rows != NULL, "storage_select should read player table") != SUCCESS ||
        assert_true(row_count == 2, "delete should leave two rows") != SUCCESS) {
        storage_free_rows(rows, row_count, col_count);
        return EXIT_FAILURE;
    }
    storage_free_rows(rows, row_count, col_count);

    executor_reset_runtime_state();
    puts("[PASS] executor");
    return EXIT_SUCCESS;
}
