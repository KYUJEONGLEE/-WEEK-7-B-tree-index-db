#include "soft_parser.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct SoftParserCacheEntry {
    char *sql;
    Token *tokens;
    int token_count;
    struct SoftParserCacheEntry *next;
} SoftParserCacheEntry;

#define SOFT_PARSER_CACHE_LIMIT 64

static SoftParserCacheEntry *soft_parser_cache_head = NULL;
static int soft_parser_cache_entry_count = 0;
static int soft_parser_cache_hit_count = 0;

static void soft_parser_free_cache_entry(SoftParserCacheEntry *entry) {
    if (entry == NULL) {
        return;
    }

    free(entry->sql);
    free(entry->tokens);
    free(entry);
}

static Token *soft_parser_clone_tokens(const Token *tokens, int token_count) {
    Token *copy;

    if (tokens == NULL || token_count <= 0) {
        return NULL;
    }

    copy = (Token *)malloc((size_t)token_count * sizeof(Token));
    if (copy == NULL) {
        fprintf(stderr, "Error: Failed to allocate memory for soft parser cache.\n");
        return NULL;
    }

    memcpy(copy, tokens, (size_t)token_count * sizeof(Token));
    return copy;
}

static void soft_parser_evict_oldest_cache_entry(void) {
    SoftParserCacheEntry *previous;
    SoftParserCacheEntry *entry;

    if (soft_parser_cache_head == NULL) {
        return;
    }

    previous = NULL;
    entry = soft_parser_cache_head;
    while (entry->next != NULL) {
        previous = entry;
        entry = entry->next;
    }

    if (previous == NULL) {
        soft_parser_cache_head = NULL;
    } else {
        previous->next = NULL;
    }

    soft_parser_free_cache_entry(entry);
    soft_parser_cache_entry_count--;
}

static Token *soft_parser_lookup_cache(const char *sql, int *token_count) {
    SoftParserCacheEntry *entry;
    SoftParserCacheEntry *previous;
    Token *copy;

    if (sql == NULL || token_count == NULL) {
        return NULL;
    }

    previous = NULL;
    entry = soft_parser_cache_head;
    while (entry != NULL) {
        if (strcmp(entry->sql, sql) == 0) {
            if (previous != NULL) {
                previous->next = entry->next;
                entry->next = soft_parser_cache_head;
                soft_parser_cache_head = entry;
            }

            copy = soft_parser_clone_tokens(entry->tokens, entry->token_count);
            if (copy == NULL) {
                return NULL;
            }

            *token_count = entry->token_count;
            soft_parser_cache_hit_count++;
            return copy;
        }

        previous = entry;
        entry = entry->next;
    }

    return NULL;
}

static int soft_parser_store_cache(const char *sql, const Token *tokens,
                                   int token_count) {
    SoftParserCacheEntry *entry;

    if (sql == NULL || tokens == NULL || token_count <= 0) {
        return FAILURE;
    }

    entry = (SoftParserCacheEntry *)calloc(1, sizeof(SoftParserCacheEntry));
    if (entry == NULL) {
        fprintf(stderr, "Error: Failed to allocate memory for soft parser cache.\n");
        return FAILURE;
    }

    entry->sql = utils_strdup(sql);
    if (entry->sql == NULL) {
        free(entry);
        return FAILURE;
    }

    entry->tokens = soft_parser_clone_tokens(tokens, token_count);
    if (entry->tokens == NULL) {
        free(entry->sql);
        free(entry);
        return FAILURE;
    }

    entry->token_count = token_count;
    entry->next = soft_parser_cache_head;
    soft_parser_cache_head = entry;
    soft_parser_cache_entry_count++;

    if (soft_parser_cache_entry_count > SOFT_PARSER_CACHE_LIMIT) {
        soft_parser_evict_oldest_cache_entry();
    }

    return SUCCESS;
}

static int soft_parser_append_token(Token **tokens, int *count, int *capacity,
                                    TokenType type, const char *value) {
    Token *new_tokens;

    if (tokens == NULL || count == NULL || capacity == NULL || value == NULL) {
        return FAILURE;
    }

    if (*tokens == NULL) {
        *capacity = INITIAL_TOKEN_CAPACITY;
        *tokens = (Token *)malloc((size_t)(*capacity) * sizeof(Token));
        if (*tokens == NULL) {
            fprintf(stderr, "Error: Failed to allocate memory for tokens.\n");
            return FAILURE;
        }
    } else if (*count >= *capacity) {
        *capacity *= 2;
        new_tokens = (Token *)realloc(*tokens, (size_t)(*capacity) * sizeof(Token));
        if (new_tokens == NULL) {
            fprintf(stderr, "Error: Failed to allocate memory for tokens.\n");
            return FAILURE;
        }
        *tokens = new_tokens;
    }

    (*tokens)[*count].type = type;
    if (utils_safe_strcpy((*tokens)[*count].value, sizeof((*tokens)[*count].value),
                          value) != SUCCESS) {
        fprintf(stderr, "Error: Token value is too long.\n");
        return FAILURE;
    }

    (*count)++;
    return SUCCESS;
}

