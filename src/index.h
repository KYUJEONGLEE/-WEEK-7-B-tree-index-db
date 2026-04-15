#ifndef INDEX_H
#define INDEX_H

#include "bptree.h"
#include "storage.h"

typedef struct {
    int row_index;
} RowRef;

typedef struct OffsetNode {
    int row_index;
    struct OffsetNode *next;
} OffsetNode;

typedef struct {
    long count;
    OffsetNode *head;
    OffsetNode *tail;
} OffsetList;

typedef struct {
    BPTree *id_tree;
    BPTree *win_tree;
} PlayerIndexSet;

int index_build_player_indexes(const TableData *table, PlayerIndexSet *out_indexes);
int index_insert_row(PlayerIndexSet *indexes, long long id,
                     long long game_win_count, int row_index);
int index_search_by_id(const PlayerIndexSet *indexes, long long id,
                       int *out_row_index);
const OffsetList *index_search_by_win_count(const PlayerIndexSet *indexes,
                                            long long game_win_count);
int index_collect_win_count_row_indexes(const PlayerIndexSet *indexes, const char *op,
                                        long long game_win_count,
                                        int **row_indexes, int *count);
void index_free_player_indexes(PlayerIndexSet *indexes);

#endif
