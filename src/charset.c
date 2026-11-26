#include "charset.h"
#include "tokenizer.h"
#include <assert.h>

static bool cegex_uchar_eq(cegex_uchar* a,
                           cegex_uchar* b)
{
    return *a == *b;
}

cegex_charset cegex_charset_perl(cegex_token_type type)
{
    switch(type) {
    case CEGEX_TOKEN_CLASS_DOT:
        return CEGEX_CHARSET_PERL_DOT;
    case CEGEX_TOKEN_CLASS_NOT_WORD:
        return CEGEX_CHARSET_PERL_NOT_WORD;
    case CEGEX_TOKEN_CLASS_WORD:
        return CEGEX_CHARSET_PERL_WORD;
    case CEGEX_TOKEN_CLASS_DIGIT:
        return CEGEX_CHARSET_PERL_DIGIT;
    case CEGEX_TOKEN_CLASS_NOT_DIGIT:
        return CEGEX_CHARSET_PERL_NOT_DIGIT;
    case CEGEX_TOKEN_CLASS_WHITESPACE:
        return CEGEX_CHARSET_PERL_SPACE;
    case CEGEX_TOKEN_CLASS_NOT_WHITESPACE:
        return CEGEX_CHARSET_PERL_NOT_SPACE;

    default:
        assert(false);
        return (cegex_charset){ 0 };
    }
}

cegex_status cegex_charset_add(cegex_charset* set,
                               cegex_uchar character)
{
    if(set->num_characters >= CEGEX_CHARSET_CAPACITY) {
        return CEGEX_ERR_LIMIT;
    }

    cegex_uchar* characters = set->characters;
    if(!array_contains((void**) &characters, set->num_characters, sizeof(cegex_uchar), (void*) &character, (comparator) cegex_uchar_eq)) {
        set->characters[set->num_characters++] = character;
    }

    return CEGEX_OK;
}

cegex_status cegex_charset_add_range(cegex_charset* set,
                                     cegex_uchar low,
                                     cegex_uchar high)
{
    cegex_status status = CEGEX_ERR_UNKNOWN;
    while(low <= high) {
        CEGEX_TRY(
            cegex_charset_add(set, low));

        if(low == 0xFF)
            break;

        ++low;
    };

    return CEGEX_OK;
fail:
    return status;
}

cegex_status cegex_charset_add_subset(cegex_charset* set,
                                      cegex_charset subset)
{
    cegex_status status = CEGEX_ERR_UNKNOWN;

    if(subset.negated) {
        cegex_uchar* subset_characters = subset.characters;
        for(cegex_uchar curr = CEGEX_ASCII_CHARCODE_BEGIN; curr <= CEGEX_ASCII_CHARCODE_END; curr += 1) {
            if(array_contains((void**) &subset_characters, subset.num_characters, sizeof(cegex_uchar), &curr, (comparator) cegex_uchar_eq))
                continue;

            CEGEX_TRY(
                cegex_charset_add(set, curr));
        }
    } else {
        for(size_t i = 0; i < subset.num_characters; i++) {
            CEGEX_TRY(
                cegex_charset_add(set, subset.characters[i]));
        }
    }

    return CEGEX_OK;
fail:
    return status;
}

bool cegex_charset_contains(const cegex_charset* set,
                            cegex_uchar needle)
{
    assert(set != NULL);
    for(size_t i = 0; i < set->num_characters; i++) {
        if(set->characters[i] == needle)
            return true;
    }

    return false;
}