#include "collections.h"
#include <stdlib.h>

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