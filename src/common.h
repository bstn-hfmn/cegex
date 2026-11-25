#ifndef CEGEX_COMMON_H
#define CEGEX_COMMON_H

#include <stdbool.h>

#define CEGEX_ASCII_CHARCODE_BEGIN 32
#define CEGEX_ASCII_CHARCODE_END   126

#define CEGEX_RADIX_OCT            8
#define CEGEX_RADIX_DEC            10
#define CEGEX_RADIX_HEX            16

#define CEGEX_EXPRESSION_CAPACITY  32

#define CEGEX_TRY(expr)        \
    do {                       \
        status = (expr);       \
        if(status != CEGEX_OK) \
            goto fail;         \
    } while(0)

typedef unsigned char cegex_uchar;

typedef enum cegex_status
{
    CEGEX_OK = 0,
    CEGEX_ERR_UNEXPECTED,
    CEGEX_ERR_INVALID_ARGUMENT,
    CEGEX_ERR_MEMORY,
    CEGEX_ERR_SYNTAX,
    CEGEX_ERR_LIMIT,
    CEGEX_ERR_UNSUPPORTED,
    CEGEX_ERR_UNKNOWN
} cegex_status;

#endif