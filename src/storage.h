#ifndef STORAGE_H
#define STORAGE_H

#include "parser.h"

#include <stdio.h>

typedef struct {
    int row_count;
    int col_count;
    char columns[MAX_COLUMNS][MAX_IDENTIFIER_LEN];
    char ***rows;
    long *offsets;
} TableData;

typedef struct {
    FILE *fp;
    char table_name[MAX_IDENTIFIER_LEN];
    long long next_id;
    long inserted_count;
    int active;
} PlayersBulkInsert;

typedef struct {
    FILE *fp;
    char table_name[MAX_IDENTIFIER_LEN];
    long long next_id;
    long inserted_count;
    int active;
} InsertTestRecordsBulkInsert;

/*
 * 테이블 CSV 파일에 행 하나를 추가한다.
 * 성공 시 SUCCESS, 실패 시 FAILURE를 반환한다.
 */
int storage_insert(const char *table_name, const InsertStatement *stmt);

/*
 * players 전용 bulk INSERT 세션을 시작한다.
 * CSV 파일은 한 번만 열고, meta는 finish 시점에 한 번만 갱신한다.
 */
int storage_players_bulk_begin(const char *table_name, PlayersBulkInsert *bulk);

/*
 * 열린 players bulk 세션에 INSERT 한 행을 append한다.
 */
int storage_players_bulk_insert(PlayersBulkInsert *bulk,
                                const InsertStatement *stmt);

/*
 * bulk INSERT 세션을 정상 종료하고 meta 파일을 갱신한다.
 */
int storage_players_bulk_finish(PlayersBulkInsert *bulk);

/*
 * bulk INSERT 세션을 닫는다. 이미 쓴 row가 있으면 meta를 현재 next_id로 맞춘다.
 */
void storage_players_bulk_abort(PlayersBulkInsert *bulk);

/*
 * insert_test_records 전용 bulk INSERT 세션.
 */
int storage_insert_test_records_bulk_begin(const char *table_name,
                                           InsertTestRecordsBulkInsert *bulk);
int storage_insert_test_records_bulk_insert(InsertTestRecordsBulkInsert *bulk,
                                            const InsertStatement *stmt);
int storage_insert_test_records_bulk_finish(InsertTestRecordsBulkInsert *bulk);
void storage_insert_test_records_bulk_abort(InsertTestRecordsBulkInsert *bulk);

/*
 * 테이블 CSV 파일에서 조건에 맞는 행을 삭제한다.
 * WHERE가 없으면 헤더를 제외한 모든 데이터 행을 삭제한다.
 * jungle_menu 테이블은 같은 slot_key를 공유하는 메뉴 묶음을 함께 삭제한다.
 * 성공 시 SUCCESS, 실패 시 FAILURE를 반환한다.
 */
int storage_delete(const char *table_name, const DeleteStatement *stmt,
                   int *deleted_count);

/*
 * 테이블의 모든 행을 새로 할당한 3차원 배열로 읽어온다.
 * 반환된 메모리는 호출자가 storage_free_rows()로 해제해야 한다.
 */
char ***storage_select(const char *table_name, int *row_count, int *col_count);

/*
 * 테이블 CSV 파일의 헤더 행을 읽는다.
 * 성공 시 SUCCESS, 읽을 수 없으면 FAILURE를 반환한다.
 */
int storage_get_columns(const char *table_name, char columns[][MAX_IDENTIFIER_LEN],
                        int *col_count);

/*
 * 헤더, 데이터 행, 바이트 오프셋을 포함한 전체 테이블을 메모리로 읽는다.
 * 할당된 멤버는 호출자가 storage_free_table()로 해제해야 한다.
 */
int storage_load_table(const char *table_name, TableData *table);

/*
 * 바이트 오프셋을 이용해 행 하나를 읽는다.
 * 반환된 행은 호출자가 storage_free_row()로 해제해야 한다.
 */
int storage_read_row_at_offset(const char *table_name, long offset, int expected_col_count,
                               char ***out_row);

/*
 * storage_read_row_at_offset()로 할당한 행 하나를 해제한다.
 */
void storage_free_row(char **row, int col_count);

/*
 * storage_select()가 반환한 행 배열을 해제한다.
 */
void storage_free_rows(char ***rows, int row_count, int col_count);

/*
 * TableData 구조체가 소유한 동적 메모리를 모두 해제한다.
 */
void storage_free_table(TableData *table);

#endif
