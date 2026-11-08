/* hash_table.c
 *      simple hash_table
 */

#include <stdlib.h>
#include <string.h>
#include "hash_table.h"

#define HASH_SIZE 10007 /* Must be a prime */
#define HASH_P1   29
#define HASH_P2   31

static int hash_table_hash(const void* key, const size_t sz_key) {
    int hash_res = 0;
    const unsigned char *pkey = (const unsigned char*)key;
    for(size_t i = 0; i < sz_key; ++i)
        hash_res = (hash_res * HASH_P1 + pkey[i]) % HASH_SIZE;
    return hash_res;
}

void hash_table_init(hash_table_t* this) {
    *this = (list_node_t*)malloc(sizeof(list_node_t) * HASH_SIZE);
    for(int i = 0; i < HASH_SIZE; ++i)
        list_node_init((*this) + i);
}

void hash_table_destroy(hash_table_t* this) { 
    for(int i = 0; i < HASH_SIZE; ++i) {
        list_node_t *p = (*this)[i].nxt;
        while(p != (*this) + i) {
            list_node_t *t = p; p = p->nxt;
            /* Notice: the bidirection list point breaks here, 
             * but does make sense because we are destroying it.
             *
             * List -> { key, ptr } 
             *            +    +----> managed externally
             *            |
             *            +---------> managed here
             */
            free(((hash_table_elem_t*)(t->ptr))->key);  /* key */
            free(t->ptr);                               /* hash_table elem */
            free(t);                                    /* the list_node */
        }
    }
    free(*this); *this = NULL;
}

hash_table_elem_t* hash_table_insert(
        hash_table_t* this,  /* Pointers to the hash_table */
        const void* key,    /* Using raw memory hashing */
        const size_t sz_key,/* Size of key*/
        void* ptr) {
    const int hash = hash_table_hash(key, sz_key);
    hash_table_elem_t* elem;
    elem = (hash_table_elem_t*)malloc(sizeof(hash_table_elem_t));
    void* elem_key = malloc(sz_key); memcpy(elem_key, key, sz_key);
    *elem = (hash_table_elem_t){.key = elem_key, .ptr = ptr };
    elem->ref_list_nd = list_node_insert_head((*this) + hash, elem);
    return elem;
}

hash_table_elem_t* hash_table_query(
        const hash_table_t* this, 
        const void* key, 
        const size_t sz_key) {
    const int hash = hash_table_hash(key, sz_key);
    for(list_node_t *p = (*this)[hash].nxt; p != (*this) + hash; p=p->nxt)
        if(!memcmp(((hash_table_elem_t*)(p->ptr))->key, key, sz_key))
            return (hash_table_elem_t*)(p->ptr);
    return NULL;
}

void hash_table_remove(hash_table_t *this, hash_table_elem_t *elem) {
    (void)(this);  /* This is unused here */
    list_node_remove(elem->ref_list_nd);
    free(elem->key);
    free(elem);
}
