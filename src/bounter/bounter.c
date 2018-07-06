#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cms_log8.c"

#define BITMASK(nbits) ((nbits) == 64 ? 0xffffffffffffffff : \
                       (1ULL << (nbits)) - 1ULL)

/*return the integer representation of the base */
static inline uint8_t map_base(char base) {
    switch(base) {
        case 'A': case 'a': { return 0; }
        case 'C': case 'c': { return 1; }
        case 'G': case 'g': { return 2; }
        case 'T': case 't': { return 3; }
        default: { return 4; }
    }
}

#define NON_ACGT(b) (b < 0 || b > 3)

typedef struct {
    uint64_t u64s[4];
} b256;

/**
 * Set to all 0s.
 */
static void b256_clear(b256 *b) {
    memset(b, 0, 32);
}

/**
 * Left-shift by 2 bits and mask so that DNA string is no longer than k.  Put
 * result in dst.
 */
static void b256_lshift_and_mask(b256 *b, size_t k) {
    k *= 2;
    int word = (int)(k >> 6);
    for(int i = 3; i > 0; i--) {
        b->u64s[i] = (b->u64s[i] << 2) | (b->u64s[i-1] >> 62);
    }
    b->u64s[0] <<= 2;
    b->u64s[word] &= BITMASK(k & 63);
}

/**
 * Left-shift by 2 bits, in place.
 */
static void b256_lshift(b256 *b) {
    for(int i = 3; i > 0; i--) {
        b->u64s[i] = (b->u64s[i] << 2) | (b->u64s[i-1] >> 62);
    }
    b->u64s[0] <<= 2;
}

/**
 * Right-shift by 2 bits, in place.
 */
static void b256_rshift_2(b256 *b) {
    for(int i = 0; i < 3; i++) {
        b->u64s[i] = (b->u64s[i] >> 2) | (b->u64s[i+1] << 62);
    }
    b->u64s[3] >>= 2;
}

/**
 * OR the given value into the low bits of b, in place.
 */
static void b256_or_low(b256 *b, uint8_t val) {
    b->u64s[0] |= val;
}

/**
 * OR the given value into given bitpair of b, in place.
 */
static void b256_or_at(b256 *b, uint8_t val, int k) {
    assert(k < 128);
    assert(val < 4);
    k *= 2;
    size_t word = k >> 6, bitoff = k & 63;
    b->u64s[word] |= ((uint64_t)val << bitoff);
}

/**
 * Reverse complement the length-k DNA string in src, putting result in dst.
 */
static void b256_revcomp(const b256 *src, b256 *dst, size_t k) {
    // presumably *dst is all 0s
    assert(k > 0);
    size_t lo_word = 0, lo_bitoff = 0;
    size_t hi_word = ((k-1) * 2) >> 6;
    size_t hi_bitoff = ((k-1) * 2) & 63;
    while(1) {
        assert(lo_word >= 0);
        assert(lo_word < 4);
        assert(hi_word >= 0);
        assert(hi_word < 4);
        assert(lo_bitoff >= 0);
        assert(lo_bitoff < 64);
        assert(hi_bitoff >= 0);
        assert(hi_bitoff < 64);
        int base = (src->u64s[lo_word] >> lo_bitoff) & 3;
        uint64_t rcbase = base ^ 3;
        assert(rcbase < 4);
        dst->u64s[hi_word] |= rcbase << hi_bitoff;
        if(lo_bitoff == 62) {
            lo_bitoff = 0;
            lo_word++;
        } else {
            lo_bitoff += 2;
        }
        if(hi_bitoff == 0) {
            if(hi_word == 0) {
                break;
            }
            hi_word--;
            hi_bitoff = 62;
        } else {
            hi_bitoff -= 2;
        }
    }
}

/**
 * Return the pointer for whichever k-mer is minimal.
 */
static b256 *b256_min(b256 *a, b256 *b) {
    for(int i = 3; i >= 0; i--) {
        if(a->u64s[i] < b->u64s[i]) {
            return a;
        } else if(a->u64s[i] != b->u64s[i]) {
            return b;
        }
    }
    return a; // equal
}

/**
 * Given a string, extract every k-mer, canonicalize, and add to the QF.
 */
