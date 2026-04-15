#include "executor.h"
#include "parser.h"
#include "storage.h"
#include "tokenizer.h"
#include "utils.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * 문자열에서 연속된 공백을 건너뛰고 다음 유효 위치를 찾는다.
 * 반환값은 SQL 문이 시작될 수 있는 다음 인덱스다.
 */
static size_t main_skip_whitespace(const char *text, size_t index)
{
    while (text[index] != '\0' && isspace((unsigned char)text[index]))
    {
        index++;
    }
    return index;
}

/*
 * 완전한 SQL 문 하나를 파싱하고 실행한다.
 * 빈 문장이거나 정상 실행되면 SUCCESS를 반환한다.
 */
static int main_parse_sql_statement(const char *sql, SqlStatement *statement,
                                    int *is_empty)
{
    Token *tokens;
    int token_count;
    char *working_sql;
    int status;

    if (sql == NULL || statement == NULL || is_empty == NULL)
    {
        return FAILURE;
    }

    *is_empty = 0;
    working_sql = utils_strdup(sql);
    if (working_sql == NULL)
    {
        return FAILURE;
    }

    utils_trim(working_sql);
    if (working_sql[0] == '\0')
    {
        free(working_sql);
        *is_empty = 1;
        return SUCCESS;
    }

    tokens = tokenizer_tokenize(working_sql, &token_count);
    if (tokens == NULL || token_count == 0)
    {
        free(tokens);
        free(working_sql);
        return FAILURE;
    }

    status = parser_parse(tokens, token_count, statement);

    free(tokens);
    free(working_sql);
    return status;
}

/*
 * 완전한 SQL 문 하나를 파싱하고 실행한다.
 * 빈 문장이거나 정상 실행되면 SUCCESS를 반환한다.
 */
static int main_process_sql_statement(const char *sql)
{
    SqlStatement statement;
    int is_empty;
    int status;

    status = main_parse_sql_statement(sql, &statement, &is_empty);
    if (status != SUCCESS || is_empty)
    {
        return status;
    }

    return executor_execute(&statement);
}

/*
 * `.sql` 파일을 읽어 세미콜론 기준으로 문장을 나눈 뒤 순서대로 실행한다.
 * 파일 읽기나 내부 메모리 할당에 실패하지 않으면 SUCCESS를 반환한다.
 */
static int main_run_file_mode(const char *path)
{
    char *content;
    size_t start;
    int terminator_index;
    char *statement;
    char *remaining;

    content = utils_read_file(path);
    if (content == NULL)
    {
        return FAILURE;
    }

    start = 0;
    while (content[start] != '\0')
    {
        start = main_skip_whitespace(content, start);
        if (content[start] == '\0')
        {
            break;
        }

        terminator_index = utils_find_statement_terminator(content, start);
        if (terminator_index == FAILURE)
        {
            remaining = utils_strdup(content + start);
            if (remaining == NULL)
            {
                free(content);
                return FAILURE;
            }
            utils_trim(remaining);
            if (remaining[0] != '\0')
            {
                fprintf(stderr, "Error: Missing semicolon at end of SQL statement.\n");
            }
            free(remaining);
            break;
        }

        statement = utils_substring(content, start,
                                    (size_t)terminator_index - start + 1);
        if (statement == NULL)
        {
            free(content);
            return FAILURE;
        }

        main_process_sql_statement(statement);
        free(statement);
        start = (size_t)terminator_index + 1;
    }

    free(content);
    return SUCCESS;
}

/*
 * 지원 테이블 전용 bulk 파일 모드.
 * SQL 문법은 그대로 파싱하되 CSV 파일은 한 번만 열고 meta는 마지막에 한 번만 갱신한다.
 */