static int soft_parser_read_word(const char *sql, size_t *index, char *buffer,
                                 size_t buffer_size) {
    size_t length;

    length = 0;
    while (sql[*index] != '\0' &&
           (isalnum((unsigned char)sql[*index]) || sql[*index] == '_')) {
        if (length + 1 >= buffer_size) {
            return FAILURE;
        }
        buffer[length++] = sql[*index];
        (*index)++;
    }
    buffer[length] = '\0';
    return SUCCESS;
}

static int soft_parser_read_string(const char *sql, size_t *index, char *buffer,
                                   size_t buffer_size) {
    size_t length;

    length = 0;
    (*index)++;
    while (sql[*index] != '\0') {
        if (sql[*index] == '\'') {
            if (sql[*index + 1] == '\'') {
                if (length + 1 >= buffer_size) {
                    return FAILURE;
                }
                buffer[length++] = '\'';
                *index += 2;
                continue;
            }
            (*index)++;
            buffer[length] = '\0';
            return SUCCESS;
        }

        if (length + 1 >= buffer_size) {
            return FAILURE;
        }

        buffer[length++] = sql[*index];
        (*index)++;
    }

    return FAILURE;
}

static int soft_parser_read_number(const char *sql, size_t *index, char *buffer,
                                   size_t buffer_size) {
    size_t length;

    length = 0;
    if (sql[*index] == '-' || sql[*index] == '+') {
        if (length + 1 >= buffer_size) {
            return FAILURE;
        }
        buffer[length++] = sql[*index];
        (*index)++;
    }

    while (isdigit((unsigned char)sql[*index])) {
        if (length + 1 >= buffer_size) {
            return FAILURE;
        }
        buffer[length++] = sql[*index];
        (*index)++;
    }

    buffer[length] = '\0';
    return SUCCESS;
}

static int soft_parser_is_numeric_start(const char *sql, size_t index) {
    if (isdigit((unsigned char)sql[index])) {
        return 1;
    }

    if ((sql[index] == '-' || sql[index] == '+') &&
        isdigit((unsigned char)sql[index + 1])) {
        return 1;
    }

    return 0;
}

static Token *soft_parser_tokenize_sql(const char *sql, int *token_count) {
    Token *tokens;
    int count;
    int capacity;
    char upper_buffer[MAX_TOKEN_VALUE];
    char token_buffer[MAX_TOKEN_VALUE];
    size_t i;

    if (sql == NULL || token_count == NULL) {
        return NULL;
    }

    *token_count = 0;
    tokens = NULL;
    count = 0;
    capacity = 0;
    i = 0;
    while (sql[i] != '\0') {
        if (isspace((unsigned char)sql[i])) {
            i++;
            continue;
        }

        if (sql[i] == '(') {
            if (soft_parser_append_token(&tokens, &count, &capacity, TOKEN_LPAREN,
                                         "(") != SUCCESS) {
                free(tokens);
                return NULL;
            }
            i++;
            continue;
        }

        if (sql[i] == ')') {
            if (soft_parser_append_token(&tokens, &count, &capacity, TOKEN_RPAREN,
                                         ")") != SUCCESS) {
                free(tokens);
                return NULL;
            }
            i++;
            continue;
        }

        if (sql[i] == ',') {
            if (soft_parser_append_token(&tokens, &count, &capacity, TOKEN_COMMA,
                                         ",") != SUCCESS) {
                free(tokens);
                return NULL;
            }
            i++;
            continue;
        }

        if (sql[i] == ';') {
            if (soft_parser_append_token(&tokens, &count, &capacity,
                                         TOKEN_SEMICOLON, ";") != SUCCESS) {
                free(tokens);
                return NULL;
            }
            i++;
            continue;
        }

        if (sql[i] == '*') {
            if (soft_parser_append_token(&tokens, &count, &capacity,
                                         TOKEN_IDENTIFIER, "*") != SUCCESS) {
                free(tokens);
                return NULL;
            }
            i++;
            continue;
        }

        if (sql[i] == '\'') {
            if (soft_parser_read_string(sql, &i, token_buffer,
                                        sizeof(token_buffer)) != SUCCESS) {
                fprintf(stderr, "Error: Unterminated string literal.\n");
                free(tokens);
                return NULL;
            }

            if (soft_parser_append_token(&tokens, &count, &capacity,
                                         TOKEN_STR_LITERAL, token_buffer) != SUCCESS) {
                free(tokens);
                return NULL;
            }
            continue;
        }

        if (sql[i] == '!' || sql[i] == '<' || sql[i] == '>' || sql[i] == '=') {
            token_buffer[0] = sql[i];
            token_buffer[1] = '\0';
            if ((sql[i] == '!' || sql[i] == '<' || sql[i] == '>') &&
                sql[i + 1] == '=') {
                token_buffer[1] = '=';
                token_buffer[2] = '\0';
                i += 2;
            } else {
                i++;
            }

            if (soft_parser_append_token(&tokens, &count, &capacity,
                                         TOKEN_OPERATOR, token_buffer) != SUCCESS) {
                free(tokens);
                return NULL;
            }
            continue;
        }

        if (soft_parser_is_numeric_start(sql, i)) {
            if (soft_parser_read_number(sql, &i, token_buffer,
                                        sizeof(token_buffer)) != SUCCESS) {
                fprintf(stderr, "Error: Integer literal is too long.\n");
                free(tokens);
                return NULL;
            }

            if (soft_parser_append_token(&tokens, &count, &capacity,
                                         TOKEN_INT_LITERAL, token_buffer) != SUCCESS) {
                free(tokens);
                return NULL;
            }
            continue;
        }

        if (isalpha((unsigned char)sql[i]) || sql[i] == '_') {
            if (soft_parser_read_word(sql, &i, token_buffer,
                                      sizeof(token_buffer)) != SUCCESS) {
                fprintf(stderr, "Error: Identifier is too long.\n");
                free(tokens);
                return NULL;
            }

            if (utils_is_sql_keyword(token_buffer)) {
                if (utils_to_upper_copy(token_buffer, upper_buffer,
                                        sizeof(upper_buffer)) != SUCCESS) {
                    free(tokens);
                    return NULL;
                }
                if (soft_parser_append_token(&tokens, &count, &capacity,
                                             TOKEN_KEYWORD, upper_buffer) != SUCCESS) {
                    free(tokens);
                    return NULL;
                }
            } else {
                if (soft_parser_append_token(&tokens, &count, &capacity,
                                             TOKEN_IDENTIFIER, token_buffer) != SUCCESS) {
                    free(tokens);
                    return NULL;
                }
            }
            continue;
        }

        token_buffer[0] = sql[i];
        token_buffer[1] = '\0';
        if (soft_parser_append_token(&tokens, &count, &capacity, TOKEN_UNKNOWN,
                                     token_buffer) != SUCCESS) {
            free(tokens);
            return NULL;
        }
        i++;
    }

    *token_count = count;
    return tokens;
}

