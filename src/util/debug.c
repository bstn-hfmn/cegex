#include "debug.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>

#define CEGEX_DBG_TREE_SPACE            " "
#define CEGEX_DBG_TREE_SPACES_PER_DEPTH 3

typedef struct cegex_token_repr
{
    cegex_token_type type;
    const char* repr;
} cegex_token_repr;

typedef struct cegex_parse_node_repr
{
    cegex_parse_node_type type;
    const char* repr;
} cegex_parse_node_repr;

typedef struct cegex_ascii_table_entry
{
    cegex_uchar code;
    const char* repr;
} cegex_ascii_table_entry;

static cegex_parse_node_repr node_repr[] = {
    { CEGEX_PARSE_NODE_UNKNOWN, "UNK" },
    { CEGEX_PARSE_NODE_CHARSET, "CHARSET" },
    { CEGEX_PARSE_NODE_CONCAT, "CONCAT" },
    { CEGEX_PARSE_NODE_ALTERNATE, "ALTERNATE" },
    { CEGEX_PARSE_NODE_GROUP, "GROUP" },
    { CEGEX_PARSE_NODE_QUANTIFIER_PLUS, "PLUS (+)" },
    { CEGEX_PARSE_NODE_QUANTIFIER_STAR, "STAR (*)" },
    { CEGEX_PARSE_NODE_QUANTIFIER_COUNTED, "COUNTED" },
    { CEGEX_PARSE_NODE_QUANTIFIER_OPTIONAL, "OPT (?)" },
    { CEGEX_PARSE_NODE_QUANTIFIER_LAZY, "LAZY (?)" },
    { CEGEX_PARSE_NODE_ANCHOR_START, "ANCHOR START (^)" },
    { CEGEX_PARSE_NODE_ANCHOR_END, "ANCHOR END ($)" },
    { CEGEX_PARSE_NODE_ANCHOR_WORD_BOUNDARY, "BOUNDARY (\\b)" },
    { CEGEX_PARSE_NODE_ANCHOR_NOT_WORD_BOUNDARY, "NOT BOUNDARY (\\B)" },
};

static cegex_token_repr token_repr[] = {
    { CEGEX_TOKEN_UNKNOWN, "CEGEX_TOKEN_UNKNOWN" },
    { CEGEX_TOKEN_CHARCODE, "CEGEX_TOKEN_CHARACTER" },
    { CEGEX_TOKEN_CLASS_DOT, "CEGEX_TOKEN_CLASS_DOT" },
    { CEGEX_TOKEN_CLASS_WORD, "CEGEX_TOKEN_CLASS_WORD" },
    { CEGEX_TOKEN_CLASS_NOT_WORD, "CEGEX_TOKEN_CLASS_NOT_WORD" },
    { CEGEX_TOKEN_CLASS_DIGIT, "CEGEX_TOKEN_CLASS_DIGIT" },
    { CEGEX_TOKEN_CLASS_NOT_DIGIT, "CEGEX_TOKEN_CLASS_NOT_DIGIT" },
    { CEGEX_TOKEN_CLASS_WHITESPACE, "CEGEX_TOKEN_CLASS_WHITESPACE" },
    { CEGEX_TOKEN_CLASS_NOT_WHITESPACE, "CEGEX_TOKEN_CLASS_NOT_WHITESPACE" },
    { CEGEX_TOKEN_HAT, "CEGEX_TOKEN_HAT" },
    { CEGEX_TOKEN_DOLLAR, "CEGEX_TOKEN_DOLLAR" },
    { CEGEX_TOKEN_ANCHOR_BOUNDARY, "CEGEX_TOKEN_ANCHOR_BOUNDARY" },
    { CEGEX_TOKEN_ANCHOR_NOT_BOUNDARY, "CEGEX_TOKEN_ANCHOR_NOT_BOUNDARY" },
    { CEGEX_TOKEN_ESCAPE_BELL, "CEGEX_TOKEN_ESCAPE_BELL" },
    { CEGEX_TOKEN_ESCAPE_FORM_FEED, "CEGEX_TOKEN_ESCAPE_FORM_FEED" },
    { CEGEX_TOKEN_ESCAPE_HORIZONTAL_TAB, "CEGEX_TOKEN_ESCAPE_HORIZONTAL_TAB" },
    { CEGEX_TOKEN_ESCAPE_NEWLINE, "CEGEX_TOKEN_ESCAPE_NEWLINE" },
    { CEGEX_TOKEN_ESCAPE_CARRIAGE_RETURN, "CEGEX_TOKEN_ESCAPE_CARRIAGE_RETURN" },
    { CEGEX_TOKEN_ESCAPE_VERTICAL_TAB, "CEGEX_TOKEN_ESCAPE_VERTICAL_TAB" },
    { CEGEX_TOKEN_ESCAPE_OCTAL, "CEGEX_TOKEN_ESCAPE_OCTAL" },
    { CEGEX_TOKEN_ESCAPE_HEX, "CEGEX_TOKEN_ESCAPE_HEX" },
    { CEGEX_TOKEN_STAR, "CEGEX_TOKEN_STAR" },
    { CEGEX_TOKEN_PLUS, "CEGEX_TOKEN_PLUS" },
    { CEGEX_TOKEN_MINUS, "CEGEX_TOKEN_MINUS" },
    { CEGEX_TOKEN_QUESTION_MARK, "CEGEX_TOKEN_QUESTION_MARK" },
    { CEGEX_TOKEN_COLON, "CEGEX_TOKEN_COLON" },
    { CEGEX_TOKEN_ALTERNATE, "CEGEX_TOKEN_ALTERNATE" },
    { CEGEX_TOKEN_PAREN_OPEN, "CEGEX_TOKEN_PAREN_OPEN" },
    { CEGEX_TOKEN_PAREN_CLOSE, "CEGEX_TOKEN_PAREN_CLOSE" },
    { CEGEX_TOKEN_SQUARE_OPEN, "CEGEX_TOKEN_SQUARE_OPEN" },
    { CEGEX_TOKEN_SQUARE_CLOSE, "CEGEX_TOKEN_SQUARE_CLOSE" },
    { CEGEX_TOKEN_CURLY_OPEN, "CEGEX_TOKEN_CURLY_OPEN" },
    { CEGEX_TOKEN_CURLY_CLOSE, "CEGEX_TOKEN_CURLY_CLOSE" },
    { CEGEX_TOKEN_COMMA, "CEGEX_TOKEN_COMMA" }
};

