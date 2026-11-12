#include "collections.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static const size_t prime_capacities[] = {
    53, 97, 193, 389, 769, 1543, 3079, 6151,
    12289, 24593, 49157, 98317, 196613, 393241,
    786433, 1572869, 3145739, 6291469, 12582917,
    25165843, 50331653, 100663319
};

bool next_prime_capacity(size_t n,
                         size_t* out)
{
    if(out == NULL)
        return false;

    size_t length = sizeof(prime_capacities) / sizeof(prime_capacities[0]);

    size_t lo = 0;
    size_t hi = length;
    while(lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if(prime_capacities[mid] < n)
            lo = mid + 1;
        else
            hi = mid;
    }

    if(length == lo)
        return false;

    *out = prime_capacities[lo];
    return true;
}

void iterator_init(void** elements,
                   size_t* index,
                   size_t* length,
                   size_t capcity,
                   void* source)
{
    assert(elements != NULL);
    assert(length != NULL);
    assert(index != NULL);
    assert(source != NULL);

    *elements = source;
    *length = capcity;
    *index = 0;
}

bool iterator_empty(size_t index,
                    size_t length)
{
    return index >= length;
}

cegex_status iterator_peek(void** elements,
                           size_t index,
                           size_t length,
                           size_t stride,
                           void* out)
{
    assert(elements != NULL);

    if(index + 1 >= length)
        return CEGEX_ERR_LIMIT;

    if(out != NULL) {
        memcpy((char*) out, (char*) *elements + (index + 1) * stride, stride);
    }

    return CEGEX_OK;
}

cegex_status iterator_next(size_t* index,
                           size_t length)
{
    assert(index != NULL);
    if(*index >= length)
        return CEGEX_ERR_LIMIT;

    ++(*index);
    if(*index < length) {
        return CEGEX_OK;
    } else {
        return CEGEX_ERR_LIMIT;
    }
}

cegex_status iterator_current(void** elements,
                              size_t index,
                              size_t length,
                              size_t stride,
                              void* out)
{
    assert(elements != NULL);

    if(index >= length)
        return CEGEX_ERR_LIMIT;

    if(out != NULL) {
        memcpy((char*) out, (char*) *elements + index * stride, stride);
    }

    return CEGEX_OK;
}

cegex_status array_init(void** elements,
                        size_t* length,
                        size_t* capacity,
                        size_t stride)
{
    assert(elements != NULL);
    assert(length != NULL);
    assert(capacity != NULL);

    *length = 0;
    if(!next_prime_capacity(0, capacity)) {
        return CEGEX_ERR_LIMIT;
    }

    *elements = malloc(*capacity * stride);
    if(*elements == NULL) {
        return CEGEX_ERR_MEMORY;
    }

    return CEGEX_OK;
}

void array_free(void** elements,
                size_t* length,
                size_t* capacity)
{
    assert(elements != NULL);

    if(*elements != NULL) {
        free(*elements);
        *elements = NULL;
    }

    if(length != NULL)
        *length = 0;
    if(capacity != NULL)
        *capacity = 0;
}

bool array_empty(size_t length)
{
    return length == 0;
}

cegex_status array_nth(void** elements,
                       size_t length,
                       size_t index,
                       size_t stride,
                       void* out)
{
    assert(elements != NULL);
    assert(out != NULL);

    if(index >= length) {
        return CEGEX_ERR_LIMIT;
    }

    memcpy((char*) out, (char*) *elements + index * stride, stride);
    return CEGEX_OK;
}

cegex_status array_nth_ref(void** elements,
                           size_t length,
                           size_t index,
                           size_t stride,
                           void** out)
{
    assert(elements != NULL);
    assert(out != NULL);

    if(index >= length) {
        *out = NULL;
        return CEGEX_ERR_LIMIT;
    }

    *out = (char*) *elements + (index * stride);
    return CEGEX_OK;
}

cegex_status array_grow(void** elements,
                        size_t* capacity,
                        size_t stride)
{
    size_t next_capacity;
    if(!next_prime_capacity((*capacity) + 1, &next_capacity)) {
        return CEGEX_ERR_LIMIT;
    }

    void* tmp = realloc(*elements, next_capacity * stride);
    if(tmp == NULL) {
        return CEGEX_ERR_MEMORY;
    }

    *elements = tmp;
    *capacity = next_capacity;
    return CEGEX_OK;
}

cegex_status array_pop(void** elements,
                       size_t* length,
                       size_t stride,
                       void* out)
{
    assert(elements != NULL);
    assert(length != NULL);

    if(array_empty(*length))
        return CEGEX_ERR_INVALID_ARGUMENT;

    if(out != NULL) {
        memcpy((char*) out, (char*) *elements + (((*length) - 1) * stride), stride);
    }

    (*length)--;
    return CEGEX_OK;
}

cegex_status array_push(void** elements,
                        size_t* length,
                        size_t* capacity,
                        size_t stride,
                        const void* element)
{
    assert(elements != NULL);
    assert(length != NULL);
    assert(capacity != NULL);
    assert(element != NULL);

    cegex_status status = CEGEX_OK;

    if(*length >= *capacity) {
        CEGEX_TRY(array_grow(elements, capacity, stride));
    }

    memcpy((char*) *elements + (*length * stride), element, stride);
    (*length)++;

    return CEGEX_OK;

fail:
    return status;
}

bool array_contains(void** elements,
                    size_t length,
                    size_t stride,
                    void* needle,
                    comparator fn)
{
    assert(elements != NULL);
    assert(needle != NULL);

    for(size_t i = 0; i < length; i++) {
        char* element = (char*) *elements + i * stride;
        if(fn(element, needle))
            return true;
    }

    return false;
}