/* test_table.c
 *      isolated tests for backend/table
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "column/column.h"
#include "constant.h"
#include "table/table.h"

static table_header_t* header_build(char *buffer, const uint32_t m,
        const column_t *columns, const char *const *defaults,
        const uint32_t page_count, const uint32_t free_page_hint) {
    table_header_t *head = (table_header_t*)buffer;
    assert(table_header_build(head, m, columns, defaults) == 0);
    head->page_count = page_count;
    head->free_page_hint = free_page_hint;
    return head;
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

static void test_header_build(void) {
    const column_t columns[] = {
        {.name = "id", .type = {ELEM_TYPE_I, 4}},
        {.name = "name", .type = {ELEM_TYPE_S, 32}, .not_null = true},
        {.name = "score", .type = {ELEM_TYPE_D, 8}},
    };
    const char *defaults[] = {NULL, "guest", "1.5"};
    _Alignas(table_header_t) char buffer[PAGE_SIZE];
    table_header_t *head = header_build(buffer, 3, columns, defaults, 7, 3);

    assert(head->m == 3);
    assert(head->page_count == 7);
    assert(head->free_page_hint == 3);
    assert(head->record_size == 45);

    assert(strcmp(head->columns[0].name, "id") == 0);
    assert(head->columns[0].type.kind == ELEM_TYPE_I);
    assert(head->columns[0].not_null == 0);
    assert(head->columns[1].not_null == 1);

    assert(head->columns[0].offset == 1);
    assert(head->columns[1].offset == 5);
    assert(head->columns[2].offset == 37);

    assert(column_size(&head->columns[0]) == sizeof(int32_t));
    assert(column_size(&head->columns[1]) == 32);
    assert(column_size(&head->columns[2]) == sizeof(double));

    assert(column_default(head, &head->columns[0]) == NULL);
    assert(strcmp(column_default(head, &head->columns[1]), "guest") == 0);
    assert(strcmp(column_default(head, &head->columns[2]), "1.5") == 0);

    assert(strcmp(column_type_name(&head->columns[0]), "INT") == 0);
    assert(strcmp(column_type_name(&head->columns[1]), "VARCHAR") == 0);
    assert(strcmp(column_type_name(&head->columns[2]), "FLOAT") == 0);

    assert(table_get_null_bitmap_size(head) == 1);
    assert(table_get_cell_offset(head, 0) == 1);
    assert(table_get_cell_offset(head, 1) == 5);
    assert(table_get_cell_offset(head, 2) == 37);
    assert(table_get_record_size(head) == 45);

    assert(table_header_search_key_name(head, "id") == 0);
    assert(table_header_search_key_name(head, "name") == 1);
    assert(table_header_search_key_name(head, "score") == 2);
    assert(table_header_search_key_name(head, "missing") == (uint32_t)-1);
}

static void test_empty_default(void) {
    const column_t columns[] = {
        {.name = "tag", .type = {ELEM_TYPE_S, 8}},
    };
    const char *defaults[] = {""};
    _Alignas(table_header_t) char buffer[PAGE_SIZE];
    table_header_t *head = header_build(buffer, 1, columns, defaults, 0, 1);

    const char *value = column_default(head, &head->columns[0]);
    assert(value != NULL);
    assert(value[0] == '\0');
    assert(head->columns[0].default_offset != 0);
}

static void test_page_count_above_64(void) {
    const column_t columns[] = {{.name = "id", .type = {ELEM_TYPE_I, 4}}};
    _Alignas(table_header_t) char buffer[PAGE_SIZE];
    table_header_t *head = header_build(buffer, 1, columns, NULL, 1000, 700);

    assert(head->page_count == 1000);
    assert(head->free_page_hint == 700);

    /* RID encoding keeps reaching pages past the old 64-page limit. */
    uint32_t slots = table_get_slots_per_page(head);
    assert(slots == TABLE_SLOT_MAX);
    assert(table_get_pageid(head, slots * 65) == 66);
    assert(table_get_pageid(head, slots * 999) == 1000);
    assert(table_get_pageid(head, slots * 1000) == 1001);
    assert(table_get_page_offset_in_file(head, slots * 999)
            == (size_t)1000 * PAGE_SIZE + sizeof(uint64_t));
}