static int main_run_bulk_insert_mode(const char *path, int silent)
{
    char *content;
    size_t start;
    int terminator_index;
    char *statement_sql;
    char *remaining;
    SqlStatement statement;
    PlayersBulkInsert players_bulk;
    InsertTestRecordsBulkInsert records_bulk;
    int is_empty;
    int bulk_started;
    int bulk_kind;
    int status;

    content = utils_read_file(path);
    if (content == NULL)
    {
        return FAILURE;
    }

    memset(&players_bulk, 0, sizeof(players_bulk));
    memset(&records_bulk, 0, sizeof(records_bulk));
    bulk_started = 0;
    bulk_kind = 0;
    status = SUCCESS;

    start = 0;
    while (content[start] != '\0')
    {
        start = main_skip_whitespace(content, start);
        if (content[start] == '\0')
        {
            break;
        }

        terminator_index = utils_find_statement_terminator(content, start);
        if (terminator_index == FAILURE)
        {
            remaining = utils_strdup(content + start);
            if (remaining == NULL)
            {
                status = FAILURE;
                break;
            }
            utils_trim(remaining);
            if (remaining[0] != '\0')
            {
                fprintf(stderr, "Error: Missing semicolon at end of SQL statement.\n");
                status = FAILURE;
            }
            free(remaining);
            break;
        }

        statement_sql = utils_substring(content, start,
                                        (size_t)terminator_index - start + 1);
        if (statement_sql == NULL)
        {
            status = FAILURE;
            break;
        }

        status = main_parse_sql_statement(statement_sql, &statement, &is_empty);
        free(statement_sql);
        if (status != SUCCESS)
        {
            break;
        }

        if (!is_empty)
        {
            if (statement.type != SQL_INSERT)
            {
                fprintf(stderr,
                        "Error: --bulk-insert only supports INSERT statements.\n");
                status = FAILURE;
                break;
            }

            if (!utils_equals_ignore_case(statement.insert.table_name, "players") &&
                !utils_equals_ignore_case(statement.insert.table_name,
                                          "insert_test_records"))
            {
                fprintf(stderr,
                        "Error: --bulk-insert supports players and insert_test_records tables only.\n");
                status = FAILURE;
                break;
            }

            if (!bulk_started)
            {
                if (utils_equals_ignore_case(statement.insert.table_name, "players"))
                {
                    if (storage_players_bulk_begin(statement.insert.table_name,
                                                   &players_bulk) != SUCCESS)
                    {
                        status = FAILURE;
                        break;
                    }
                    bulk_kind = 1;
                }
                else
                {
                    if (storage_insert_test_records_bulk_begin(
                            statement.insert.table_name,
                            &records_bulk) != SUCCESS)
                    {
                        status = FAILURE;
                        break;
                    }
                    bulk_kind = 2;
                }
                bulk_started = 1;
            }

            if ((bulk_kind == 1 &&
                 storage_players_bulk_insert(&players_bulk,
                                             &statement.insert) != SUCCESS) ||
                (bulk_kind == 2 &&
                 storage_insert_test_records_bulk_insert(
                     &records_bulk, &statement.insert) != SUCCESS))
            {
                status = FAILURE;
                break;
            }
        }

        start = (size_t)terminator_index + 1;
    }

    free(content);

    if (bulk_started)
    {
        if (status == SUCCESS)
        {
            if (bulk_kind == 1)
            {
                status = storage_players_bulk_finish(&players_bulk);
            }
            else
            {
                status = storage_insert_test_records_bulk_finish(&records_bulk);
            }

            if (status == SUCCESS && !silent)
            {
                printf("[성공] %s 테이블에 %ld행을 bulk INSERT했습니다.\n",
                       bulk_kind == 1 ? players_bulk.table_name : records_bulk.table_name,
                       bulk_kind == 1 ? players_bulk.inserted_count :
                                        records_bulk.inserted_count);
            }
        }
        else
        {
            if (bulk_kind == 1)
            {
                storage_players_bulk_abort(&players_bulk);
            }
            else if (bulk_kind == 2)
            {
                storage_insert_test_records_bulk_abort(&records_bulk);
            }
        }
    }

    return status;
}

/*
 * 한 줄 입력을 공백 제거 후 제어 키워드와 비교한다.
 * 일치하면 1, 아니면 0을 반환한다.
 */
static int main_trimmed_equals(const char *line, const char *keyword)
{
    char *copy;
    int result;

    copy = utils_strdup(line);
    if (copy == NULL)
    {
        return 0;
    }

    utils_trim(copy);
    result = utils_equals_ignore_case(copy, keyword);
    free(copy);
    return result;
}

/*
 * REPL 버퍼에서 처리한 SQL 문을 제거하고 남은 문자열만 유지한다.
 * 성공 시 갱신된 버퍼 소유권은 계속 호출자에게 있다.
 */
static int main_replace_buffer_with_remainder(char **buffer, size_t *length,
                                              size_t *capacity, int end_index)
{
    char *remainder;
    size_t remainder_length;

    if (buffer == NULL || *buffer == NULL || length == NULL || capacity == NULL)
    {
        return FAILURE;
    }

    remainder = utils_strdup(*buffer + end_index + 1);
    if (remainder == NULL)
    {
        return FAILURE;
    }

    utils_trim(remainder);
    free(*buffer);
    *buffer = remainder;
    remainder_length = strlen(remainder);
    *length = remainder_length;
    *capacity = remainder_length + 1;
    return SUCCESS;
}

