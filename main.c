#include <stdio.h>

#include "./src/automata.h"
#include "./src/tokenizer.h"
#include "./src/util/debug.h"
#include <assert.h>
#include <ctype.h>
#include <string.h>

int main(void)
{
    cegex_tokenizer ctx = { 0 };
    cegex_tokenizer_init(&ctx);

    cegex_token_array tokens = { 0 };
    const char* expr = "\\xZ[(ab[de]|a)*c";
    if(CEGEX_OK != cegex_tokenizer_exec(&ctx, expr, 16, &tokens)) {
        printf("Error occured during tokenization\n");
        cegex_tokenizer_free(&ctx);
        return 1;
    }

    for(size_t i = 0; i < tokens.length; i++) {
        cegex_token tok = { 0 };
        ARRAY_NTH(&tokens, i, &tok);

        char buf[32];
        memcpy(buf, expr + tok.start, tok.length);
        buf[tok.length] = '\0';

        printf("(%zu): \"%s\" \t\t(%s)\n", i, buf, cegex_token_str(tok.type));
    }

    cegex_tokenizer_free(&ctx);
    return 0;
}
