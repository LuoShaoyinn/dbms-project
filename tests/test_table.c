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
        const char **key_name, const char **fmt,
        const uint32_t page_count, const uint32_t free_page_hint) {
    this->m = m;
    this->page_count = page_count;
    this->free_page_hint = free_page_hint;
    this->key_name = (char**)malloc(sizeof(char*) * m);
    this->fmt = (elem_type_t*)malloc(sizeof(elem_type_t) * m);
    this->offset_presum = (size_t*)malloc(sizeof(size_t) * (m + 1));
    this->offset_presum[0] = 0;

    for(uint32_t c = 0; c < m; ++c) {
        this->key_name[c] = (char*)malloc(sizeof(char) \
                * (strlen(key_name[c]) + 1));
        strcpy(this->key_name[c], key_name[c]);
        elem_type_init(&this->fmt[c], fmt[c]);
        this->offset_presum[c + 1] = this->offset_presum[c]
                + elem_type_sizeof(&this->fmt[c]);
    }
}

static uint64_t slot_mask(const uint32_t slots) {
    if(slots >= TABLE_SLOT_MAX) return ~(uint64_t)0;
    return ((uint64_t)1 << slots) - 1;
}

static void test_null_bitmap_size(void) {
    table_header_t head;
    memset(&head, 0, sizeof(head));

    head.m = 1;  assert(table_get_null_bitmap_size(&head) == 1);
    head.m = 8;  assert(table_get_null_bitmap_size(&head) == 1);
    head.m = 9;  assert(table_get_null_bitmap_size(&head) == 2);
    head.m = 16; assert(table_get_null_bitmap_size(&head) == 2);
    head.m = 17; assert(table_get_null_bitmap_size(&head) == 3);
}

static void test_header_roundtrip(void) {
    table_header_t src;
    table_header_t dst;
    const char *key_name[] = {"id", "name", "score"};
    const char *fmt[] = {"i", "s32", "d"};
    void *buffer = malloc(PAGE_SIZE);
    assert(buffer != NULL);

    header_build(&src, 3, key_name, fmt, 7, 3);
    assert(table_header_validate(&src) == 0);
    assert(table_header_save(&src, buffer) == 0);
    table_header_destory(&src);

    table_header_init(&dst, buffer);
    assert(dst.m == 3);
    assert(dst.page_count == 7);
    assert(dst.free_page_hint == 3);

    for(uint32_t c = 0; c < 3; ++c) {
        assert(strcmp(dst.key_name[c], key_name[c]) == 0);
        assert(strcmp(dst.fmt[c].fmt, fmt[c]) == 0);
        assert(elem_type_total_n(&dst.fmt[c]) == 1);
    }

    assert(elem_type_sizeof(&dst.fmt[0]) == sizeof(int32_t));
    assert(elem_type_sizeof(&dst.fmt[1]) == 32);
    assert(elem_type_sizeof(&dst.fmt[2]) == sizeof(double));

    /* Payloads start at 0, 4 and 36; the NULL bitmap shifts them by 1. */
    assert(dst.offset_presum[0] == 0);
    assert(dst.offset_presum[1] == 4);
    assert(dst.offset_presum[2] == 36);
    assert(dst.offset_presum[3] == 44);

    assert(table_get_null_bitmap_size(&dst) == 1);
    assert(table_get_cell_offset(&dst, 0) == 1);
    assert(table_get_cell_offset(&dst, 1) == 5);
    assert(table_get_cell_offset(&dst, 2) == 37);
    assert(table_get_record_size(&dst) == 45);

    assert(table_header_search_key_name(&dst, "id") == 0);
    assert(table_header_search_key_name(&dst, "name") == 1);
    assert(table_header_search_key_name(&dst, "score") == 2);
    assert(table_header_search_key_name(&dst, "missing") == (uint32_t)-1);

    table_header_destory(&dst);
    free(buffer);
}

static void test_page_count_above_64(void) {
    table_header_t src;
    table_header_t dst;
    const char *key_name[] = {"id"};
    const char *fmt[] = {"i"};
    void *buffer = malloc(PAGE_SIZE);
    assert(buffer != NULL);

    header_build(&src, 1, key_name, fmt, 1000, 700);
    assert(table_header_validate(&src) == 0);
    assert(table_header_save(&src, buffer) == 0);
    table_header_destory(&src);

    table_header_init(&dst, buffer);
    assert(dst.page_count == 1000);
    assert(dst.free_page_hint == 700);

    /* RID encoding keeps reaching pages past the old 64-page limit. */
    uint32_t slots = table_get_slots_per_page(&dst);
    assert(slots == TABLE_SLOT_MAX);
    assert(table_get_pageid(&dst, slots * 65) == 66);
    assert(table_get_pageid(&dst, slots * 999) == 1000);
    assert(table_get_pageid(&dst, slots * 1000) == 1001);
    assert(table_get_page_offset_in_file(&dst, slots * 999)
            == (size_t)1000 * PAGE_SIZE + sizeof(uint64_t));

    table_header_destory(&dst);
    free(buffer);
}

