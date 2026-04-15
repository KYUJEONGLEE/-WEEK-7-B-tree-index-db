#ifndef STORAGE_H
#define STORAGE_H

#include "parser.h"

typedef struct {
    int row_count;
    int col_count;
    char columns[MAX_COLUMNS][MAX_IDENTIFIER_LEN];
    char ***rows;
    long *offsets;
} TableData;

/*
 * 테이블 CSV 파일에 행 하나를 추가한다.
 * 성공 시 SUCCESS, 실패 시 FAILURE를 반환한다.
 */
int storage_insert(const char *table_name, const InsertStatement *stmt);

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

/*
 * INSERT 결과 구조체.
 * executor가 INSERT 성공 직후 캐시된 인덱스에 즉시 반영할 수 있도록
 * 할당된 id, 승리/패배/총 횟수, 파일 오프셋을 함께 반환한다.
 */
typedef struct {
    long long assigned_id;
    int game_win_count;
    int game_loss_count;
    int total_game_count;
    long file_offset;
    int id_was_auto_generated;
} StorageInsertResult;

/*
 * INSERT 실행 후 결과 정보를 함께 반환하는 확장 함수.
 * 성공 시 SUCCESS, 실패 시 FAILURE를 반환한다.
 * result가 NULL이면 기존 storage_insert()와 동일하게 동작한다.
 */
int storage_insert_with_result(const char *table_name, const InsertStatement *stmt,
                               StorageInsertResult *result);

/*
 * meta 파일에서 next_id를 읽는다.
 * meta 파일이 없으면 CSV를 한 번 스캔하여 복구한다.
 *
 * 왜 meta에서 읽는가?
 *   1,000,000건 INSERT 시 매번 CSV 전체를 스캔하면 O(n^2)이 된다.
 *   meta 파일을 두면 O(1)에 next_id를 얻을 수 있다.
 */
long long storage_get_next_id_from_meta(const char *table_name);

/*
 * meta 파일에 next_id를 저장한다.
 */
int storage_save_next_id_to_meta(const char *table_name, long long next_id);

#endif