int bounter_string_injest(
    CMS_Log8 *sketch,       // bounter sketch
    int k,                  // k-mer length
    const char *read,       // string whose k-mers to add
    size_t read_len)        // length of string
{
    if(k > 127) {
        fprintf(stderr, "No support for k-mers sizes over 127; k-mer size "
                "%u was specified\n", k);
        return -1;
    }
    int nadded = 0;
    int i = 0;
    for(; (size_t)i < read_len; i++) {
        b256 first, first_rev, *item = NULL;
        b256_clear(&first);
        b256_clear(&first_rev);
        // Phase 1: get initial k-mer
        int do_continue = 0;
        for(int start = i; i < start + k; i++) { // First kmer
            uint8_t curr = map_base(read[i]);
            if(NON_ACGT(curr)) {
                do_continue = 1;
                break;
            }
            b256_or_low(&first, curr);
            b256_lshift(&first);
        }
        if(do_continue) {
            continue;
        }
        b256_rshift_2(&first);
        b256_revcomp(&first, &first_rev, k);
        item = b256_min(&first, &first_rev);
        CMS_Log8_increment_obj(sketch, (char *)item, sizeof(*item), 1);
        nadded++;
        // Phase 2: get all subsequent k-mers
        b256 next = first, next_rev = first_rev;
        for(; (size_t)i < read_len; i++) {
            uint8_t curr = map_base(read[i]);
            if(NON_ACGT(curr)) {
                break;
            }
            b256_lshift_and_mask(&next, k);
            b256_or_low(&next, curr);
            uint64_t tmp = curr ^ 3;
            b256_rshift_2(&next_rev);
            b256_or_at(&next_rev, tmp, k - 1);
            item = b256_min(&next, &next_rev);
            CMS_Log8_increment_obj(sketch, (char *)item, sizeof(*item), 1);
            nadded++;
        }
    }
    return nadded;
}

void *bounter_new(int width, int depth) {
    CMS_Log8 *sketch = calloc(sizeof(CMS_Log8), 1);
    CMS_Log8_init(sketch, width, depth);
    return sketch;
}

void bounter_delete(void *sketch) {
    CMS_Log8_dealloc((CMS_Log8 *)sketch);
    free(sketch);
}

/**
 * Assumes no locking is needed, i.e. that no other thread
 * is trying to update the CQF.
 */
int bounter_string_query(
    CMS_Log8 *sketch,       // bounter sketch
    int k,                  // k-mer length
    const char *read_orig,  // query string
    size_t read_len_orig,   // length of query
    int64_t *count_array,   // result array
    size_t count_array_len) // length of result array
{
    int64_t *cur_count = count_array;
    const char *read = read_orig;
    size_t read_len = read_len_orig;
    do {
        if(read_len < (size_t)k) {
            return (int)(cur_count - count_array);
        }
        b256 first, first_rev, *item = NULL;
        b256_clear(&first);
        b256_clear(&first_rev);
        int do_continue = 0;
        for(int i = 0; i < k; i++) {
            uint8_t curr = map_base(read[i]);
            if(NON_ACGT(curr)) {
                // append -1s for k-mers that include a non-ACGT
                for(int j = 0; j <= i && j + k <= (int)read_len; j++) {
                    *cur_count++ = -1;
                    assert((size_t)(cur_count - count_array) <= count_array_len);
                }
                read += (i+1);
                read_len -= (i+1);
                do_continue = 1;
                break;
            }
            b256_or_low(&first, curr);
            b256_lshift(&first);
        }
        if(do_continue) {
            continue;
        }
        
        b256_rshift_2(&first);
        b256_revcomp(&first, &first_rev, k);
        item = b256_min(&first, &first_rev);
        int64_t count = CMS_Log8_getitem(sketch, (char *)item, sizeof(*item));
        *cur_count++ = count;
        if((size_t)(cur_count - count_array) > count_array_len) {
            fprintf(stderr, "Wrote off end of count array\n");
            exit(1);
        }
        
        b256 next = first, next_rev = first_rev;
        read += k;
        if(read_len < (size_t)k) {
            fprintf(stderr, "Query string became too short\n");
            exit(1);
        }
        read_len -= k;
        
        for(uint32_t i = 0; i < read_len; i++) { //next kmers
            uint8_t curr = map_base(read[i]);
            if(NON_ACGT(curr)) {
                for(int j = 0; j < k && (size_t)(cur_count - count_array) < count_array_len; j++) {
                    *cur_count++ = -1;
                }
                read++;
                read_len--;
                do_continue = 1;
                break;
            }
            b256_lshift_and_mask(&next, k);
            b256_or_low(&next, curr);
            uint64_t tmp = curr ^ 3;
            b256_rshift_2(&next_rev);
            b256_or_at(&next_rev, tmp, k - 1);
            item = b256_min(&next, &next_rev);
            count = CMS_Log8_getitem(sketch, (char *)item, sizeof(*item));
            *cur_count++ = count;
            if((size_t)(cur_count - count_array) > count_array_len) {
                fprintf(stderr, "Wrote off end of count array\n");
                exit(1);
            }
        }
        if(!do_continue) {
            break;
        }
    } while(1);
    return (int)(cur_count - count_array);
}

