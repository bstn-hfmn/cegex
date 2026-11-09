#include <stdio.h>

#include "./src/automata.h"
#include "./src/util/debug.h"

int main(void)
{
    cegex_status status = CEGEX_OK;
    nfa_fragment abde, de, a, a2, b, c, ab, unioned, repeated, root;

    nfa ctx;
    nfa_init(&ctx);

    CEGEX_TRY(NFA_CREATE_SYMBOLS(&ctx, ((byte[]){ 'd', 'e' }), &de));
    CEGEX_TRY(NFA_CREATE_SYMBOL(&ctx, 'a', &a));
    CEGEX_TRY(NFA_CREATE_SYMBOL(&ctx, 'a', &a2));
    CEGEX_TRY(NFA_CREATE_SYMBOL(&ctx, 'b', &b));
    CEGEX_TRY(NFA_CREATE_SYMBOL(&ctx, 'c', &c));

    CEGEX_TRY(nfa_concat(&ctx, a, b, &ab));          // ab
    CEGEX_TRY(nfa_concat(&ctx, ab, de, &abde));      // ab[de]
    CEGEX_TRY(nfa_union(&ctx, abde, a2, &unioned));  // ab[de]|a
    CEGEX_TRY(nfa_star(&ctx, unioned, &repeated));   // (ab[de]|a)*
    CEGEX_TRY(nfa_concat(&ctx, repeated, c, &root)); // (ab[de]|a)*c

    char buffer[1024];
    nfa_digraph_str(&ctx, root, buffer, 1024);
    nfa_free(&ctx);

    printf("%s", buffer);
    return 0;

fail:
    fprintf(stderr, "Error occured");
    nfa_free(&ctx);
    return status;
}
