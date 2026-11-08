/* list.h
 *      simple list
 */

#ifndef _H_LIST
#define _H_LIST

typedef struct _list_node_s {
    struct _list_node_s *pre, *nxt;
    void* ptr;
} list_node_t;

void list_node_init(list_node_t *nd);
list_node_t* list_node_insert(list_node_t *pre, void* ptr);
list_node_t* list_node_insert_head(list_node_t *nxt, void* ptr);
void list_node_remove(list_node_t *nd);

#endif // _H_LIST
