/* test_table.c
 *      isolated tests for backend/table
 */

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "constant.h"
#include "table/table.h"

static void header_build(table_header_t *this, const uint32_t m,
        const char **key_name, const char **fmt, const uint64_t page_bitmap) {
    this->m = m;
    this->page_bitmap = page_bitmap;
    this->key_name = (char**)malloc(sizeof(char*) * m);
    this->fmt = (elem_type_t*)malloc(sizeof(elem_type_t) * m);
    this->offset_presum = (size_t*)malloc(sizeof(size_t) * (m + 1));
    this->offset_presum[0] = 0;

    for(uint32_t c = 0; c < m; ++c) {
        this->key_name[c] = (char*)malloc(sizeof(char) * (strlen(key_name[c]) + 1));
        strcpy(this->key_name[c], key_name[c]);
        elem_type_init(&this->fmt[c], fmt[c]);
        this->offset_presum[c + 1] = this->offset_presum[c] + elem_type_sizeof(&this->fmt[c]);
    }
}

static void test_header_roundtrip(void) {
    table_header_t src;
    table_header_t dst;
    const char *key_name[] = {"id", "name", "score"};
    const char *fmt[] = {"i", "s32", "d"};
    void *buffer = malloc(PAGE_SIZE);

    header_build(&src, 3, key_name, fmt, 0x5);
    table_header_save(&src, buffer);
    table_header_destory(&src);

    table_header_init(&dst, buffer);
    assert(dst.m == 3);
    assert(dst.page_bitmap == 0x5);

    for(uint32_t c = 0; c < 3; ++c) {
        assert(strcmp(dst.key_name[c], key_name[c]) == 0);
        assert(strcmp(dst.fmt[c].fmt, fmt[c]) == 0);
        assert(elem_type_total_n(&dst.fmt[c]) == 1);
    }

    assert(elem_type_sizeof(&dst.fmt[0]) == sizeof(int32_t));
    assert(elem_type_sizeof(&dst.fmt[1]) == 32);
    assert(elem_type_sizeof(&dst.fmt[2]) == sizeof(double));

    /* int32_t, char[32], double -> offsets 0, 4, 36; record size 44. */
    assert(table_get_cell_offset(&dst, 0) == 0);
    assert(table_get_cell_offset(&dst, 1) == 4);
    assert(table_get_cell_offset(&dst, 2) == 36);
    assert(dst.offset_presum[3] == 44);

    assert(table_header_search_key_name(&dst, "id") == 0);
    assert(table_header_search_key_name(&dst, "name") == 1);
    assert(table_header_search_key_name(&dst, "score") == 2);
    assert(table_header_search_key_name(&dst, "missing") == (uint32_t)-1);

    table_header_destory(&dst);
    free(buffer);
}

static void test_page_geometry(void) {
    table_header_t head;
    const char *key_name[] = {"id", "name", "score"};
    const char *fmt[] = {"i", "s32", "d"};
    const size_t record_size = 44;
    const size_t slots = 64; /* capped by the 64-bit slot bitmap */

    header_build(&head, 3, key_name, fmt, 0);

    assert(table_get_pageid(&head, 0) == 1);
    assert(table_get_pageid(&head, slots - 1) == 1);
    assert(table_get_pageid(&head, slots) == 2);
    assert(table_get_pageid(&head, 2 * slots) == 3);

    assert(table_get_record_offset_in_page(&head, 0) == sizeof(uint64_t));
    assert(table_get_record_offset_in_page(&head, slots - 1)
            == sizeof(uint64_t) + (slots - 1) * record_size);
    assert(table_get_record_offset_in_page(&head, slots) == sizeof(uint64_t));

    assert(table_get_page_offset_in_file(&head, 0)
            == PAGE_SIZE + sizeof(uint64_t));
    assert(table_get_page_offset_in_file(&head, slots)
            == 2 * PAGE_SIZE + sizeof(uint64_t));

    table_header_destory(&head);
}

static void test_page_geometry_small_page(void) {
    table_header_t head;
    const char *key_name[] = {"payload"};
    const char *fmt[] = {"s1000"};
    const size_t record_size = 1000;
    const size_t slots = (PAGE_SIZE - sizeof(uint64_t)) / record_size;

    header_build(&head, 1, key_name, fmt, 0);
    assert(slots == 4);

    assert(table_get_pageid(&head, 0) == 1);
    assert(table_get_pageid(&head, slots - 1) == 1);
    assert(table_get_pageid(&head, slots) == 2);
    assert(table_get_record_offset_in_page(&head, 0) == sizeof(uint64_t));
    assert(table_get_record_offset_in_page(&head, 1)
            == sizeof(uint64_t) + record_size);
    assert(table_get_record_offset_in_page(&head, slots) == sizeof(uint64_t));
    assert(table_get_page_offset_in_file(&head, slots)
            == 2 * PAGE_SIZE + sizeof(uint64_t));

    table_header_destory(&head);
}

static void test_record_too_large(void) {
    table_header_t head;
    const char *key_name[] = {"payload"};
    const char *fmt[] = {"s5000"};

    header_build(&head, 1, key_name, fmt, 0);

    /* No slot fits into a page. */
    assert(table_get_pageid(&head, 0) == 0);
    assert(table_get_record_offset_in_page(&head, 0) == 0);
    assert(table_get_page_offset_in_file(&head, 0) == 0);

    table_header_destory(&head);
}

static void test_first_available_page(void) {
    table_header_t head;
    const char *key_name[] = {"id"};
    const char *fmt[] = {"i"};

    header_build(&head, 1, key_name, fmt, 0);
    assert(table_get_first_available_page(&head) == 1);

    head.page_bitmap = 0x1; /* data page 1 is used */
    assert(table_get_first_available_page(&head) == 2);

    head.page_bitmap = 0x7; /* data pages 1, 2 and 3 are used */
    assert(table_get_first_available_page(&head) == 4);

    /* All pages except the last one are used. */
    head.page_bitmap = ~((uint64_t)1 << 63);
    assert(table_get_first_available_page(&head) == 64);

    head.page_bitmap = ~(uint64_t)0;
    assert(table_get_first_available_page(&head) == 0);

    table_header_destory(&head);
}

int main(void) {
    test_header_roundtrip();
    test_page_geometry();
    test_page_geometry_small_page();
    test_record_too_large();
    test_first_available_page();
    return 0;
}