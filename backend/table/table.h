/* table.h
 *      Simple table
 */

#ifndef _H_TABLE
#define _H_TABLE

#include <stddef.h>
#include <stdint.h>

#include "constant.h"
#include "elem_type/elem_type.h"

typedef struct _s_table_header {
    uint32_t m;                 /* column count */
    uint64_t page_bitmap;       /* bit i: data page i + 1 is allocated */
    char** key_name;
    elem_type_t* fmt;           /* format of each column */
    size_t *offset_presum;      /* offset in record, [m] is record size */
} table_header_t;

typedef struct _s_table_page {
    uint64_t slot_bitmap;               /* 1: valid record */
    char data[];
} table_page_t;

/* The header is kept in page 0 of a table file. Data pages start from 1. */
void table_header_init(table_header_t *this, void *p_buffer);
void table_header_save(table_header_t *this, void *p_buffer);
void table_header_destory(table_header_t *this);
uint32_t table_header_search_key_name(table_header_t *this, const char *keyname);

/* Return the first free data page, or 0 when the bitmap is full. */
uint32_t table_get_first_available_page(table_header_t *p_head);
size_t table_get_cell_offset(table_header_t *p_head, const uint32_t c);
uint32_t table_get_pageid(table_header_t *p_head, const uint32_t rid);
size_t table_get_record_offset_in_page(table_header_t *p_head, const uint32_t rid);
size_t table_get_page_offset_in_file(table_header_t *p_head, const uint32_t rid);


#endif // _H_TABLE
