/* lru.h
 *      simple lru algorithm
 */

#ifndef _H_LRU
#define _H_LRU

#include "utils/list/list.h"
#include "utils/hash_table/hash_table.h"

/*   LIST
 *    ||
 * +------+
 * |      |         LRU_NODE
 * |  ptr-+---> { ref_ht_elem, -------> hash_table_elem
 * |      | <---- ref_list_nd }
 * +------+                 
 *    ||
 * +------+
 * |      |
 * |  ptr-+---
 * |      |
 * +------+
 *    ||
 */

typedef struct _lru_node_s {
    hash_table_elem_t*  ref_ht_elem;
    list_node_t*        ref_list_nd;
    void*               ptr;
} lru_node_t;

typedef struct _lru_s {
    list_node_t *       head;
    list_node_t *       tail;
    hash_table_t *      hash_table; 
    size_t              max_size;
    size_t              size;
} lru_t;

void lru_init(lru_t* p, const size_t max_size);
void lru_destroy(lru_t* p);

/* lru_access:
 *      access key and bring it to the top, creating the node when missing.
 *      ptr_out: receives the created node, or NULL if the key already existed.
 *      rm_ptr:  receives the removed pointer when over max_size. */
lru_node_t* lru_access(lru_t* p, void* key, const size_t sz_key,
        void **ptr_out, void **rm_ptr);

#endif // _H_LRU