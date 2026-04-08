#ifndef INDEX_H
#define INDEX_H

#include "storage.h"

typedef struct {
    long *items;
    int count;
    int capacity;
} OffsetList;

typedef struct HashNode {
    char *key;
    OffsetList offsets;
    struct HashNode *next;
} HashNode;

typedef struct {
    int bucket_count;
    HashNode **buckets;
} EqualityIndex;

typedef struct {
    char key[MAX_VALUE_LEN];
    long offset;
} RangeEntry;

typedef struct {
    RangeEntry *entries;
    int count;
} RangeIndex;

typedef struct {
    int column_index;
    EqualityIndex equality;
    RangeIndex range;
} TableIndex;

/*
 * Build transient in-memory equality and range indexes for one table column.
 * The index is rebuilt during query execution and is not persisted on disk.
 */
int index_build(const TableData *table, int column_index, TableIndex *out_index);

/*
 * Query the equality index.
 * Caller owns the returned offsets array.
 */
int index_query_equals(const TableIndex *index, const char *value,
                       long **offsets, int *count);

/*
 * Query the range index for !=, >, <, >=, <=.
 * Caller owns the returned offsets array.
 */
int index_query_range(const TableIndex *index, const char *op, const char *value,
                      long **offsets, int *count);

/*
 * Release all dynamic memory owned by the index.
 */
void index_free(TableIndex *index);

#endif
