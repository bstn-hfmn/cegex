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

#define ARRAY_INIT(arr)                 array_init((void**) &(arr)->elements, &(arr)->length, &(arr)->capacity, sizeof(*(arr)->elements))
#define ARRAY_FREE(arr)                 array_free((void**) &(arr)->elements, &(arr)->length, &(arr)->capacity)

#define ARRAY_EMPTY(arr)                array_empty((arr)->length)

#define ARRAY_NTH(arr, index, out)      array_nth((void**) &(arr)->elements, (arr)->length, (index), sizeof(*(arr)->elements), (void*) (out))
#define ARRAY_NTH_REF(arr, index, out)  array_nth_ref((void**) &(arr)->elements, (arr)->length, (index), sizeof(*(arr)->elements), (void**) (out))

#define ARRAY_POP(arr, out)             array_pop((void**) &(arr)->elements, &(arr)->length, sizeof(*(arr)->elements), (void*) (out))
#define ARRAY_PUSH(arr, el)             array_push((void**) &(arr)->elements, &(arr)->length, &(arr)->capacity, sizeof(*(arr)->elements), &(el))

#define ARRAY_CONTAINS(arr, needle, fn) array_contains((void**) &(arr)->elements, (arr)->length, sizeof(*(arr)->elements), &(needle), fn)

typedef bool (*comparator)(void*,
                           void*);

bool next_prime_capacity(size_t n,
                         size_t* out);

void array_init(void** elements,
                size_t* length,
                size_t* capacity,
                size_t stride);

void array_free(void** elements,
                size_t* length,
                size_t* capacity);

bool array_empty(size_t length);

void array_nth(void** elements,
               size_t length,
               size_t index,
               size_t stride,
               void* out);

void array_nth_ref(void** elements,
                   size_t length,
                   size_t index,
                   size_t stride,
                   void** out);

void array_grow(void** elements,
                size_t* capacity,
                size_t stride);

void array_pop(void** elements,
               size_t* length,
               size_t stride,
               void* out);

void array_push(void** elements,
                size_t* length,
                size_t* capacity,
                size_t stride,
                const void* element);

bool array_contains(void** elements,
                    size_t length,
                    size_t stride,
                    void* needle,
                    comparator fn);

#endif