static cegex_ascii_table_entry ascii_table[] = {
    { .code = 0, .repr = "<NUL>" },
    { .code = 1, .repr = "<SOH>" },
    { .code = 2, .repr = "<STX>" },
    { .code = 3, .repr = "<ETX>" },
    { .code = 4, .repr = "<EOT>" },
    { .code = 5, .repr = "<ENQ>" },
    { .code = 6, .repr = "<ACK>" },
    { .code = 7, .repr = "<BEL>" },
    { .code = 8, .repr = "<BS>" },
    { .code = 9, .repr = "<HT>" },
    { .code = 10, .repr = "<LF>" },
    { .code = 11, .repr = "<VT>" },
    { .code = 12, .repr = "<FF>" },
    { .code = 13, .repr = "<CR>" },
    { .code = 14, .repr = "<SO>" },
    { .code = 15, .repr = "<SI>" },
    { .code = 16, .repr = "<DLE>" },
    { .code = 17, .repr = "<DC1>" },
    { .code = 18, .repr = "<DC2>" },
    { .code = 19, .repr = "<DC3>" },
    { .code = 20, .repr = "<DC4>" },
    { .code = 21, .repr = "<NAK>" },
    { .code = 22, .repr = "<SYN>" },
    { .code = 23, .repr = "<ETB>" },
    { .code = 24, .repr = "<CAN>" },
    { .code = 25, .repr = "<EM>" },
    { .code = 26, .repr = "<SUB>" },
    { .code = 27, .repr = "<ESC>" },
    { .code = 28, .repr = "<FS>" },
    { .code = 29, .repr = "<GS>" },
    { .code = 30, .repr = "<RS>" },
    { .code = 31, .repr = "<US>" },

    { .code = 32, .repr = "<SP>" },
    { .code = 33, .repr = "!" },
    { .code = 34, .repr = "\"" },
    { .code = 35, .repr = "#" },
    { .code = 36, .repr = "$" },
    { .code = 37, .repr = "%" },
    { .code = 38, .repr = "&" },
    { .code = 39, .repr = "'" },
    { .code = 40, .repr = "(" },
    { .code = 41, .repr = ")" },
    { .code = 42, .repr = "*" },
    { .code = 43, .repr = "+" },
    { .code = 44, .repr = "," },
    { .code = 45, .repr = "-" },
    { .code = 46, .repr = "." },
    { .code = 47, .repr = "/" },

    { .code = 48, .repr = "0" },
    { .code = 49, .repr = "1" },
    { .code = 50, .repr = "2" },
    { .code = 51, .repr = "3" },
    { .code = 52, .repr = "4" },
    { .code = 53, .repr = "5" },
    { .code = 54, .repr = "6" },
    { .code = 55, .repr = "7" },
    { .code = 56, .repr = "8" },
    { .code = 57, .repr = "9" },

    { .code = 58, .repr = ":" },
    { .code = 59, .repr = ";" },
    { .code = 60, .repr = "<" },
    { .code = 61, .repr = "=" },
    { .code = 62, .repr = ">" },
    { .code = 63, .repr = "?" },
    { .code = 64, .repr = "@" },

    { .code = 65, .repr = "A" },
    { .code = 66, .repr = "B" },
    { .code = 67, .repr = "C" },
    { .code = 68, .repr = "D" },
    { .code = 69, .repr = "E" },
    { .code = 70, .repr = "F" },
    { .code = 71, .repr = "G" },
    { .code = 72, .repr = "H" },
    { .code = 73, .repr = "I" },
    { .code = 74, .repr = "J" },
    { .code = 75, .repr = "K" },
    { .code = 76, .repr = "L" },
    { .code = 77, .repr = "M" },
    { .code = 78, .repr = "N" },
    { .code = 79, .repr = "O" },
    { .code = 80, .repr = "P" },
    { .code = 81, .repr = "Q" },
    { .code = 82, .repr = "R" },
    { .code = 83, .repr = "S" },
    { .code = 84, .repr = "T" },
    { .code = 85, .repr = "U" },
    { .code = 86, .repr = "V" },
    { .code = 87, .repr = "W" },
    { .code = 88, .repr = "X" },
    { .code = 89, .repr = "Y" },
    { .code = 90, .repr = "Z" },

    { .code = 91, .repr = "[" },
    { .code = 92, .repr = "\\" },
    { .code = 93, .repr = "]" },
    { .code = 94, .repr = "^" },
    { .code = 95, .repr = "_" },
    { .code = 96, .repr = "`" },

    { .code = 97, .repr = "a" },
    { .code = 98, .repr = "b" },
    { .code = 99, .repr = "c" },
    { .code = 100, .repr = "d" },
    { .code = 101, .repr = "e" },
    { .code = 102, .repr = "f" },
    { .code = 103, .repr = "g" },
    { .code = 104, .repr = "h" },
    { .code = 105, .repr = "i" },
    { .code = 106, .repr = "j" },
    { .code = 107, .repr = "k" },
    { .code = 108, .repr = "l" },
    { .code = 109, .repr = "m" },
    { .code = 110, .repr = "n" },
    { .code = 111, .repr = "o" },
    { .code = 112, .repr = "p" },
    { .code = 113, .repr = "q" },
    { .code = 114, .repr = "r" },
    { .code = 115, .repr = "s" },
    { .code = 116, .repr = "t" },
    { .code = 117, .repr = "u" },
    { .code = 118, .repr = "v" },
    { .code = 119, .repr = "w" },
    { .code = 120, .repr = "x" },
    { .code = 121, .repr = "y" },
    { .code = 122, .repr = "z" },

    { .code = 123, .repr = "{" },
    { .code = 124, .repr = "|" },
    { .code = 125, .repr = "}" },
    { .code = 126, .repr = "~" },

    { .code = 127, .repr = "<DEL>" },

    { .code = 128, .repr = "€" },
    { .code = 129, .repr = "<UNUSED>" },

    { .code = 130, .repr = "‚" },
    { .code = 131, .repr = "ƒ" },
    { .code = 132, .repr = "„" },
    { .code = 133, .repr = "…" },
    { .code = 134, .repr = "†" },
    { .code = 135, .repr = "‡" },
    { .code = 136, .repr = "ˆ" },
    { .code = 137, .repr = "‰" },
    { .code = 138, .repr = "Š" },
    { .code = 139, .repr = "‹" },
    { .code = 140, .repr = "Œ" },
    { .code = 141, .repr = "<UNUSED>" },
    { .code = 142, .repr = "Ž" },
    { .code = 143, .repr = "<UNUSED>" },
    { .code = 144, .repr = "<UNUSED>" },
    { .code = 145, .repr = "‘" },
    { .code = 146, .repr = "’" },
    { .code = 147, .repr = "“" },
    { .code = 148, .repr = "”" },
    { .code = 149, .repr = "•" },
    { .code = 150, .repr = "–" },
    { .code = 151, .repr = "—" },
    { .code = 152, .repr = "˜" },
    { .code = 153, .repr = "™" },
    { .code = 154, .repr = "š" },
    { .code = 155, .repr = "›" },
    { .code = 156, .repr = "œ" },
    { .code = 157, .repr = "<UNUSED>" },
    { .code = 158, .repr = "ž" },
    { .code = 159, .repr = "Ÿ" },

    { .code = 160, .repr = "<NBSP>" },
    { .code = 161, .repr = "¡" },
    { .code = 162, .repr = "¢" },
    { .code = 163, .repr = "£" },
    { .code = 164, .repr = "¤" },
    { .code = 165, .repr = "¥" },
    { .code = 166, .repr = "¦" },
    { .code = 167, .repr = "§" },
    { .code = 168, .repr = "¨" },
    { .code = 169, .repr = "©" },
    { .code = 170, .repr = "ª" },
    { .code = 171, .repr = "«" },
    { .code = 172, .repr = "¬" },
    { .code = 173, .repr = "<SHY>" },
    { .code = 174, .repr = "®" },
    { .code = 175, .repr = "¯" },
    { .code = 176, .repr = "°" },
    { .code = 177, .repr = "±" },
    { .code = 178, .repr = "²" },
    { .code = 179, .repr = "³" },
    { .code = 180, .repr = "´" },
    { .code = 181, .repr = "µ" },
    { .code = 182, .repr = "¶" },
    { .code = 183, .repr = "·" },
    { .code = 184, .repr = "¸" },
    { .code = 185, .repr = "¹" },
    { .code = 186, .repr = "º" },
    { .code = 187, .repr = "»" },
    { .code = 188, .repr = "¼" },
    { .code = 189, .repr = "½" },
    { .code = 190, .repr = "¾" },
    { .code = 191, .repr = "¿" },

    { .code = 192, .repr = "À" },
    { .code = 193, .repr = "Á" },
    { .code = 194, .repr = "Â" },
    { .code = 195, .repr = "Ã" },
    { .code = 196, .repr = "Ä" },
    { .code = 197, .repr = "Å" },
    { .code = 198, .repr = "Æ" },
    { .code = 199, .repr = "Ç" },
    { .code = 200, .repr = "È" },
    { .code = 201, .repr = "É" },
    { .code = 202, .repr = "Ê" },
    { .code = 203, .repr = "Ë" },
    { .code = 204, .repr = "Ì" },
    { .code = 205, .repr = "Í" },
    { .code = 206, .repr = "Î" },
    { .code = 207, .repr = "Ï" },
    { .code = 208, .repr = "Ð" },
    { .code = 209, .repr = "Ñ" },
    { .code = 210, .repr = "Ò" },
    { .code = 211, .repr = "Ó" },
    { .code = 212, .repr = "Ô" },
    { .code = 213, .repr = "Õ" },
    { .code = 214, .repr = "Ö" },
    { .code = 215, .repr = "×" },
    { .code = 216, .repr = "Ø" },
    { .code = 217, .repr = "Ù" },
    { .code = 218, .repr = "Ú" },
    { .code = 219, .repr = "Û" },
    { .code = 220, .repr = "Ü" },
    { .code = 221, .repr = "Ý" },
    { .code = 222, .repr = "Þ" },
    { .code = 223, .repr = "ß" },

    { .code = 224, .repr = "à" },
    { .code = 225, .repr = "á" },
    { .code = 226, .repr = "â" },
    { .code = 227, .repr = "ã" },
    { .code = 228, .repr = "ä" },
    { .code = 229, .repr = "å" },
    { .code = 230, .repr = "æ" },
    { .code = 231, .repr = "ç" },
    { .code = 232, .repr = "è" },
    { .code = 233, .repr = "é" },
    { .code = 234, .repr = "ê" },
    { .code = 235, .repr = "ë" },
    { .code = 236, .repr = "ì" },
    { .code = 237, .repr = "í" },
    { .code = 238, .repr = "î" },
    { .code = 239, .repr = "ï" },
    { .code = 240, .repr = "ð" },
    { .code = 241, .repr = "ñ" },
    { .code = 242, .repr = "ò" },
    { .code = 243, .repr = "ó" },
    { .code = 244, .repr = "ô" },
    { .code = 245, .repr = "õ" },
    { .code = 246, .repr = "ö" },
    { .code = 247, .repr = "÷" },
    { .code = 248, .repr = "ø" },
    { .code = 249, .repr = "ù" },
    { .code = 250, .repr = "ú" },
    { .code = 251, .repr = "û" },
    { .code = 252, .repr = "ü" },
    { .code = 253, .repr = "ý" },
    { .code = 254, .repr = "þ" },
    { .code = 255, .repr = "ÿ" }
};

