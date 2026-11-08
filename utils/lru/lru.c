/* lru.c
 *      simple lru algorithm
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lru.h"
#include "list/list.h"
#include "hash_table/hash_table.h"

void lru_init(lru_t* p, const size_t max_size) {
    p = (lru_t*)malloc(sizeof(lru_t));
    /* Create the hash table */
    p->hash_table = (hash_table_t*)malloc(sizeof(hash_table_t));
    hash_table_init(p->hash_table);
    /* set max size and size */
    p->max_size = max_size; p->size = 0;
    p->head = p->tail = NULL;
}

void lru_destroy(lru_t* p) {
    if(p->head != NULL || p->tail != NULL) {
        perror("Can not destroy a LRU object which isn't empty. ");
        exit(-1);
    }
    hash_table_destroy(p->hash_table);
    free(p->hash_table);
    free(p);
}

lru_node_t* lru_insert(lru_t* this, void* key, const size_t sz_key, void* ptr) {
    lru_node_t *p_lru_node = (lru_node_t*)malloc(sizeof(lru_node_t));
    p_lru_node->ref_ht_elem = hash_table_insert(this->hash_table, key, sz_key, p_lru_node);
    p_lru_node->ref_list_nd = list_node_insert_head(this->head, p_lru_node);
    p_lru_node->ptr = ptr;
    return p_lru_node;
}

lru_node_t* lru_access(lru_t* this, void* key, const size_t sz_key) {
    hash_table_elem_t *hte = hash_table_query(this->hash_table, key, sz_key);
    if(hte == NULL) return NULL; /* Not in hash_table = not in the LRU */
    lru_node_t *p_lru_node = (lru_node_t*)(hte->ptr);
    list_node_remove(p_lru_node->ref_list_nd);
    this->head = list_node_insert_head(this->head, p_lru_node);
    return p_lru_node;
}

/* lru_remove:  
 *      remove one with the pointer. nd can be p->head or p->tail. */
void* lru_remove(lru_t* this, lru_node_t* p_lru_node) {
    list_node_remove(p_lru_node->ref_list_nd);
    hash_table_remove(this->hash_table, p_lru_node->ref_ht_elem);
    void* ptr = p_lru_node->ptr; 
    free(p_lru_node);
    return ptr;
}

