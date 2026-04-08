#ifndef SOFT_PARSER_H
#define SOFT_PARSER_H

#include "utils.h"

typedef enum {
    TOKEN_KEYWORD,
    TOKEN_IDENTIFIER,
    TOKEN_INT_LITERAL,
    TOKEN_STR_LITERAL,
    TOKEN_OPERATOR,
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_COMMA,
    TOKEN_SEMICOLON,
    TOKEN_UNKNOWN
} TokenType;

typedef struct {
    TokenType type;
    char value[MAX_TOKEN_VALUE];
} Token;

/*
 * Convert raw SQL text into a dynamically allocated token array.
 * On success, returns the token array and stores the token count.
 * Caller owns the returned memory and must free it with free().
 */
Token *soft_parse(const char *sql, int *token_count);

/*
 * Release all cached tokenized SQL statements held by the soft parser.
 */
void soft_parser_cleanup_cache(void);

/*
 * Return the number of cached SQL statements currently stored.
 */
int soft_parser_get_cache_entry_count(void);

/*
 * Return the number of cache hits since the last cleanup.
 */
int soft_parser_get_cache_hit_count(void);

/*
 * Return a human-readable name for a token type.
 */
const char *soft_parser_token_type_name(TokenType type);

#endif
