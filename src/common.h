#ifndef CEGEX_COMMON_H
#define CEGEX_COMMON_H

#define CEGEX_TRY(expr)        \
    do {                       \
        status = (expr);       \
        if(status != CEGEX_OK) \
            goto fail;         \
    } while(0)

typedef unsigned char cbyte;

typedef enum cegex_status
{
    CEGEX_OK = 0,
    CEGEX_ERR_INVALID_ARGUMENT,
    CEGEX_ERR_MEMORY,
    CEGEX_ERR_SYNTAX,
    CEGEX_ERR_LIMIT,
    CEGEX_ERR_UNSUPPORTED
} cegex_status;

#endif