#include <stdio.h>

#include "./src/automata.h"
#include "./src/charset.h"
#include "./src/parser.h"
#include "./src/tokenizer.h"
#include "./src/util/debug.h"
#include "tests/testing.h"
#include <assert.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    cegex_status status = CEGEX_OK;

    cegex_parser parser;
    cegex_parser_init(&parser);
    cegex_parsed_expr* parsed = NULL;

    cegex_tokenizer tokenizer;
    cegex_tokenizer_init(&tokenizer);
    cegex_tokenized_expr* tokenized = NULL;

    const char* expr = "(\\d{1,3}\\.){3}\\d{1,3}";
    if((status = cegex_tokenizer_exec(&tokenizer, expr, strlen(expr), &tokenized)) != CEGEX_OK) {
        printf("Error occured during tokenization\n");
        goto free;
    }

    if((status = cegex_parser_exec(&parser, tokenized, &parsed)) != CEGEX_OK) {
        printf("Error occured during parsing\n");
        goto free;
    }

    char buffer[512];
    cegex_parse_node_str(parsed->root, buffer, 512);
    printf("%s", buffer);

free:
    cegex_parser_free(&parser);
    cegex_tokenizer_free(&tokenizer);
    return status;
}
