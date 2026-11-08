/* test_list.c
 *      isolated tests for utils/list
 */

#include <assert.h>
#include <stdlib.h>

#include "list/list.h"

static void test_init(void) {
    list_node_t nd;
    list_node_init(&nd);
    assert(nd.pre == &nd);
    assert(nd.nxt == &nd);
    assert(nd.ptr == NULL);
}

static void test_insert(void) {
    list_node_t head;
    int a = 1, b = 2, c = 3;
    list_node_init(&head);

    list_node_t *a_nd = list_node_insert(&head, &a);
    list_node_t *b_nd = list_node_insert(a_nd, &b);
    list_node_t *c_nd = list_node_insert(&head, &c);

    /* Forward: head -> c -> a -> b -> head. */
    assert(head.nxt == c_nd);
    assert(c_nd->nxt == a_nd);
    assert(a_nd->nxt == b_nd);
    assert(b_nd->nxt == &head);

    /* Backward: head -> b -> a -> c -> head. */
    assert(head.pre == b_nd);
    assert(b_nd->pre == a_nd);
    assert(a_nd->pre == c_nd);
    assert(c_nd->pre == &head);

    assert(c_nd->ptr == &c);
    assert(a_nd->ptr == &a);
    assert(b_nd->ptr == &b);

    list_node_remove(c_nd);
    list_node_remove(a_nd);
    list_node_remove(b_nd);
    assert(head.nxt == &head);
    assert(head.pre == &head);
}

static void test_insert_head(void) {
    list_node_t head;
    int a = 1, b = 2, c = 3;
    list_node_init(&head);

    list_node_t *a_nd = list_node_insert_head(&head, &a);
    list_node_t *b_nd = list_node_insert_head(&head, &b);
    list_node_t *c_nd = list_node_insert_head(&head, &c);

    /* Forward: head -> a -> b -> c -> head (inserted before head each time). */
    assert(head.nxt == a_nd);
    assert(a_nd->nxt == b_nd);
    assert(b_nd->nxt == c_nd);
    assert(c_nd->nxt == &head);

    /* Backward: head -> c -> b -> a -> head. */
    assert(head.pre == c_nd);
    assert(c_nd->pre == b_nd);
    assert(b_nd->pre == a_nd);
    assert(a_nd->pre == &head);

    list_node_remove(c_nd);
    list_node_remove(b_nd);
    list_node_remove(a_nd);
    assert(head.nxt == &head);
    assert(head.pre == &head);
}

static void test_unlink(void) {
    list_node_t head;
    int a = 1, b = 2, c = 3;
    list_node_init(&head);

    list_node_t *a_nd = list_node_insert_head(&head, &a);
    list_node_t *b_nd = list_node_insert_head(&head, &b);
    list_node_t *c_nd = list_node_insert_head(&head, &c);

    /* Forward: head -> a -> b -> c -> head. */
    list_node_unlink(b_nd);
    assert(head.nxt == a_nd);
    assert(a_nd->nxt == c_nd);
    assert(c_nd->nxt == &head);
    assert(c_nd->pre == a_nd);
    assert(a_nd->pre == &head);
    assert(b_nd->pre == NULL);
    assert(b_nd->nxt == NULL);

    /* Unlink the last and the first node as well. */
    list_node_unlink(c_nd);
    assert(head.nxt == a_nd);
    assert(a_nd->nxt == &head);
    assert(head.pre == a_nd);
    list_node_unlink(a_nd);
    assert(head.nxt == &head);
    assert(head.pre == &head);

    free(a_nd);
    free(b_nd);
    free(c_nd);
}

int main(void) {
    test_init();
    test_insert();
    test_insert_head();
    test_unlink();
    return 0;
}