static void test_rid_page_boundaries(void) {
    table_header_t head;
    const char *key_name[] = {"payload"};
    const char *fmt[] = {"s1000"};
    const size_t record_size = 1001; /* 1 byte NULL bitmap + 1000 */
    const uint32_t slots = (PAGE_SIZE - sizeof(uint64_t)) / record_size;

    header_build(&head, 1, key_name, fmt, 0, 0);
    assert(table_get_record_size(&head) == record_size);
    assert(slots == 8);
    assert(table_get_slots_per_page(&head) == slots);

    assert(table_get_pageid(&head, 0) == 1);
    assert(table_get_pageid(&head, slots - 1) == 1);
    assert(table_get_pageid(&head, slots) == 2);
    assert(table_get_pageid(&head, 2 * slots) == 3);

    assert(table_get_record_offset_in_page(&head, 0)
            == sizeof(uint64_t));
    assert(table_get_record_offset_in_page(&head, slots - 1)
            == sizeof(uint64_t) + (slots - 1) * record_size);
    assert(table_get_record_offset_in_page(&head, slots)
            == sizeof(uint64_t));

    assert(table_get_page_offset_in_file(&head, 0)
            == PAGE_SIZE + sizeof(uint64_t));
    assert(table_get_page_offset_in_file(&head, slots)
            == 2 * PAGE_SIZE + sizeof(uint64_t));

    table_header_destory(&head);
}

static void test_slot_bitmap_helpers(void) {
    table_header_t head;
    const char *key_name[] = {"payload"};
    const char *fmt[] = {"s1000"};
    const uint32_t slots = 8;
    const uint64_t full = slot_mask(slots);

    header_build(&head, 1, key_name, fmt, 0, 0);
    assert(table_get_slots_per_page(&head) == slots);

    assert(table_page_first_free_slot(&head, 0) == 0);
    assert(table_page_first_free_slot(&head, 0x3) == 2);
    assert(table_page_first_free_slot(&head, full) == slots);
    assert(table_page_is_full(&head, full));
    assert(!table_page_is_full(&head, full ^ ((uint64_t)1 << (slots - 1))));
    assert(table_page_first_free_slot(&head,
            full ^ ((uint64_t)1 << 3)) == 3);

    table_header_destory(&head);
}

static void test_maximum_fitting_record(void) {
    table_header_t head;
    const char *key_name[] = {"payload"};
    const char *fmt[] = {"s8183"}; /* 1 + 8183 = 8184 bytes */
    const size_t max_record = PAGE_SIZE - sizeof(uint64_t);
    void *buffer = malloc(PAGE_SIZE);
    assert(buffer != NULL);

    header_build(&head, 1, key_name, fmt, 0, 0);
    assert(table_get_null_bitmap_size(&head) == 1);
    assert(table_get_record_size(&head) == max_record);
    assert(table_get_slots_per_page(&head) == 1);
    assert(table_header_validate(&head) == 0);
    assert(table_header_save(&head, buffer) == 0);

    assert(table_get_pageid(&head, 0) == 1);
    assert(table_get_pageid(&head, 1) == 2);
    assert(table_get_record_offset_in_page(&head, 0)
            == sizeof(uint64_t));
    assert(table_get_page_offset_in_file(&head, 1)
            == 2 * PAGE_SIZE + sizeof(uint64_t));

    table_header_destory(&head);
    free(buffer);
}

static void test_oversized_schema_rejection(void) {
    table_header_t head;
    const char *key_name[] = {"payload"};
    const char *fmt[] = {"s8184"}; /* 1 + 8184 = 8185: no slot fits */
    unsigned char *buffer = (unsigned char*)malloc(PAGE_SIZE);
    char *long_name = (char*)malloc(PAGE_SIZE + 1);
    assert(buffer != NULL);
    assert(long_name != NULL);

    header_build(&head, 1, key_name, fmt, 0, 0);
    assert(table_get_record_size(&head) == PAGE_SIZE - sizeof(uint64_t) + 1);
    assert(table_get_slots_per_page(&head) == 0);
    assert(table_header_validate(&head) == -1);

    memset(buffer, 0xa5, PAGE_SIZE);
    assert(table_header_save(&head, buffer) == -1);
    for(size_t i = 0; i < PAGE_SIZE; ++i) assert(buffer[i] == 0xa5);

    table_header_destory(&head);

    /* A header that cannot fit page 0 is rejected before writing too. */
    memset(long_name, 'k', PAGE_SIZE);
    long_name[PAGE_SIZE] = '\0';
    const char *big_key[] = {long_name};
    const char *big_fmt[] = {"i"};
    header_build(&head, 1, big_key, big_fmt, 0, 0);
    assert(table_header_validate(&head) == -1);
    assert(table_header_save(&head, buffer) == -1);

    for(size_t i = 0; i < PAGE_SIZE; ++i) assert(buffer[i] == 0xa5);

    table_header_destory(&head);
    free(long_name);
    free(buffer);
}

int main(void) {
    test_null_bitmap_size();
    test_header_roundtrip();
    test_page_count_above_64();
    test_rid_page_boundaries();
    test_slot_bitmap_helpers();
    test_maximum_fitting_record();
    test_oversized_schema_rejection();
    return 0;
}