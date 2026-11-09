/* test_lru_stress.c
 *      demanding lru test against a reference model
 */

#include <assert.h>
#include <stdint.h>

#include "lru/lru.h"

#define KEY_RANGE 200
#define MAX_SIZE  64
#define OPS       20000

static void test_stress(void) {
    lru_t lru;
    int values[KEY_RANGE];
    lru_node_t *nodes[KEY_RANGE] = {0};
    int order[MAX_SIZE];          /* most recently used first */
    int order_size = 0;
    uint32_t seed = 0x12345678u;

    for(int i = 0; i < KEY_RANGE; ++i)
        values[i] = i * 7 + 1;
    lru_init(&lru, MAX_SIZE);

    for(int op = 0; op < OPS; ++op) {
        seed = seed * 1103515245u + 12345u;
        int key = (int)((seed >> 16) % KEY_RANGE);

        int pos = -1;
        for(int i = 0; i < order_size; ++i)
            if(order[i] == key) { pos = i; break; }

        void *created = (void*)0x1;
        void *removed = (void*)0x1;
        lru_node_t *node = lru_access(&lru, &key, sizeof key, &created, &removed);
        assert(node != NULL);

        if(pos >= 0) { /* hit */
            assert(created == NULL);
            assert(removed == NULL);
            assert(node == nodes[key]);
            assert(node->ptr == &values[key]);

            for(int i = pos; i > 0; --i)
                order[i] = order[i-1];
            order[0] = key;
        }
        else { /* miss, create a node and maybe evict the LRU tail */
            assert(created == node);
            nodes[key] = node;
            node->ptr = &values[key];

            if(order_size == MAX_SIZE) {
                int evicted = order[order_size-1];
                assert(removed == &values[evicted]);
                --order_size;
            }
            else assert(removed == NULL);

            for(int i = order_size; i > 0; --i)
                order[i] = order[i-1];
            order[0] = key;
            ++order_size;
        }

        assert(lru.size == (size_t)order_size);
        assert(lru.head->nxt == nodes[order[0]]->ref_list_nd);
        assert(lru.tail == nodes[order[order_size-1]]->ref_list_nd);
    }

    lru_destroy(&lru);
}

int main(void) {
    test_stress();
    return 0;
}