static void node_snprint(char* buffer,
                         size_t n,
                         size_t* written,
                         const char* fmt,
                         ...)
{
    char* dst = NULL;
    size_t dn = 0;

    if(buffer != NULL && *written < n) {
        dst = buffer + *written;
        dn = n - *written;
    }

    va_list args;
    va_start(args, fmt);
    int ret = vsnprintf(dst, dn, fmt, args);
    va_end(args);

    if(ret > 0) {
        *written += (size_t) ret;
    }
}

void cegex_parse_node_snprint(cegex_parse_node* node,
                              char* buffer,
                              size_t n,
                              size_t depth,
                              size_t* written)
{
    const char* type_str = cegex_parse_node_type_str(node->type);
    int pad = (int) depth;

    switch(node->type) {
    case CEGEX_PARSE_NODE_QUANTIFIER_COUNTED: {
        switch(node->data.counted.type) {
        case CEGEX_COUNTED_QUANTIFIER_EXACT:
            node_snprint(buffer, n, written, "%*s%s {%zu} (\n",
                         pad, CEGEX_DBG_TREE_SPACE, type_str, node->data.counted.count);
            break;

        case CEGEX_COUNTED_QUANTIFIER_MIN:
            node_snprint(buffer, n, written, "%*s%s {%zu, ...} (\n",
                         pad, CEGEX_DBG_TREE_SPACE, type_str, node->data.counted.min);
            break;

        case CEGEX_COUNTED_QUANTIFIER_MIN_MAX:
            node_snprint(buffer, n, written, "%*s%s {%zu, %zu} (\n",
                         pad, CEGEX_DBG_TREE_SPACE, type_str, node->data.counted.min, node->data.counted.max);
            break;
        }

        cegex_parse_node_snprint(node->as.unary.value, buffer, n, depth + CEGEX_DBG_TREE_SPACES_PER_DEPTH, written);
        node_snprint(buffer, n, written, "%*s)\n", pad, CEGEX_DBG_TREE_SPACE);
    } break;

    case CEGEX_PARSE_NODE_GROUP:
    case CEGEX_PARSE_NODE_QUANTIFIER_PLUS:
    case CEGEX_PARSE_NODE_QUANTIFIER_OPTIONAL:
    case CEGEX_PARSE_NODE_QUANTIFIER_LAZY:
    case CEGEX_PARSE_NODE_QUANTIFIER_STAR: {
        node_snprint(buffer, n, written, "%*s%s (\n", pad, CEGEX_DBG_TREE_SPACE, type_str);
        cegex_parse_node_snprint(node->as.unary.value, buffer, n, depth + CEGEX_DBG_TREE_SPACES_PER_DEPTH, written);
        node_snprint(buffer, n, written, "%*s)\n", pad, CEGEX_DBG_TREE_SPACE);
    } break;

    case CEGEX_PARSE_NODE_ANCHOR_WORD_BOUNDARY:
    case CEGEX_PARSE_NODE_ANCHOR_NOT_WORD_BOUNDARY:
    case CEGEX_PARSE_NODE_ANCHOR_START:
    case CEGEX_PARSE_NODE_ANCHOR_END: {
        node_snprint(buffer, n, written, "%*s%s\n", pad, CEGEX_DBG_TREE_SPACE, type_str);
    } break;

    case CEGEX_PARSE_NODE_ALTERNATE:
    case CEGEX_PARSE_NODE_CONCAT: {
        node_snprint(buffer, n, written, "%*s%s (\n", pad, CEGEX_DBG_TREE_SPACE, type_str);
        cegex_parse_node_snprint(node->as.binary.lhs, buffer, n, depth + CEGEX_DBG_TREE_SPACES_PER_DEPTH, written);
        cegex_parse_node_snprint(node->as.binary.rhs, buffer, n, depth + CEGEX_DBG_TREE_SPACES_PER_DEPTH, written);
        node_snprint(buffer, n, written, "%*s)\n", pad, CEGEX_DBG_TREE_SPACE);
    } break;

    case CEGEX_PARSE_NODE_CHARSET: {
        node_snprint(buffer, n, written, "%*s%s <negated=%s, [ ",
                     pad, CEGEX_DBG_TREE_SPACE, type_str, node->as.set.negated ? "true" : "false");

        for(size_t i = 0; i < node->as.set.num_characters; i++) {
            cegex_uchar charcode = node->as.set.characters[i];
            assert(charcode <= 255);

            node_snprint(buffer, n, written, "%s%s",
                         ascii_table[charcode].repr,
                         (i + 1 < node->as.set.num_characters) ? "," : "");
        }

        node_snprint(buffer, n, written, " ]>\n");
    } break;

    default: {
        assert(false);
    } break;
    }
}