#ifdef BOUNTER_TEST_MAIN
static void quick_tests(unsigned seed) {
    CMS_Log8 sketch;
    {
        int64_t results[4];
        const char *text = "ACGTACG";
        //                  0123
        //                  ACGT (1)
        //                   CGTA (2)
        //                    GTAC (1)
        //                     TACG (2)
        for(int i = 0; i < 3; i++) {
            CMS_Log8_init(&sketch, 1024, 10);
            int nadded = bounter_string_injest(&sketch, 4, text, i + 4);
            assert(i+1 == nadded);
            bounter_string_query(&sketch, 4, text, i+4, results, i+1);
            for(int j = 0; j <= i; j++) {
                assert(results[j] == 1);
            }
            CMS_Log8_dealloc(&sketch);
        }
        CMS_Log8_init(&sketch, 1024, 10);
        bounter_string_injest(&sketch, 4, text, 7);
        bounter_string_query(&sketch, 4, text, 7, results, 4);
        assert(results[0] == 1);
        assert(results[1] == 2);
        assert(results[2] == 1);
        assert(results[3] == 2);
        CMS_Log8_dealloc(&sketch);
    }

    {
        const char *text  = "TCCCGGGAGGGA";
        const char *query = "TCCCNGGGA";
        int64_t results[6];
        CMS_Log8_init(&sketch, 1024, 10);
        bounter_string_injest(&sketch, 4, text, 12);
        bounter_string_query(&sketch, 4, query, 9, results, 6);
        assert(results[0] == 3);
        assert(results[5] == 3);
        CMS_Log8_dealloc(&sketch);
    }

    {
        CMS_Log8_init(&sketch, 1024, 10);
        srand(seed);
        int textlen = 100000;
        int ksize = 60;
        char *text = (char*)malloc(textlen + ksize);
        char *cur = text;
        for(int i = 0; i < textlen + ksize - 1; i++) {
            *cur++ = "ACGT"[rand() % 4];
        }
        text[textlen+ksize-1] = '\0';
        bounter_string_injest(&sketch, 60, text, textlen + ksize - 1);
        CMS_Log8_dealloc(&sketch);
    }
}

// Example args: 4 40 40 ACGTACG ACGT
int main(int argc, char *argv[]) {

    if(argc < 5) {
        fprintf(stderr, "Need at least 4 args\n");
        return 1;
    }

    int ksize = atoi(argv[1]), width  = atoi(argv[2]), depth = atoi(argv[3]);
    fprintf(stderr, "ksize: %d\n", ksize);
    fprintf(stderr, "width: %d\n", width);
    fprintf(stderr, "depth: %d\n", depth);
    unsigned seed = 777;

    quick_tests(seed);

    CMS_Log8 sketch;
    CMS_Log8_init(&sketch, width, depth);

    const char *ref = argv[4];
    fprintf(stderr, "Building reference from: %s\n", ref);
    bounter_string_injest(&sketch, ksize, ref, strlen(ref));

    for(int i = 5; i < argc; i++) {
        size_t len = strlen(argv[i]);
        size_t results_len = len - ksize + 1;
        int64_t *results = (int64_t*)malloc(sizeof(int64_t) * results_len);
        if(results == NULL) {
            fprintf(stderr, "ERROR: could not allocate results\n");
            return 1;
        }
        int nres = bounter_string_query(&sketch, ksize, argv[i], len, results, results_len);
        assert(nres == (int)results_len);
        for(int j = 0; j < nres; j++) {
            fprintf(stderr, "[%d]: %lld\n", j, results[j]);
        }
        free(results);
    }
}
#endif //BOUNTER_TEST_MAIN