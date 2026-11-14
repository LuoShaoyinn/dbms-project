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

/* Recoverable table statuses. TABLE_SCAN_END is a normal end of scan; every
 * negative value is an error. */
typedef enum _s_table_status {
    TABLE_OK = 0,
    TABLE_SCAN_END = 1,
    TABLE_ERR = -1,
    TABLE_ERR_EXISTS = -2,
    TABLE_ERR_NOT_FOUND = -3,
    TABLE_ERR_INVALID = -4,
    TABLE_ERR_RID = -5,
    TABLE_ERR_IO = -6,
    TABLE_ERR_FULL = -7
} table_status_t;

/* A table handle owns its decoded header and its file path.
 * The header mirrors page 0; page_count and free_page_hint are persisted on
 * every allocation change. */
typedef struct _s_table {
    table_header_t head;
    char *path;                 /* owned copy of the table file path */
} table_t;

/* Scans are opaque. A scan must be closed before its table is closed.
 * Mutating a table while a scan is active is unsupported: the scan reads
 * the table's live header but keeps a borrowed page between calls. */
typedef struct _s_table_scan table_scan_t;

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

/* Record CRUD.
 *
 * table_create:
 *      create a brand-new table file. Fails with TABLE_ERR_EXISTS when the
 *      path already exists and leaves that file untouched.
 * table_open:
 *      open an existing table. Fails with TABLE_ERR_NOT_FOUND when the path
 *      is absent; it never creates the file.
 * table_close:
 *      destroy the handle. Any scan on this table must already be closed.
 *
 * Records are already encoded byte buffers whose length must equal
 * table_get_record_size(). Mutation takes effect immediately in the mapped
 * file and allocation metadata is saved before returning.
 *
 * table_insert:
 *      store one record and write its RID to *rid.
 * table_get:
 *      copy one occupied record into a caller-owned malloc() buffer written
 *      to *record; the caller must free() it. *size receives its length.
 *      TABLE_ERR_RID means the RID is out of range or the slot is free.
 * table_update:
 *      overwrite one occupied slot without changing its RID.
 * table_delete:
 *      clear one occupied slot. Other records are never moved.
 *
 * Scans visit occupied slots in page/slot order.
 * table_scan_next:
 *      return TABLE_OK with a caller-owned record buffer and its RID, or
 *      TABLE_SCAN_END when no record remains. */
table_status_t table_create(table_t **this, const char *db_name,
        const char *table_name, const uint32_t m,
        const char *const *key_name, const char *const *fmt);
table_status_t table_open(table_t **this, const char *db_name,
        const char *table_name);
void table_close(table_t *this);

table_status_t table_insert(table_t *this, const void *record,
        const size_t size, uint32_t *rid);
table_status_t table_get(table_t *this, const uint32_t rid,
        void **record, size_t *size);
table_status_t table_update(table_t *this, const uint32_t rid,
        const void *record, const size_t size);
table_status_t table_delete(table_t *this, const uint32_t rid);

table_status_t table_scan_open(table_t *this, table_scan_t **scan);
table_status_t table_scan_next(table_scan_t *scan, uint32_t *rid,
        void **record, size_t *size);
void table_scan_close(table_scan_t *scan);

#endif // _H_TABLE
