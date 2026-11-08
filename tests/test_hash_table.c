/* test_hash_table.c
 *      isolated tests for utils/hash_table
 */

#include <assert.h>
#include <string.h>

#include "hash_table/hash_table.h"

static void test_empty(void) {
    hash_table_t ht;
    int key = 1;

    hash_table_init(&ht);
    assert(ht != NULL);
    assert(hash_table_query(&ht, &key, sizeof key) == NULL);
    hash_table_destroy(&ht);
    assert(ht == NULL);
}

static void test_integer_keys(void) {
    hash_table_t ht;
    int k1 = 42, k2 = 7, k3 = -19;
    int v1 = 100, v2 = 200, v3 = 300;

    hash_table_init(&ht);
    hash_table_elem_t *e1 = hash_table_insert(&ht, &k1, sizeof k1, &v1);
    hash_table_elem_t *e2 = hash_table_insert(&ht, &k2, sizeof k2, &v2);
    hash_table_elem_t *e3 = hash_table_insert(&ht, &k3, sizeof k3, &v3);

    assert(e1 != NULL && e2 != NULL && e3 != NULL);
    assert(e1->ptr == &v1);
    assert(e2->ptr == &v2);
    assert(e3->ptr == &v3);

    assert(hash_table_query(&ht, &k1, sizeof k1) == e1);
    assert(hash_table_query(&ht, &k2, sizeof k2) == e2);
    assert(hash_table_query(&ht, &k3, sizeof k3) == e3);

    int missing = 12345;
    assert(hash_table_query(&ht, &missing, sizeof missing) == NULL);

    hash_table_destroy(&ht);
}

static void test_key_is_copied(void) {
    hash_table_t ht;
    int key = 5;
    int value = 50;

    hash_table_init(&ht);
    hash_table_elem_t *elem = hash_table_insert(&ht, &key, sizeof key, &value);
    key = 6;

    int old_key = 5;
    assert(hash_table_query(&ht, &old_key, sizeof old_key) == elem);
    assert(hash_table_query(&ht, &key, sizeof key) == NULL);

    hash_table_destroy(&ht);
}

static void test_string_keys(void) {
    hash_table_t ht;
    const char *k1 = "alpha";
    const char *k2 = "beta";
    const char *k3 = "alpha-beta";
    int v1 = 1, v2 = 2, v3 = 3;

    hash_table_init(&ht);
    hash_table_elem_t *e1 = hash_table_insert(&ht, k1, strlen(k1) + 1, &v1);
    hash_table_elem_t *e2 = hash_table_insert(&ht, k2, strlen(k2) + 1, &v2);
    hash_table_elem_t *e3 = hash_table_insert(&ht, k3, strlen(k3) + 1, &v3);

    assert(hash_table_query(&ht, "alpha", 6) == e1);
    assert(hash_table_query(&ht, "beta", 5) == e2);
    assert(hash_table_query(&ht, "alpha-beta", 11) == e3);
    assert(hash_table_query(&ht, "gamma", 6) == NULL);

    hash_table_destroy(&ht);
}

static void test_remove(void) {
    hash_table_t ht;
    int k1 = 11, k2 = 22;
    int v1 = 1, v2 = 2;

    hash_table_init(&ht);
    hash_table_elem_t *e1 = hash_table_insert(&ht, &k1, sizeof k1, &v1);
    hash_table_elem_t *e2 = hash_table_insert(&ht, &k2, sizeof k2, &v2);

    hash_table_remove(&ht, e1);
    assert(hash_table_query(&ht, &k1, sizeof k1) == NULL);
    assert(hash_table_query(&ht, &k2, sizeof k2) == e2);

    hash_table_remove(&ht, e2);
    assert(hash_table_query(&ht, &k2, sizeof k2) == NULL);

    /* Re-insert after removal to make sure the bucket is still usable. */
    hash_table_elem_t *e3 = hash_table_insert(&ht, &k1, sizeof k1, &v2);
    assert(hash_table_query(&ht, &k1, sizeof k1) == e3);

    hash_table_destroy(&ht);
}

static void test_many_entries(void) {
    hash_table_t ht;
    int keys[500], values[500];

    hash_table_init(&ht);
    for(int i = 0; i < 500; ++i) {
        keys[i] = i;
        values[i] = i * 2;
        assert(hash_table_insert(&ht, &keys[i], sizeof keys[i], &values[i]) != NULL);
    }
    for(int i = 0; i < 500; ++i) {
        hash_table_elem_t *elem = hash_table_query(&ht, &keys[i], sizeof keys[i]);
        assert(elem != NULL);
        assert(elem->ptr == &values[i]);
    }
    hash_table_destroy(&ht);
}

int main(void) {
    test_empty();
    test_integer_keys();
    test_key_is_copied();
    test_string_keys();
    test_remove();
    test_many_entries();
    return 0;
}
