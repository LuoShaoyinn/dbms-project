/* hash_table.c
 *      simple hash_table
 */

#include <stdlib.h>
#include <string.h>
#include "hash_table.h"

#define HASH_SIZE 10007 /* Must be a prime */

static uint64_t hash_table_hash(const void* key, const size_t sz_key) {
    /* FNV-1a uses unsigned wraparound; reduce to a bucket only once. */
    uint64_t hash_res = UINT64_C(14695981039346656037);
    const unsigned char *pkey = (const unsigned char*)key;
    for(size_t i = 0; i < sz_key; ++i) {
        hash_res ^= pkey[i];
        hash_res *= UINT64_C(1099511628211);
    }
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
    const uint64_t hash = hash_table_hash(key, sz_key);
    hash_table_elem_t* elem;
    elem = (hash_table_elem_t*)malloc(sizeof(hash_table_elem_t));
    void* elem_key = malloc(sz_key == 0 ? 1 : sz_key);
    if(sz_key != 0) memcpy(elem_key, key, sz_key);
    *elem = (hash_table_elem_t){.key = elem_key, .ptr = ptr,
        .sz_key = sz_key, .hash = hash};
    elem->ref_list_nd = list_node_insert_head((*this) + hash % HASH_SIZE, elem);
    return elem;
}

hash_table_elem_t* hash_table_query(
        const hash_table_t* this, 
        const void* key, 
        const size_t sz_key) {
    const uint64_t hash = hash_table_hash(key, sz_key);
    list_node_t *head = (*this) + hash % HASH_SIZE;
    for(list_node_t *p = head->nxt; p != head; p=p->nxt) {
        hash_table_elem_t *elem = (hash_table_elem_t*)p->ptr;
        if(elem->hash == hash && elem->sz_key == sz_key &&
                (sz_key == 0 || !memcmp(elem->key, key, sz_key)))
            return elem;
    }
    return NULL;
}

void hash_table_remove(hash_table_t *this, hash_table_elem_t *elem) {
    (void)(this);  /* This is unused here */
    list_node_remove(elem->ref_list_nd);
    free(elem->key);
    free(elem);
}
