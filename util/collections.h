#ifndef UTIL_ARRAY_H
#define UTIL_ARRAY_H

#include <stdbool.h>

#define ARRAY_MAX_CAPACITY 100663319

#define ARRAY(t)         \
    struct               \
    {                    \
        size_t length;   \
        size_t capacity; \
        t* elements;     \
    }

#define ARRAY_INIT(arr)                                                       \
    do {                                                                      \
        assert((arr) != NULL);                                                \
        (arr)->length = 0;                                                    \
        next_prime_capacity(0, &(arr)->capacity);                             \
        (arr)->elements = malloc((arr)->capacity * sizeof(*(arr)->elements)); \
    } while(0)

#define ARRAY_FREE(arr)               \
    do {                              \
        assert((arr) != NULL);        \
                                      \
        if((arr)->elements != NULL) { \
            free((arr)->elements);    \
            (arr)->elements = NULL;   \
            (arr)->length = 0;        \
            (arr)->capacity = 0;      \
        }                             \
    } while(0)

#define ARRAY_COPY(dst, src)                        \
    do {                                            \
        assert((dst) != NULL);                      \
        assert((src) != NULL);                      \
        assert(sizeof(*(dst)) == sizeof(*(src)));   \
                                                    \
        for(size_t i = 0; i < (src)->length; i++) { \
            ARRAY_PUSH(dst, (src)->elements[i]);    \
        }                                           \
    } while(0)

#define ARRAY_EMPTY(arr) ((arr)->length == 0)

#define ARRAY_NTH_REF(arr, index, out)  \
    do {                                \
        assert((arr) != NULL);          \
        assert(index >= 0);             \
                                        \
        if(index >= (arr)->length)      \
            break;                      \
                                        \
        *out = &(arr)->elements[index]; \
    } while(0)

#define ARRAY_NTH(arr, index, out)     \
    do {                               \
        assert((arr) != NULL);         \
        assert(index >= 0);            \
                                       \
        if(index >= (arr)->length)     \
            break;                     \
                                       \
        *out = (arr)->elements[index]; \
    } while(0)

#define ARRAY_PUSH(arr, element)                                                            \
    do {                                                                                    \
        assert((arr) != NULL);                                                              \
                                                                                            \
        if((arr)->length >= (arr)->capacity) {                                              \
            size_t next_capacity;                                                           \
            if(!next_prime_capacity((arr)->capacity + 1, &next_capacity))                   \
                break;                                                                      \
                                                                                            \
            void* tmp = realloc((arr)->elements, next_capacity * sizeof(*(arr)->elements)); \
            if(tmp == NULL)                                                                 \
                break;                                                                      \
                                                                                            \
            (arr)->elements = tmp;                                                          \
            (arr)->capacity = next_capacity;                                                \
        }                                                                                   \
                                                                                            \
        (arr)->elements[(arr)->length++] = element;                                         \
    } while(0)

#define ARRAY_POP(arr, out)                               \
    do {                                                  \
        assert((arr) != NULL);                            \
        if(ARRAY_EMPTY(arr))                              \
            break;                                        \
                                                          \
        if((out) != NULL)                                 \
            memcpy((out),                                 \
                   (arr)->elements + ((arr)->length - 1), \
                   sizeof(*(arr)->elements));             \
                                                          \
        (arr)->length--;                                  \
    } while(0)

bool next_prime_capacity(size_t n,
                         size_t* out);

#endif