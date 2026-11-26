#ifndef CEGEX_PARSER_H
#define CEGEX_PARSER_H

#include "charset.h"
#include "common.h"
#include "util/collections.h"

#include <stdbool.h>
#include <stdint.h>

#define CEGEX_DIGIT_BUFFER_LENGTH 32

typedef struct cegex_token cegex_token;
typedef enum cegex_token_type cegex_token_type;
typedef struct cegex_tokenized_expr cegex_tokenized_expr;

typedef enum cegex_parse_node_type
{
    CEGEX_PARSE_NODE_UNKNOWN = 0,
    CEGEX_PARSE_NODE_CHARSET,

    CEGEX_PARSE_NODE_CONCAT,
    CEGEX_PARSE_NODE_ALTERNATE,

    CEGEX_PARSE_NODE_GROUP,

    CEGEX_PARSE_NODE_QUANTIFIER_PLUS,
    CEGEX_PARSE_NODE_QUANTIFIER_STAR,
    CEGEX_PARSE_NODE_QUANTIFIER_COUNTED,
    CEGEX_PARSE_NODE_QUANTIFIER_OPTIONAL,
    CEGEX_PARSE_NODE_QUANTIFIER_LAZY,

    CEGEX_PARSE_NODE_ANCHOR_START,
    CEGEX_PARSE_NODE_ANCHOR_END,
    CEGEX_PARSE_NODE_ANCHOR_WORD_BOUNDARY,
    CEGEX_PARSE_NODE_ANCHOR_NOT_WORD_BOUNDARY,
} cegex_parse_node_type;

typedef struct cegex_parse_node_binary
{
    struct cegex_parse_node* lhs;
    struct cegex_parse_node* rhs;
} cegex_parse_node_alternation, cegex_parse_node_concat;

typedef struct cegex_parse_node_unary
{
    struct cegex_parse_node* value;
} cegex_parse_node_quantified, cegex_parse_node_group;

typedef enum cegex_counted_quantifier_type
{
    CEGEX_COUNTED_QUANTIFIER_EXACT,
    CEGEX_COUNTED_QUANTIFIER_MIN,
    CEGEX_COUNTED_QUANTIFIER_MIN_MAX,
} cegex_counted_quantifier_type;

typedef struct cegex_parse_node
{
    cegex_parse_node_type type;

    union
    {
        struct
        {
            uint32_t index;
            bool is_capturing;
        } group;

        struct
        {
            cegex_counted_quantifier_type type;
            uint64_t count;
            uint64_t min;
            uint64_t max;
        } counted;
    } data;

    union
    {
        struct cegex_charset set;
        struct cegex_parse_node_unary unary;
        struct cegex_parse_node_binary binary;
    } as;
} cegex_parse_node;

typedef ITERATOR(cegex_token) cegex_token_iterator;
typedef struct cegex_parsed_expr
{
    size_t num_groups;

    cegex_parse_node* root;
    cegex_tokenized_expr* tokenized;
} cegex_parsed_expr;

typedef ARRAY(uint32_t) cegex_group_idx_stack;
typedef struct cegex_parser
{
    cegex_token_iterator token_iterator;
    cegex_group_idx_stack group_idx_stack;

    size_t num_parsed;
    cegex_parsed_expr parsed[CEGEX_EXPRESSION_CAPACITY];
} cegex_parser;

void cegex_parser_init(cegex_parser* parser);
void cegex_parser_free(cegex_parser* parser);

cegex_status cegex_parser_exec(cegex_parser* ctx,
                               cegex_tokenized_expr* tokenized,
                               cegex_parsed_expr** out);

#endif