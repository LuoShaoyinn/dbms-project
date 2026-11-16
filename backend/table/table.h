/* table.h
 *      Simple table
 *
 * Record layout:
 *      [NULL bitmap: ceil(m / 8) bytes][fixed-width column payloads]
 *      Bit c of the NULL bitmap is 1 when column c is NULL.
 *
 * Page 0 layout:
 *      table_header_t header
 *      char defaults[]         packed default values
 *
 * Data page layout:
 *      uint64_t slot_bitmap    bit s is 1 when slot s holds a valid record
 *      record slots            each header->record_size bytes
 *
 * RID encoding:
 *      slot    = rid % slots_per_page
 *      page_id = rid / slots_per_page + 1
 *      rid     = (page_id - 1) * slots_per_page + slot
 *      Page 0 holds the table header; data pages start at 1.
 *
 * The persisted header contains no pointers. While a table is open, page 0
 * stays borrowed and the handle reads the header directly from the mapped
 * file. Close every handle before fs_destroy() or before dropping its table.
 */

#ifndef _H_TABLE
#define _H_TABLE

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "column/column.h"
#include "constant.h"
#include "fs/fs.h"

typedef struct _s_table_header {
    uint32_t m;                 /* column count */
    uint32_t page_count;        /* number of allocated data pages */
    uint32_t free_page_hint;    /* first data page to inspect for a free slot */
    uint32_t record_size;       /* bytes per record */

    column_t columns[MAX_COLUMN];
    char defaults[];            /* NUL-terminated default values */
} table_header_t;

typedef struct _s_table_page {
    uint64_t slot_bitmap;               /* 1: valid record */
    char data[];
} table_page_t;

/* Status-returning operations use 0 for success and -1 for failure.
 * Definitions, record buffers, and persisted data are trusted. */

/* A table handle keeps page 0 borrowed, so header changes are written
 * straight into the mapped file and are visible to every handle. */
typedef struct _s_table {
    fs_datablock_t *header_page; /* retained until table_close() */
    char path[MAX_FILENAME];
} table_t;

/* Access the mapped header. */
static inline table_header_t* table_get_header(const table_t *this) {
    return (table_header_t*)this->header_page->ptr;
}

/* Borrow one whole page; the caller returns the original token with fs_return. */
fs_datablock_t* table_borrow_page(const table_t *this, uint32_t page_id);

/* table_header_build:
 *      fill a caller-provided PAGE_SIZE buffer with a header definition.
 *      Copies m columns, computes record offsets and record_size, and packs
 *      defaults. defaults[c] is NULL for no default, or a NUL-terminated
 *      value (including "" for an empty default).
 *      Inputs are verified by the caller and must fit the header/data pages;
 *      source columns/defaults must not alias the destination page.
 *      Returns 0. The buffer may be a freshly borrowed page 0. */
int table_header_build(table_header_t *this, uint32_t m,
        const column_t *columns, const char *const *defaults);

uint32_t table_header_search_key_name(const table_header_t *this,
        const char *keyname);

/* Geometry helpers. */
size_t table_get_null_bitmap_size(const table_header_t *p_head);
size_t table_get_record_size(const table_header_t *p_head);
uint32_t table_get_slots_per_page(const table_header_t *p_head);
size_t table_get_cell_offset(const table_header_t *p_head, uint32_t c);
uint32_t table_get_pageid(const table_header_t *p_head, uint32_t rid);
size_t table_get_record_offset_in_page(const table_header_t *p_head,
        uint32_t rid);
size_t table_get_page_offset_in_file(const table_header_t *p_head,
        uint32_t rid);

/* Slot bitmap helpers:
 *      a page is full when table_page_first_free_slot() returns
 *      table_get_slots_per_page(). */
uint32_t table_page_first_free_slot(const table_header_t *p_head,
        uint64_t slot_bitmap);
bool table_page_is_full(const table_header_t *p_head, uint64_t slot_bitmap);

/* Table lifecycle. Record access is provided by record/record.h.
 *
 * table_create:
 *      create a brand-new file and build its header directly in mapped page 0.
 *      Existing files are left untouched; return -1 if creation fails.
 * table_open:
 *      open an existing table and keep page 0 borrowed. Fails with
 *      -1 when the path is absent; it never creates the
 *      file.
 * table_close:
 *      release page 0 and destroy the handle. Return all records first. */
int table_create(table_t **this, const char *db_name,
        const char *table_name, uint32_t m, const column_t *columns,
        const char *const *defaults);
int table_open(table_t **this, const char *db_name,
        const char *table_name);
void table_close(table_t *this);

#endif // _H_TABLE
