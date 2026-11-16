/* table.c
 *      Table header, geometry, page access, and lifecycle
 *
 * Page 0 holds table_header_t followed by packed default strings. The header
 * is mapped directly and contains no pointers.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "column/column.h"
#include "constant.h"
#include "table.h"

int table_header_build(table_header_t *this, const uint32_t m,
        const column_t *columns, const char *const *defaults) {
    memset(this, 0, offsetof(table_header_t, defaults));
    this->m = m;
    this->page_count = 0;
    this->free_page_hint = 1;

    size_t offset = ((size_t)m + 7) / 8;
    size_t default_offset = offsetof(table_header_t, defaults);
    for(uint32_t c = 0; c < m; ++c) {
        this->columns[c] = columns[c];
        this->columns[c].offset = (uint32_t)offset;
        this->columns[c].default_offset = 0;
        offset += column_size(&this->columns[c]);

        if(defaults == NULL || defaults[c] == NULL) continue;

        size_t len = strlen(defaults[c]) + 1;
        this->columns[c].default_offset = (uint32_t)default_offset;
        memcpy((char*)this + default_offset, defaults[c], len);
        default_offset += len;
    }
    this->record_size = (uint32_t)offset;
    return 0;
}

size_t table_get_null_bitmap_size(const table_header_t *p_head) {
    return ((size_t)p_head->m + 7) / 8;
}

size_t table_get_record_size(const table_header_t *p_head) {
    return p_head->record_size;
}

uint32_t table_get_slots_per_page(const table_header_t *p_head) {
    size_t record_size = table_get_record_size(p_head);
    size_t slots = (PAGE_SIZE - sizeof(uint64_t)) / record_size;
    return slots > TABLE_SLOT_MAX ? TABLE_SLOT_MAX : (uint32_t)slots;
}

uint32_t table_header_search_key_name(const table_header_t *this,
        const char *keyname) {
    for(uint32_t c = 0; c < this->m; ++c)
        if(strcmp(this->columns[c].name, keyname) == 0) return c;
    return (uint32_t)-1;
}

uint32_t table_page_first_free_slot(const table_header_t *p_head,
        const uint64_t slot_bitmap) {
    uint32_t slots = table_get_slots_per_page(p_head);
    for(uint32_t slot = 0; slot < slots; ++slot)
        if((slot_bitmap & ((uint64_t)1 << slot)) == 0) return slot;
    return slots;
}

bool table_page_is_full(const table_header_t *p_head,
        const uint64_t slot_bitmap) {
    uint32_t slots = table_get_slots_per_page(p_head);
    return table_page_first_free_slot(p_head, slot_bitmap) == slots;
}

size_t table_get_cell_offset(const table_header_t *p_head,
        const uint32_t c) {
    return p_head->columns[c].offset;
}

uint32_t table_get_pageid(const table_header_t *p_head, const uint32_t rid) {
    uint32_t slots = table_get_slots_per_page(p_head);
    return rid / slots + 1;
}

size_t table_get_record_offset_in_page(const table_header_t *p_head,
        const uint32_t rid) {
    uint32_t slots = table_get_slots_per_page(p_head);
    return sizeof(uint64_t) + (rid % slots) * table_get_record_size(p_head);
}

size_t table_get_page_offset_in_file(const table_header_t *p_head,
        const uint32_t rid) {
    return (size_t)table_get_pageid(p_head, rid) * PAGE_SIZE
            + table_get_record_offset_in_page(p_head, rid);
}

/* fs_* consumes filenames; table handles keep their own path. */
static char* _table_path_dup(const char *path) {
    size_t size = strlen(path) + 1;
    char *copy = (char*)malloc(size);
    memcpy(copy, path, size);
    return copy;
}

fs_datablock_t* table_borrow_page(const table_t *this, uint32_t page_id) {
    return fs_borrow(_table_path_dup(this->path),
            (size_t)page_id * PAGE_SIZE, PAGE_SIZE);
}

static table_t* _table_new(const char *db_name, const char *table_name) {
    table_t *table = (table_t*)malloc(sizeof(table_t));
    fs_tmp_string path = fs_name_db_table(db_name, table_name);
    strcpy(table->path, path);
    free(path);
    return table;
}

int table_create(table_t **this, const char *db_name,
        const char *table_name, const uint32_t m, const column_t *columns,
        const char *const *defaults) {
    *this = NULL;
    table_t *table = _table_new(db_name, table_name);
    if(fs_create(_table_path_dup(table->path)) != 0) {
        free(table);
        return -1;
    }
    table->header_page = table_borrow_page(table, 0);
    table_header_build(table_get_header(table), m, columns, defaults);
    *this = table;
    return 0;
}

int table_open(table_t **this, const char *db_name, const char *table_name) {
    *this = NULL;
    table_t *table = _table_new(db_name, table_name);
    if(!fs_exists(_table_path_dup(table->path))) {
        free(table);
        return -1;
    }
    table->header_page = table_borrow_page(table, 0);
    *this = table;
    return 0;
}

void table_close(table_t *this) {
    fs_return(this->header_page);
    free(this);
}
