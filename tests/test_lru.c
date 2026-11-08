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

static void test_insert_access_remove(void) {
    lru_t lru;
    int k1 = 1, k2 = 2, k3 = 3;
    int v1 = 10, v2 = 20, v3 = 30;

    lru_init(&lru, 4);
    lru_node_t *n1 = lru_insert(&lru, &k1, sizeof k1, &v1);
    lru_node_t *n2 = lru_insert(&lru, &k2, sizeof k2, &v2);
    lru_node_t *n3 = lru_insert(&lru, &k3, sizeof k3, &v3);

    assert(n1 != NULL && n2 != NULL && n3 != NULL);
    assert(lru.size == 3);
    assert(n1->ptr == &v1);
    assert(n2->ptr == &v2);
    assert(n3->ptr == &v3);

    /* Newest entry is at the front, oldest at the tail. */
    assert(lru.head->nxt == n3->ref_list_nd);
    assert(lru.tail == n1->ref_list_nd);

    assert(lru_access(&lru, &k2, sizeof k2) == n2);
    assert(lru.size == 3);
    assert(lru.head->nxt == n2->ref_list_nd);
    assert(lru.tail == n1->ref_list_nd);

    int missing = 99;
    assert(lru_access(&lru, &missing, sizeof missing) == NULL);

    assert(lru_remove(&lru, n2) == &v2);
    assert(lru.size == 2);
    assert(lru_access(&lru, &k2, sizeof k2) == NULL);

    assert(lru_remove(&lru, n1) == &v1);
    assert(lru_remove(&lru, n3) == &v3);
    assert(lru.size == 0);
    assert(lru.head == lru.tail);

    lru_destroy(&lru);
}

static void test_insert_does_not_check_keys(void) {
    lru_t lru;
    int k1 = 1;
    int v1 = 10, v1_new = 11;

    lru_init(&lru, 4);
    lru_node_t *n1 = lru_insert(&lru, &k1, sizeof k1, &v1);
    lru_node_t *n2 = lru_insert(&lru, &k1, sizeof k1, &v1_new);

    assert(n2 != n1);
    assert(lru.size == 2);
    assert(n2->ptr == &v1_new);
    assert(lru.head->nxt == n2->ref_list_nd);

    assert(lru_remove(&lru, n2) == &v1_new);
    assert(lru_access(&lru, &k1, sizeof k1) == n1);

    lru_destroy(&lru);
}

static void test_insert_does_not_evict(void) {
    lru_t lru;
    int k1 = 1, k2 = 2, k3 = 3;
    int v1 = 10, v2 = 20, v3 = 30;

    lru_init(&lru, 2);
    assert(lru_insert(&lru, &k1, sizeof k1, &v1) != NULL);
    assert(lru_insert(&lru, &k2, sizeof k2, &v2) != NULL);
    assert(lru_insert(&lru, &k3, sizeof k3, &v3) != NULL);

    assert(lru.size == 3);
    assert(lru_access(&lru, &k1, sizeof k1) != NULL);
    assert(lru_access(&lru, &k2, sizeof k2) != NULL);
    assert(lru_access(&lru, &k3, sizeof k3) != NULL);

    lru_destroy(&lru);
}

static void test_replace_evicts_lru(void) {
    lru_t lru;
    int k1 = 1, k2 = 2, k3 = 3;
    int v1 = 10, v2 = 20, v3 = 30;

    lru_init(&lru, 2);
    lru_node_t *n1 = lru_insert(&lru, &k1, sizeof k1, &v1);
    lru_insert(&lru, &k2, sizeof k2, &v2);
    assert(lru_access(&lru, &k1, sizeof k1) == n1);

    /* k2 is now the least recently used entry. */
    assert(lru_replace(&lru, &k3, sizeof k3, &v3) == &v2);
    assert(lru.size == 2);
    assert(lru_access(&lru, &k2, sizeof k2) == NULL);

    lru_node_t *n3 = lru_access(&lru, &k3, sizeof k3);
    assert(n3 != NULL);
    assert(n3->ptr == &v3);
    assert(lru.head->nxt == n3->ref_list_nd);
    assert(lru.tail == n1->ref_list_nd);

    lru_destroy(&lru);
}

static void test_replace_inserts_when_not_full(void) {
    lru_t lru;
    int k1 = 1;
    int v1 = 10;

    lru_init(&lru, 2);
    assert(lru_replace(&lru, &k1, sizeof k1, &v1) == NULL);
    assert(lru.size == 1);
    assert(lru_access(&lru, &k1, sizeof k1) != NULL);

    lru_destroy(&lru);
}

static void test_destroy_non_empty(void) {
    lru_t lru;
    int k1 = 1;
    int v1 = 10;

    lru_init(&lru, 2);
    assert(lru_insert(&lru, &k1, sizeof k1, &v1) != NULL);

    /* Destroying a non-empty LRU frees all internal nodes. */
    lru_destroy(&lru);
}

int main(void) {
    test_init_empty();
    test_insert_access_remove();
    test_insert_does_not_check_keys();
    test_insert_does_not_evict();
    test_replace_evicts_lru();
    test_replace_inserts_when_not_full();
    test_destroy_non_empty();
    return 0;
}