void cegex_parse_node_str(cegex_parse_node* node,
                          char* buffer,
                          size_t n)
{
    assert(node != NULL);
    size_t written = 0;
    cegex_parse_node_snprint(node, buffer, n, 0, &written);
}

void cegex_nfa_digraph_str(nfa* ctx,
                           nfa_fragment fragment,
                           char* buffer,
                           size_t n)
{
    assert(ctx != NULL);
    if(ctx->states.length == 0 || n == 0) {
        return;
    }

    nfa_state_id_array visited;
    nfa_state_id_array stack;
    ARRAY_INIT(&stack);
    ARRAY_INIT(&visited);

    size_t written = 0;
    written += (size_t) snprintf(buffer + written, n, "digraph G {\n\trankdir=\"LR\";\n\tnode[shape=circle];\n\n");
    if(written >= n)
        goto finally;

    char label[((NFA_TRANSITION_SET_MAX_SYMBOLS * 2) + 2) + 1];
    ARRAY_PUSH(&stack, fragment.initial);
    do {
        nfa_state_id top = 0;
        ARRAY_POP(&stack, &top);

        nfa_state* state = NULL;
        ARRAY_NTH_REF(&ctx->states, top, &state);

        if(state->is_final && state->id == fragment.final) {
            continue;
        }

        for(size_t i = 0; i < state->num_transitions; i++) {
            nfa_transition* transition = &state->transitions[i];
            if(transition->condition.type == NFA_TRANSITION_COND_EPSILON) {
                written += (size_t) snprintf(buffer + written, n, "\tq%zu -> q%zu [label=\"&#949;\"];\n", transition->from, transition->to);
                if(written >= n)
                    goto finally;
            } else {
                size_t label_length = 0;

                label[label_length++] = '{';
                for(size_t j = 0; j < transition->condition.set.length; j++) {
                    cegex_uchar symbol = transition->condition.set.elements[j];
                    label[label_length++] = (char) symbol;

                    if(j + 1 != transition->condition.set.length)
                        label[label_length++] = ',';
                }
                label[label_length++] = '}';
                label[label_length] = '\0';

                written += (size_t) snprintf(buffer + written, n, "\tq%zu -> q%zu [label=\"%s\"];\n", transition->from, transition->to, label);
                if(written >= n)
                    goto finally;
            }

            if(!ARRAY_CONTAINS(&visited, transition->to, (comparator) nfa_state_id_eq)) {
                ARRAY_PUSH(&stack, transition->to);
                ARRAY_PUSH(&visited, transition->to);
            }
        }
    } while(!ARRAY_EMPTY(&stack));

    written += (size_t) snprintf(buffer + written, n,
                                 "\n"
                                 "\tstart -> q%zu;\n"
                                 "\tstart[shape=none, label=\"\"];\n"
                                 "\tq%zu[shape=doublecircle];\n"
                                 "}\n",
                                 fragment.initial, fragment.final);
finally:
    if(written >= n)
        written = n - 1;

    buffer[written] = '\0';

    ARRAY_FREE(&visited);
    ARRAY_FREE(&stack);
}

const char* cegex_token_str(cegex_token_type type)
{
    const size_t LENGTH = sizeof(token_repr) / sizeof(cegex_token_repr);

    for(size_t i = 0; i < LENGTH; i++) {
        if(type == token_repr[i].type) {
            return token_repr[i].repr;
        }
    }

    return "UNK";
}

const char* cegex_parse_node_type_str(cegex_parse_node_type type)
{
    const size_t LENGTH = sizeof(node_repr) / sizeof(cegex_parse_node_repr);

    for(size_t i = 0; i < LENGTH; i++) {
        if(type == node_repr[i].type) {
            return node_repr[i].repr;
        }
    }

    return "UNK";
}