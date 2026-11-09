/* test_lru.c
 *      isolated tests for utils/lru
 */

#include <assert.h>

#include "lru/lru.h"

static void test_init_empty(void) {
    lru_t lru;

    lru_init(&lru, 4);
    assert(lru.max_size == 4);
    assert(lru.size == 0);
    assert(lru.head != NULL);
    assert(lru.tail == lru.head);
    lru_destroy(&lru);
}

static void test_access_creates_and_hits(void) {
    lru_t lru;
    int k1 = 1, k2 = 2, k3 = 3;
    int v1 = 10, v2 = 20, v3 = 30;
    void *created = NULL;
    void *removed = NULL;

    lru_init(&lru, 4);

    /* Miss: a new node is created, but its ptr is left to the caller. */
    lru_node_t *n1 = lru_access(&lru, &k1, sizeof k1, &created, &removed);
    assert(n1 != NULL);
    assert(created == n1);
    assert(removed == NULL);
    assert(n1->ptr == NULL);
    n1->ptr = &v1;

    lru_node_t *n2 = lru_access(&lru, &k2, sizeof k2, &created, &removed);
    assert(n2 != NULL && n2 != n1);
    assert(created == n2);
    assert(removed == NULL);
    n2->ptr = &v2;

    lru_node_t *n3 = lru_access(&lru, &k3, sizeof k3, &created, &removed);
    assert(n3 != NULL && n3 != n2);
    assert(created == n3);
    assert(removed == NULL);
    n3->ptr = &v3;

    assert(lru.size == 3);
    assert(lru.head->nxt == n3->ref_list_nd);
    assert(lru.tail == n1->ref_list_nd);

    /* Hit: no node is created and the accessed node moves to the top. */
    created = (void*)0x1;
    assert(lru_access(&lru, &k2, sizeof k2, &created, &removed) == n2);
    assert(created == NULL);
    assert(removed == NULL);
    assert(lru.head->nxt == n2->ref_list_nd);
    assert(lru.tail == n1->ref_list_nd);
    assert(n2->ptr == &v2);

    lru_destroy(&lru);
}

static void test_access_evicts_lru(void) {
    lru_t lru;
    int k1 = 1, k2 = 2, k3 = 3;
    int v1 = 10, v2 = 20, v3 = 30;
    void *created = NULL;
    void *removed = NULL;

    lru_init(&lru, 2);

    lru_node_t *n1 = lru_access(&lru, &k1, sizeof k1, &created, &removed);
    n1->ptr = &v1;
    lru_node_t *n2 = lru_access(&lru, &k2, sizeof k2, &created, &removed);
    n2->ptr = &v2;

    /* Make k1 the most recently used entry, leaving k2 at the tail. */
    assert(lru_access(&lru, &k1, sizeof k1, &created, &removed) == n1);
    assert(created == NULL);

    lru_node_t *n3 = lru_access(&lru, &k3, sizeof k3, &created, &removed);
    assert(n3 != NULL);
    assert(created == n3);
    assert(removed == &v2);
    n3->ptr = &v3;

    assert(lru.size == 2);
    assert(lru.head->nxt == n3->ref_list_nd);
    assert(lru.tail == n1->ref_list_nd);

    lru_destroy(&lru);
}

static void test_access_without_rm_ptr(void) {
    lru_t lru;
    int k1 = 1, k2 = 2;
    int v1 = 10, v2 = 20;
    void *created = NULL;

    lru_init(&lru, 1);
    lru_node_t *n1 = lru_access(&lru, &k1, sizeof k1, &created, NULL);
    n1->ptr = &v1;

    /* An eviction still happens even when the removed pointer is not wanted. */
    lru_node_t *n2 = lru_access(&lru, &k2, sizeof k2, &created, NULL);
    assert(created == n2);
    n2->ptr = &v2;
    assert(lru.size == 1);
    assert(lru.head->nxt == n2->ref_list_nd);
    assert(lru.tail == n2->ref_list_nd);

    lru_destroy(&lru);
}

static void test_destroy_non_empty(void) {
    lru_t lru;
    int k1 = 1;
    int v1 = 10;
    void *created = NULL;
    void *removed = NULL;

    lru_init(&lru, 2);
    lru_node_t *n1 = lru_access(&lru, &k1, sizeof k1, &created, &removed);
    n1->ptr = &v1;

    /* Destroying a non-empty LRU frees all internal nodes. */
    lru_destroy(&lru);
}

int main(void) {
    test_init_empty();
    test_access_creates_and_hits();
    test_access_evicts_lru();
    test_access_without_rm_ptr();
    test_destroy_non_empty();
    return 0;
}