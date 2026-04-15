#ifndef PLAYER_INDEX_H
#define PLAYER_INDEX_H

#include "bptree.h"
#include "storage.h"

/*
 * 플레이어 전적 테이블 전용 B+ 트리 인덱스 매니저
 * ─────────────────────────────────────────────────
 * 이 모듈은 오직 두 컬럼만 인덱스로 관리한다:
 *   - id: unique PK → RowRef* (단일 offset)
 *   - game_win_count: secondary → OffsetList* (중복 key를 value list로 처리)
 *
 * B+ 트리 코어는 중복 key를 허용하지 않는다.
 * game_win_count의 중복 문제는 이 레이어에서 OffsetList로 해결한다.
 */

/* id tree의 value: 하나의 CSV row offset */
typedef struct {
    long offset;
} RowRef;

/* game_win_count tree의 value: 동일 key를 가진 여러 row의 offset 연결 리스트 */
typedef struct OffsetNode {
    long offset;
    struct OffsetNode *next;
} OffsetNode;

typedef struct {
    long count;
    OffsetNode *head;
    OffsetNode *tail;
} POffsetList;

/* 테이블당 인덱스 세트 */
typedef struct {
    BPTree *id_tree;
    BPTree *win_tree;
} PlayerIndexSet;

/*
 * TableData에서 id와 game_win_count 컬럼을 찾아 두 B+ 트리를 빌드한다.
 * 성공 시 SUCCESS, 실패 시 FAILURE를 반환한다.
 * 반환된 index_set은 호출자가 player_index_free()로 해제해야 한다.
 */
int player_index_build(const TableData *table, PlayerIndexSet *index_set);

/*
 * id B+ 트리에서 exact match 검색한다.
 * 반환값은 RowRef* (없으면 NULL).
 */
RowRef *player_index_search_by_id(const PlayerIndexSet *index_set, long long id);

/*
 * game_win_count B+ 트리에서 exact match 검색한다.
 * 반환값은 POffsetList* (없으면 NULL).
 */
POffsetList *player_index_search_by_win_count(const PlayerIndexSet *index_set,
                                              long long win_count);

/*
 * 새 row의 id, game_win_count, offset을 두 tree에 반영한다.
 * id duplicate는 실패 처리.
 * game_win_count는 기존 list append 또는 신규 key insert.
 */
int player_index_insert_row(PlayerIndexSet *index_set, long long id,
                            long long win_count, long offset);

/*
 * tree 노드뿐 아니라 RowRef, POffsetList, OffsetNode도 모두 해제한다.
 */
void player_index_free(PlayerIndexSet *index_set);

#endif