static void test_rid_page_boundaries(void) {
    const column_t columns[] = {{.name = "payload", .type = {ELEM_TYPE_S, 1000}}};
    const size_t record_size = 1001; /* 1 byte NULL bitmap + 1000 */
    const uint32_t slots = (PAGE_SIZE - sizeof(uint64_t)) / record_size;
    _Alignas(table_header_t) char buffer[PAGE_SIZE];
    table_header_t *head = header_build(buffer, 1, columns, NULL, 0, 0);

    assert(table_get_record_size(head) == record_size);
    assert(slots == 8);
    assert(table_get_slots_per_page(head) == slots);

    assert(table_get_pageid(head, 0) == 1);
    assert(table_get_pageid(head, slots - 1) == 1);
    assert(table_get_pageid(head, slots) == 2);
    assert(table_get_pageid(head, 2 * slots) == 3);

    assert(table_get_record_offset_in_page(head, 0)
            == sizeof(uint64_t));
    assert(table_get_record_offset_in_page(head, slots - 1)
            == sizeof(uint64_t) + (slots - 1) * record_size);
    assert(table_get_record_offset_in_page(head, slots)
            == sizeof(uint64_t));

    assert(table_get_page_offset_in_file(head, 0)
            == PAGE_SIZE + sizeof(uint64_t));
    assert(table_get_page_offset_in_file(head, slots)
            == 2 * PAGE_SIZE + sizeof(uint64_t));
}

static void test_slot_bitmap_helpers(void) {
    const column_t columns[] = {{.name = "payload", .type = {ELEM_TYPE_S, 1000}}};
    const uint32_t slots = 8;
    const uint64_t full = slot_mask(slots);
    _Alignas(table_header_t) char buffer[PAGE_SIZE];
    table_header_t *head = header_build(buffer, 1, columns, NULL, 0, 0);

    assert(table_get_slots_per_page(head) == slots);

    assert(table_page_first_free_slot(head, 0) == 0);
    assert(table_page_first_free_slot(head, 0x3) == 2);
    assert(table_page_first_free_slot(head, full) == slots);
    assert(table_page_is_full(head, full));
    assert(!table_page_is_full(head, full ^ ((uint64_t)1 << (slots - 1))));
    assert(table_page_first_free_slot(head,
            full ^ ((uint64_t)1 << 3)) == 3);
}

static void test_maximum_fitting_record(void) {
    const column_t columns[] = {
        {.name = "payload", .type = {ELEM_TYPE_S, 8183}}, /* 1 + 8183 = 8184 bytes */
    };
    const size_t max_record = PAGE_SIZE - sizeof(uint64_t);
    _Alignas(table_header_t) char buffer[PAGE_SIZE];
    table_header_t *head = header_build(buffer, 1, columns, NULL, 0, 0);

    assert(table_get_null_bitmap_size(head) == 1);
    assert(table_get_record_size(head) == max_record);
    assert(table_get_slots_per_page(head) == 1);

    assert(table_get_pageid(head, 0) == 1);
    assert(table_get_pageid(head, 1) == 2);
    assert(table_get_record_offset_in_page(head, 0)
            == sizeof(uint64_t));
    assert(table_get_page_offset_in_file(head, 1)
            == 2 * PAGE_SIZE + sizeof(uint64_t));
}

int main(void) {
    test_null_bitmap_size();
    test_header_build();
    test_empty_default();
    test_page_count_above_64();
    test_rid_page_boundaries();
    test_slot_bitmap_helpers();
    test_maximum_fitting_record();
    return 0;
}