/*
 * 사용자가 종료하거나 EOF가 올 때까지 대화형 SQL 셸을 실행한다.
 * 정상 종료면 SUCCESS, 메모리 할당 실패면 FAILURE를 반환한다.
 */
static int main_run_repl_mode(void)
{
    char line[MAX_SQL_LENGTH];
    char *buffer;
    size_t buffer_length;
    size_t buffer_capacity;
    int terminator_index;
    char *statement;

    buffer = NULL;
    buffer_length = 0;
    buffer_capacity = 0;

    while (1)
    {
        printf("%s", buffer_length == 0 ? "SQL> " : "...> ");
        fflush(stdout);

        if (fgets(line, sizeof(line), stdin) == NULL)
        {
            if (buffer != NULL && buffer[0] != '\0')
            {
                fprintf(stderr, "Error: Incomplete SQL statement before EOF.\n");
            }
            break;
        }

        if (buffer_length == 0 &&
            (main_trimmed_equals(line, "exit") || main_trimmed_equals(line, "quit")))
        {
            break;
        }

        if (utils_append_buffer(&buffer, &buffer_length, &buffer_capacity, line) != SUCCESS)
        {
            free(buffer);
            return FAILURE;
        }

        while (buffer != NULL &&
               (terminator_index = utils_find_statement_terminator(buffer, 0)) != FAILURE)
        {
            statement = utils_substring(buffer, 0, (size_t)terminator_index + 1);
            if (statement == NULL)
            {
                free(buffer);
                return FAILURE;
            }

            main_process_sql_statement(statement);
            free(statement);

            if (main_replace_buffer_with_remainder(&buffer, &buffer_length,
                                                   &buffer_capacity,
                                                   terminator_index) != SUCCESS)
            {
                free(buffer);
                return FAILURE;
            }

            if (buffer_length == 0)
            {
                free(buffer);
                buffer = NULL;
                buffer_capacity = 0;
                break;
            }
        }
    }

    free(buffer);
    puts("Bye.");
    return SUCCESS;
}

/*
 * argv에 따라 파일 모드 또는 REPL 모드를 선택하고 종료 전에 파서 캐시를 정리한다.
 * 정상 종료면 EXIT_SUCCESS, 아니면 EXIT_FAILURE를 반환한다.
 */
int main(int argc, char *argv[])
{
    int status;
    const char *sql_file;
    int silent;
    int summary_only;
    int bulk_insert;
    ExecMode mode;
    int i;

    sql_file = NULL;
    silent = 0;
    summary_only = 0;
    bulk_insert = 0;
    mode = EXEC_MODE_NORMAL;

    for (i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--silent") == 0)
        {
            silent = 1;
        }
        else if (strcmp(argv[i], "--summary-only") == 0)
        {
            summary_only = 1;
        }
        else if (strcmp(argv[i], "--bulk-insert") == 0)
        {
            bulk_insert = 1;
        }
        else if (strcmp(argv[i], "--force-linear") == 0)
        {
            mode = EXEC_MODE_FORCE_LINEAR;
        }
        else if (strcmp(argv[i], "--force-id-index") == 0)
        {
            mode = EXEC_MODE_FORCE_ID_INDEX;
        }
        else if (strcmp(argv[i], "--force-win-index") == 0)
        {
            mode = EXEC_MODE_FORCE_WIN_INDEX;
        }
        else if (sql_file == NULL)
        {
            sql_file = argv[i];
        }
        else
        {
            fprintf(stderr,
                    "Usage: %s [--silent] [--summary-only] [--bulk-insert] [--force-linear|--force-id-index|--force-win-index] [sql_file]\n",
                    argv[0]);
            return EXIT_FAILURE;
        }
    }

    executor_set_silent(silent);
    executor_set_summary_only(summary_only);
    executor_set_mode(mode);

    if (bulk_insert && sql_file == NULL)
    {
        fprintf(stderr, "Error: --bulk-insert requires a SQL file.\n");
        status = FAILURE;
    }
    else if (bulk_insert)
    {
        status = main_run_bulk_insert_mode(sql_file, silent);
    }
    else if (sql_file != NULL)
    {
        status = main_run_file_mode(sql_file);
    }
    else
    {
        status = main_run_repl_mode();
    }

    executor_reset_runtime_state();
    tokenizer_cleanup_cache();
    return status == SUCCESS ? EXIT_SUCCESS : EXIT_FAILURE;
}
