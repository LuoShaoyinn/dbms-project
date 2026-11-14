/* table.h
 *      Simple table
 *
 * Record layout:
 *      [NULL bitmap: ceil(m / 8) bytes][fixed-width column payloads]
 *      Bit c of the NULL bitmap is 1 when column c is NULL.
 *
 * Data page layout:
 *      uint64_t slot_bitmap    bit s is 1 when slot s holds a valid record
 *      record slots            each table_get_record_size() bytes
 *
 * RID encoding:
 *      slot    = rid % slots_per_page
 *      page_id = rid / slots_per_page + 1
 *      rid     = (page_id - 1) * slots_per_page + slot
 *      Page 0 holds the table header; data pages start at 1.
 */

#ifndef _H_TABLE
#define _H_TABLE

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "constant.h"
#include "elem_type/elem_type.h"

typedef struct _s_table_header {
    uint32_t m;                 /* column count */
    uint32_t page_count;        /* number of allocated data pages */
    uint32_t free_page_hint;    /* first data page to inspect for a free slot */
    char** key_name;
    elem_type_t* fmt;           /* format of each column */
    size_t *offset_presum;      /* payload offset, [m] is payload size */
} table_header_t;

typedef struct _s_table_page {
    uint64_t slot_bitmap;               /* 1: valid record */
    char data[];
} table_page_t;

/* The header is kept in page 0 of a table file. Data pages start from 1. */
void table_header_init(table_header_t *this, void *p_buffer);

/* table_header_validate:
 *      return 0 when the schema fits its pages, -1 otherwise:
 *      - m is in [1, MAX_COLUMN]
 *      - the serialized header fits in PAGE_SIZE
 *      - at least one record fits in a data page. */
int table_header_validate(const table_header_t *this);

/* table_header_save:
 *      serialize the header into p_buffer.
 *      Returns 0 on success and -1 when table_header_validate fails; the
 *      buffer is not touched on failure. */
int table_header_save(table_header_t *this, void *p_buffer);
void table_header_destory(table_header_t *this);
uint32_t table_header_search_key_name(table_header_t *this, \
        const char *keyname);

/* Geometry helpers. */
size_t table_get_null_bitmap_size(const table_header_t *p_head);
size_t table_get_record_size(const table_header_t *p_head);
uint32_t table_get_slots_per_page(const table_header_t *p_head);
size_t table_get_cell_offset(const table_header_t *p_head, const uint32_t c);
uint32_t table_get_pageid(const table_header_t *p_head, const uint32_t rid);
size_t table_get_record_offset_in_page(const table_header_t *p_head,
        const uint32_t rid);
size_t table_get_page_offset_in_file(const table_header_t *p_head,
        const uint32_t rid);

/* Slot bitmap helpers:
 *      a page is full when table_page_first_free_slot() returns
 *      table_get_slots_per_page(). */
uint32_t table_page_first_free_slot(const table_header_t *p_head,
        const uint64_t slot_bitmap);
bool table_page_is_full(const table_header_t *p_head,
        const uint64_t slot_bitmap);

#endif // _H_TABLE
