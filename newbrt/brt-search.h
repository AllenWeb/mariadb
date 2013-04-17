#ifndef BRT_SEARCH_H
#define BRT_SEARCH_H

enum brt_search_direction_e {
    BRT_SEARCH_LEFT = 1,  /* search left -> right, finds min xy as defined by the compare function */
    BRT_SEARCH_RIGHT = 2, /* search right -> left, finds max xy as defined by the compare function */
};

struct brt_search;

/* the search compare function should return 0 for all xy < kv and 1 for all xy >= kv 
   the compare function should be a step function from 0 to 1 for a left to right search
   and 1 to 0 for a right to left search */

typedef int (*brt_search_compare_func_t)(struct brt_search */*so*/, DBT */*x*/, DBT */*y*/);

/* the search object contains the compare function, search direction, and the kv pair that
   is used in the compare function.  the context is the user's private data */

typedef struct brt_search {
    brt_search_compare_func_t compare;
    enum brt_search_direction_e direction;
    DBT *k;
    DBT *v;
    void *context;
} brt_search_t;

/* initialize the search compare object */
static inline brt_search_t *brt_search_init(brt_search_t *so, brt_search_compare_func_t compare, enum brt_search_direction_e direction, DBT *k, DBT *v, void *context) {
    so->compare = compare; so->direction = direction; so->k = k; so->v = v; so->context = context;
    return so;
}

#endif
