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

/* lru_insert:
 *      insert a new entry at the top. Keys are not checked. */
lru_node_t* lru_insert(lru_t* p, void* key, const size_t sz_key, void* ptr);

/* lru_access:
 *      bring the one to the top */
lru_node_t* lru_access(lru_t* p, void* key, const size_t sz_key);

/* lru_replace:
 *      insert, then remove the least recently used one when over max_size.
 *      Returns the removed pointer, or NULL if nothing was removed. */
void* lru_replace(lru_t* p, void* key, const size_t sz_key, void* ptr);

/* lru_remove:
 *      remove one with the pointer. */
void* lru_remove(lru_t* p, lru_node_t* p_lru_node);

#endif // _H_LRU