Token *soft_parse(const char *sql, int *token_count) {
    char *working_sql;
    Token *tokens;
    int parsed_token_count;

    if (sql == NULL || token_count == NULL) {
        return NULL;
    }

    *token_count = 0;
    working_sql = utils_strdup(sql);
    if (working_sql == NULL) {
        return NULL;
    }

    utils_trim(working_sql);
    if (working_sql[0] == '\0') {
        free(working_sql);
        return NULL;
    }

    tokens = soft_parser_lookup_cache(working_sql, token_count);
    if (tokens != NULL) {
        free(working_sql);
        return tokens;
    }

    tokens = soft_parser_tokenize_sql(working_sql, &parsed_token_count);
    if (tokens == NULL) {
        free(working_sql);
        return NULL;
    }

    *token_count = parsed_token_count;
    if (soft_parser_store_cache(working_sql, tokens, parsed_token_count) != SUCCESS) {
        /* Parsing already succeeded, so cache storage is treated as best-effort. */
    }

    free(working_sql);
    return tokens;
}

void soft_parser_cleanup_cache(void) {
    SoftParserCacheEntry *entry;
    SoftParserCacheEntry *next;

    entry = soft_parser_cache_head;
    while (entry != NULL) {
        next = entry->next;
        soft_parser_free_cache_entry(entry);
        entry = next;
    }

    soft_parser_cache_head = NULL;
    soft_parser_cache_entry_count = 0;
    soft_parser_cache_hit_count = 0;
}

int soft_parser_get_cache_entry_count(void) {
    return soft_parser_cache_entry_count;
}

int soft_parser_get_cache_hit_count(void) {
    return soft_parser_cache_hit_count;
}

const char *soft_parser_token_type_name(TokenType type) {
    switch (type) {
        case TOKEN_KEYWORD:
            return "KEYWORD";
        case TOKEN_IDENTIFIER:
            return "IDENTIFIER";
        case TOKEN_INT_LITERAL:
            return "INT_LITERAL";
        case TOKEN_STR_LITERAL:
            return "STR_LITERAL";
        case TOKEN_OPERATOR:
            return "OPERATOR";
        case TOKEN_LPAREN:
            return "LPAREN";
        case TOKEN_RPAREN:
            return "RPAREN";
        case TOKEN_COMMA:
            return "COMMA";
        case TOKEN_SEMICOLON:
            return "SEMICOLON";
        case TOKEN_UNKNOWN:
        default:
            return "UNKNOWN";
    }
}
