#ifndef UTILS_H
#define UTILS_H

#include <stddef.h>
#include <stdio.h>

#define SUCCESS 0
#define FAILURE -1

#define MAX_SQL_LENGTH 8192
#define MAX_TOKEN_VALUE 256
#define MAX_IDENTIFIER_LEN 64
#define MAX_COLUMNS 32
#define MAX_VALUE_LEN 256
#define MAX_PATH_LEN 256
#define MAX_CSV_LINE_LENGTH 16384
#define INITIAL_TOKEN_CAPACITY 16
#define INITIAL_ROW_CAPACITY 16
#define HASH_BUCKET_COUNT 101

/*
 * Duplicate a string with dynamic allocation.
 * Returns a newly allocated string on success, NULL on failure.
 * Caller owns the returned memory.
 */
char *utils_strdup(const char *src);

/*
 * Copy src into dest while always null-terminating the destination.
 * Returns SUCCESS on success, FAILURE if truncation or invalid input occurs.
 */
int utils_safe_strcpy(char *dest, size_t dest_size, const char *src);

/*
 * Trim leading and trailing whitespace in-place.
 */
void utils_trim(char *text);

/*
 * Convert src to uppercase and store it in dest.
 * Returns SUCCESS on success, FAILURE on invalid input or truncation.
 */
int utils_to_upper_copy(const char *src, char *dest, size_t dest_size);

/*
 * Return 1 if strings are equal ignoring case, otherwise 0.
 */
int utils_equals_ignore_case(const char *lhs, const char *rhs);

/*
 * Return 1 when text is a supported SQL keyword, otherwise 0.
 */
int utils_is_sql_keyword(const char *text);

/*
 * Return 1 if text represents a valid integer, otherwise 0.
 */
int utils_is_integer(const char *text);

/*
 * Parse an integer string. Caller should validate with utils_is_integer first.
 */
long long utils_parse_integer(const char *text);

/*
 * Compare two SQL literal values.
 * Numeric strings are compared numerically when both sides are integers.
 * Otherwise, values are compared lexicographically.
 * Returns <0, 0, >0 like strcmp.
 */
int utils_compare_values(const char *lhs, const char *rhs);

/*
 * Read an entire text file into memory.
 * Returns a newly allocated buffer on success, NULL on failure.
 * Caller owns the returned memory.
 */
char *utils_read_file(const char *path);

/*
 * Append suffix to a dynamically managed buffer.
 * Returns SUCCESS on success, FAILURE on allocation error.
 */
int utils_append_buffer(char **buffer, size_t *length, size_t *capacity,
                        const char *suffix);

/*
 * Return the index of the next SQL statement terminator outside single quotes.
 * Returns FAILURE when no terminator exists from start_index onward.
 */
int utils_find_statement_terminator(const char *text, size_t start_index);

/*
 * Return 1 if the buffer contains a complete SQL statement, otherwise 0.
 */
int utils_has_statement_terminator(const char *text);

/*
 * Create a new dynamically allocated substring.
 * Caller owns the returned memory.
 */
char *utils_substring(const char *text, size_t start, size_t length);

/*
 * Return the terminal display width of UTF-8 text.
 * ASCII characters count as 1, common CJK wide characters count as 2.
 */
int utils_display_width(const char *text);

/*
 * Print text and pad trailing spaces until the requested display width is met.
 */
void utils_print_padded(FILE *stream, const char *text, int target_width);

#endif
