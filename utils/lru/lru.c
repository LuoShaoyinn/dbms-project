/* lru.c
 *      simple lru algorithm
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lru.h"
#include "list/list.h"
#include "hash_table/hash_table.h"

/* Internal helpers, hidden from the header. */
static lru_node_t* _lru_insert(lru_t* this, void* key, const size_t sz_key, void* ptr);
static lru_node_t* _lru_bring_top(lru_t* this, void* key, const size_t sz_key);
static void* _lru_remove(lru_t* this, lru_node_t* p_lru_node);

void lru_init(lru_t* p, const size_t max_size) {
    /* Create the hash table */
    p->hash_table = (hash_table_t*)malloc(sizeof(hash_table_t));
    hash_table_init(p->hash_table);
    /* The head is a sentinel, the tail is the least recently used node. */
    p->head = (list_node_t*)malloc(sizeof(list_node_t));
    list_node_init(p->head);
    p->tail = p->head;
    /* set max size and size */
    p->max_size = max_size; p->size = 0;
}

static lru_node_t* _lru_insert(lru_t* this, void* key, const size_t sz_key, void* ptr) {
    lru_node_t *p_lru_node = (lru_node_t*)malloc(sizeof(lru_node_t));
    p_lru_node->ptr = ptr;
    p_lru_node->ref_ht_elem = hash_table_insert(this->hash_table, key, sz_key, p_lru_node);
    p_lru_node->ref_list_nd = list_node_insert(this->head, p_lru_node);
    this->tail = this->head->pre;
    ++this->size;
    return p_lru_node;
}

static lru_node_t* _lru_bring_top(lru_t* this, void* key, const size_t sz_key) {
    hash_table_elem_t *hte = hash_table_query(this->hash_table, key, sz_key);
    if(hte == NULL) return NULL; /* Not in hash_table = not in the LRU */
    lru_node_t *p_lru_node = (lru_node_t*)(hte->ptr);
    list_node_t *nd = p_lru_node->ref_list_nd;
    if(nd != this->head->nxt) {
        /* Bring the node to the top, reusing its list node. */
        list_node_unlink(nd);
        nd->pre = this->head; nd->nxt = this->head->nxt;
        this->head->nxt->pre = nd; this->head->nxt = nd;
        p_lru_node->ref_list_nd = nd;
    }
    this->tail = this->head->pre;
    return p_lru_node;
}

static void* _lru_remove(lru_t* this, lru_node_t* p_lru_node) {
    void *ptr = p_lru_node->ptr; 
    list_node_remove(p_lru_node->ref_list_nd);
    hash_table_remove(this->hash_table, p_lru_node->ref_ht_elem);
    free(p_lru_node);
    --this->size;
    this->tail = this->head->pre;
    return ptr;
}

void lru_destroy(lru_t* p) {
    while(p->size > 0)
        _lru_remove(p, (lru_node_t*)(p->tail->ptr));
    hash_table_destroy(p->hash_table);
    free(p->hash_table);
    free(p->head);
}

lru_node_t* lru_access(lru_t* this, void* key, const size_t sz_key,
        void **ptr_out, void **rm_ptr) {
    *ptr_out = NULL;
    if(rm_ptr != NULL) *rm_ptr = NULL;

    lru_node_t *p_lru_node = _lru_bring_top(this, key, sz_key);
    if(p_lru_node != NULL) return p_lru_node; /* Already in the LRU */

    /* Not found: create one, the caller fills p_lru_node->ptr if needed. */
    p_lru_node = _lru_insert(this, key, sz_key, NULL);
    *ptr_out = p_lru_node;

    if(this->max_size > 0 && this->size > this->max_size) {
        void *ptr = _lru_remove(this, (lru_node_t*)(this->tail->ptr));
        if(rm_ptr != NULL) *rm_ptr = ptr;
    }
    return p_lru_node;
}