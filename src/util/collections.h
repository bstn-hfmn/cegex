#ifndef CEGEX_UTIL_ARRAY_H
#define CEGEX_UTIL_ARRAY_H

#include <stdbool.h>
#include <stddef.h>

#define ARRAY_MAX_CAPACITY 100663319

#define ARRAY(t)         \
    struct               \
    {                    \
        size_t length;   \
        size_t capacity; \
        t* elements;     \
    }

#define ITERATOR(t)    \
    struct             \
    {                  \
        size_t index;  \
        size_t length; \
        t* elements;   \
    }

#define ITERATOR_INIT(it, src, n)       iterator_init((void**) &(it)->elements, &(it)->index, &(it)->length, (n), (void*) (src))
#define ITERATOR_EMPTY(it)              iterator_empty((it)->index, (it)->length)
#define ITERATOR_PEEK(it, out)          iterator_peek((void**) &(it)->elements, (it)->index, (it)->length, sizeof(*(it)->elements), (void*) (out))
#define ITERATOR_NEXT(it)               iterator_next(&(it)->index, (it)->length)
#define ITERATOR_CURRENT(it, out)       iterator_current((void**) &(it)->elements, (it)->index, (it)->length, sizeof(*(it)->elements), (void*) (out))

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

/*-------------------- iterator --------------------*/
void iterator_init(void** elements,
                   size_t* index,
                   size_t* length,
                   size_t capcity,
                   void* source);

bool iterator_empty(size_t index,
                    size_t length);

bool iterator_peek(void** elements,
                   size_t index,
                   size_t length,
                   size_t stride,
                   void* out);

bool iterator_next(size_t* index,
                   size_t length);

bool iterator_current(void** elements,
                      size_t index,
                      size_t length,
                      size_t stride,
                      void* out);

/*-------------------- array --------------------*/
void array_init(void** elements,
                size_t* length,
                size_t* capacity,
                size_t stride);

void array_free(void** elements,
                size_t* length,
                size_t* capacity);

bool array_empty(size_t length);

bool array_nth(void** elements,
               size_t length,
               size_t index,
               size_t stride,
               void* out);

bool array_nth_ref(void** elements,
                   size_t length,
                   size_t index,
                   size_t stride,
                   void** out);

bool array_grow(void** elements,
                size_t* capacity,
                size_t stride);

bool array_pop(void** elements,
               size_t* length,
               size_t stride,
               void* out);

bool array_push(void** elements,
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