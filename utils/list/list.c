/* list.c
 *      simple list
 */

#include <stdlib.h>
#include <string.h>
#include "list.h"


void list_node_init(list_node_t *nd) { 
    nd->pre = nd->nxt = nd; nd->ptr = NULL;
}

list_node_t* list_node_insert(list_node_t *pre, void* ptr) {
    list_node_t* p = (list_node_t*)malloc(sizeof(list_node_t));
    p->nxt = pre->nxt; p->pre = pre; p->ptr = ptr;
    pre->nxt->pre = p; pre->nxt = p;
    return p;
}

list_node_t* list_node_insert_head(list_node_t *nxt, void* ptr) {
    list_node_t* p = (list_node_t*)malloc(sizeof(list_node_t));
    p->nxt = nxt; p->pre = NULL; p->ptr = ptr; nxt->pre = p;
    return p;
}

void list_node_remove(list_node_t *nd) {
    nd->pre->nxt = nd->nxt; nd->nxt->pre = nd->pre; free(nd); 
}

