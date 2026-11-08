/* hash_table.h
 *      simple hash_table
 */

#ifndef _H_HASH_TABLE
#define _H_HASH_TABLE

#include <stddef.h>
#include "utils/list/list.h"

typedef list_node_t* hash_table_t;
typedef struct _hash_table_element_s { 
    list_node_t *ref_list_nd;
    void *key, *ptr;
} hash_table_elem_t;

void hash_table_init(hash_table_t* this); 
void hash_table_destroy(hash_table_t* this); 
hash_table_elem_t* hash_table_insert(
        hash_table_t* this, /* Pointers to the hash_table */
        const void* key,    /* Using raw memory hashing */
        const size_t sz_key,/* Size of key*/
        void* ptr);
hash_table_elem_t* hash_table_query(
        const hash_table_t* this, 
        const void* key, 
        const size_t sz_key);
void hash_table_remove(hash_table_t* this, hash_table_elem_t *elem);

#endif // _H_HASH_TABLE

