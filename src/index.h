#ifndef INDEX_H
#define INDEX_H

#include "bptree.h"
#include "storage.h"

typedef struct {
    long offset;
} RowRef;

typedef struct OffsetNode {
    long offset;
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
    int has_id_index;
    int has_win_index;
} PlayerIndexSet;

int index_build_player_indexes(const TableData *table, PlayerIndexSet *indexes);
RowRef *index_search_by_id(PlayerIndexSet *indexes, long long id);
OffsetList *index_search_by_win_count(PlayerIndexSet *indexes, long long win_count);
int index_insert_row(PlayerIndexSet *indexes, long long id,
                     long long win_count, long offset);
void index_free(PlayerIndexSet *indexes);

#endif
