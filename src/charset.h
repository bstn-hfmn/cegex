#ifndef CEGEX_CHARSET_H
#define CEGEX_CHARSET_H

#include "common.h"
#include "token.h"
#include <stdbool.h>

#define CEGEX_CHARSET_CAPACITY 256

#define CEGEX_CHARSET(neg, num, chars) \
    ((cegex_charset){                  \
        .negated = neg,                \
        .num_characters = num,         \
        .characters = chars })

#define CEGEX_CHARSET_SINGLE(c)      CEGEX_CHARSET(false, 1, { (c) })

#define CEGEX_CHARSET_PERL_DOT       CEGEX_CHARSET(true, 2, "\n\r")
#define CEGEX_CHARSET_PERL_WORD      CEGEX_CHARSET(false, 63, "ABCDEFGHIJKLMNOPQRSTUVWXYZ" \
                                                              "abcdefghijklmnopqrstuvwxyz" \
                                                              "0123456789_")

#define CEGEX_CHARSET_PERL_NOT_WORD  CEGEX_CHARSET(true, 63, "ABCDEFGHIJKLMNOPQRSTUVWXYZ" \
                                                             "abcdefghijklmnopqrstuvwxyz" \
                                                             "0123456789_")

#define CEGEX_CHARSET_PERL_DIGIT     CEGEX_CHARSET(false, 10, "0123456789")
#define CEGEX_CHARSET_PERL_NOT_DIGIT CEGEX_CHARSET(true, 10, "0123456789")

#define CEGEX_CHARSET_PERL_SPACE     CEGEX_CHARSET(false, 3, " \n\t")
#define CEGEX_CHARSET_PERL_NOT_SPACE CEGEX_CHARSET(true, 3, " \n\t")

typedef struct cegex_charset
{
    bool negated;

    size_t num_characters;
    cegex_uchar characters[CEGEX_CHARSET_CAPACITY];
} cegex_charset;

cegex_charset cegex_charset_perl(cegex_token_type type);

cegex_status cegex_charset_add(cegex_charset* set,
                               cegex_uchar character);

cegex_status cegex_charset_add_range(cegex_charset* set,
                                     cegex_uchar low,
                                     cegex_uchar high);

cegex_status cegex_charset_add_subset(cegex_charset* set,
                                      cegex_charset subset);

bool cegex_charset_contains(const cegex_charset* set,
                            cegex_uchar needle);

#endif