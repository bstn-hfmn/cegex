#include <stdio.h>

#include "./src/automata.h"
#include "./src/charset.h"
#include "./src/parser.h"
#include "./src/tokenizer.h"
#include "./src/util/debug.h"
#include <assert.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/***************************************** */
typedef struct cegex_ascii_table_entry
{
    cegex_uchar code;
    const char* repr;
} cegex_ascii_table_entry;

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

static const char* cegex_ascii_table_get_repr(cegex_uchar code)
{
    const size_t length = sizeof(ascii_table) / sizeof(cegex_ascii_table_entry);
    for(size_t i = 0; i < length; i++) {
        if(ascii_table[i].code == code)
            return ascii_table[i].repr;
    }

    return "UNK";
}

#define TREE_BRANCH "+-- "
#define TREE_LAST   "`-- "
#define TREE_PIPE   "|   "
#define TREE_SPACE  "    "

static const char* cegex_parse_node_type_name(cegex_parse_node_type type)
{
    switch(type) {
    case CEGEX_PARSE_NODE_UNKNOWN:
        return "UNKNOWN";
    case CEGEX_PARSE_NODE_CHARSET:
        return "CHARSET";
    case CEGEX_PARSE_NODE_CONCAT:
        return "CONCAT";
    case CEGEX_PARSE_NODE_ALTERNATE:
        return "ALTERNATE";
    case CEGEX_PARSE_NODE_GROUP:
        return "GROUP";
    case CEGEX_PARSE_NODE_QUANTIFIER_PLUS:
        return "PLUS";
    case CEGEX_PARSE_NODE_QUANTIFIER_STAR:
        return "STAR";
    case CEGEX_PARSE_NODE_QUANTIFIER_COUNTED:
        return "COUNTED";
    case CEGEX_PARSE_NODE_QUANTIFIER_OPTIONAL:
        return "OPTIONAL";
    case CEGEX_PARSE_NODE_QUANTIFIER_LAZY:
        return "LAZY";
    case CEGEX_PARSE_NODE_ANCHOR_START:
        return "ANCHOR_START";
    case CEGEX_PARSE_NODE_ANCHOR_END:
        return "ANCHOR_END";
    case CEGEX_PARSE_NODE_ANCHOR_WORD_BOUNDARY:
        return "WORD_BOUNDARY";
    case CEGEX_PARSE_NODE_ANCHOR_NOT_WORD_BOUNDARY:
        return "NOT_WORD_BOUNDARY";
    }

    return "INVALID";
}

static bool cegex_parse_node_is_unary(cegex_parse_node_type type)
{
    return type == CEGEX_PARSE_NODE_GROUP ||
           type == CEGEX_PARSE_NODE_QUANTIFIER_PLUS ||
           type == CEGEX_PARSE_NODE_QUANTIFIER_STAR ||
           type == CEGEX_PARSE_NODE_QUANTIFIER_COUNTED ||
           type == CEGEX_PARSE_NODE_QUANTIFIER_OPTIONAL ||
           type == CEGEX_PARSE_NODE_QUANTIFIER_LAZY;
}

static bool cegex_parse_node_is_binary(cegex_parse_node_type type)
{
    return type == CEGEX_PARSE_NODE_CONCAT ||
           type == CEGEX_PARSE_NODE_ALTERNATE;
}

static void cegex_print_charset(cegex_charset* set)
{
    printf("negated=%s, characters=[ ",
           set->negated ? "true" : "false");

    for(size_t i = 0; i < set->num_characters; i++) {
        cegex_uchar current = set->characters[i];
        printf("%s", cegex_ascii_table_get_repr(current));

        if(i + 1 < set->num_characters) {
            printf(",");
        }
    }

    printf(" ]");
}

static void cegex_parse_node_print_impl(
    cegex_parse_node* node,
    const char* prefix,
    bool is_last,
    bool is_root)
{
    if(!node) {
        printf("%s%s<null>\n", prefix, is_last ? TREE_LAST : TREE_BRANCH);
        return;
    }

    printf("%s", prefix);
    if(!is_root)
        printf("%s", is_last ? TREE_LAST : TREE_BRANCH);

    printf("%s", cegex_parse_node_type_name(node->type));

    switch(node->type) {
    case CEGEX_PARSE_NODE_CHARSET:
        printf(" <");
        cegex_print_charset(&node->as.set);
        printf(">");
        break;

    case CEGEX_PARSE_NODE_GROUP:
        if(node->data.group.is_capturing) {
            printf(" <capturing=true, index=%d>", node->data.group.index);
        } else {
            printf(" <capturing=false>");
        }
        break;

    case CEGEX_PARSE_NODE_QUANTIFIER_COUNTED:
        printf(" [");

        if(node->data.counted.type == CEGEX_COUNTED_QUANTIFIER_EXACT) {
            printf("%zu", node->data.counted.count);
        } else if(node->data.counted.type == CEGEX_COUNTED_QUANTIFIER_MIN_MAX) {
            printf("%zu, %zu",
                   node->data.counted.min,
                   node->data.counted.max);
        } else {
            printf("%zu, ...",
                   node->data.counted.min);
        }

        printf("]");
        break;

    default:
        break;
    }

    printf("\n");

    char child_prefix[512];
    snprintf(child_prefix, sizeof(child_prefix), "%s%s",
             prefix,
             is_root ? "" : (is_last ? TREE_SPACE : TREE_PIPE));

    if(cegex_parse_node_is_unary(node->type)) {
        cegex_parse_node_print_impl(
            node->as.unary.value,
            child_prefix,
            true,
            false);
    } else if(cegex_parse_node_is_binary(node->type)) {
        cegex_parse_node_print_impl(
            node->as.binary.lhs,
            child_prefix,
            false,
            false);

        cegex_parse_node_print_impl(
            node->as.binary.rhs,
            child_prefix,
            true,
            false);
    }
}

void cegex_parse_node_print(cegex_parse_node* root)
{
    if(root == NULL) {
        printf("<empty tree>\n");
        return;
    }

    cegex_parse_node_print_impl(root, " ", true, true);
}
/***************************************** */

int main(void)
{
    cegex_status status = CEGEX_OK;

    cegex_parser parser;
    cegex_parser_init(&parser);
    cegex_parsed_expr* parsed = NULL;

    cegex_tokenizer tokenizer;
    cegex_tokenizer_init(&tokenizer);
    cegex_tokenized_expr* tokenized = NULL;

    const char* expr = "\\n";
    if((status = cegex_tokenizer_exec(&tokenizer, expr, strlen(expr), &tokenized)) != CEGEX_OK) {
        printf("Error occured during tokenization\n");
        goto free;
    }

    if((status = cegex_parser_exec(&parser, tokenized, &parsed)) != CEGEX_OK) {
        printf("Error occured during parsing\n");
        goto free;
    }

    cegex_parse_node_print(parsed->root);

free:
    cegex_parser_free(&parser);
    cegex_tokenizer_free(&tokenizer);
    return status;
}
