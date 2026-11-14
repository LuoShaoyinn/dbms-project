/* table_page.c
 *      Internal page helpers for backend/table
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constant.h"
#include "fs/fs.h"
#include "table.h"
#include "table_page.h"

void* _table_xmalloc(const size_t size) {
    void *ptr = malloc(size);
    if(ptr == NULL) {
        perror("table: malloc");
        exit(-1);
    }
    return ptr;
}

char* _table_path_dup(const char *path) {
    size_t len = strlen(path) + 1;
    char *copy = (char*)_table_xmalloc(len);
    memcpy(copy, path, len);
    return copy;
}

fs_datablock_t* _table_borrow_page(const table_t *this,
        const uint32_t page_id) {
    return fs_borrow(_table_path_dup(this->path),
            (size_t)page_id * PAGE_SIZE, PAGE_SIZE);
}

void _table_save_header(table_t *this) {
    fs_datablock_t *block = _table_borrow_page(this, 0);
    if(table_header_save(&this->head, block->ptr) != 0) exit(-1);
    fs_return(block);
}

void _table_normalize_hint(table_t *this) {
    if(this->head.page_count == 0) {
        this->head.free_page_hint = 1;
    } else if(this->head.free_page_hint == 0
            || this->head.free_page_hint > this->head.page_count) {
        this->head.free_page_hint = 1;
    }
}

void _table_refresh(table_t *this) {
    fs_datablock_t *block = _table_borrow_page(this, 0);
    const char *p = (const char*)block->ptr;
    memcpy(&this->head.page_count, p + sizeof(uint32_t),
            sizeof(this->head.page_count));
    memcpy(&this->head.free_page_hint, p + 2 * sizeof(uint32_t),
            sizeof(this->head.free_page_hint));
    fs_return(block);
    _table_normalize_hint(this);
}

int _table_rid_page(const table_t *this, const uint32_t rid,
        uint32_t *page_id) {
    uint32_t slots = table_get_slots_per_page(&this->head);
    uint64_t page_index = (uint64_t)rid / slots;
    if(page_index >= this->head.page_count) return 0;

    *page_id = (uint32_t)page_index + 1;
    return 1;
}

int _table_make_rid(const uint32_t page_id, const uint32_t slot,
        const uint32_t slots, uint32_t *rid) {
    uint64_t value = ((uint64_t)page_id - 1) * slots + slot;
    if(value > UINT32_MAX) return 0;

    *rid = (uint32_t)value;
    return 1;
}

size_t _table_slot_offset(const table_header_t *p_head,
        const uint32_t slot) {
    return sizeof(uint64_t)
            + (size_t)slot * table_get_record_size(p_head);
}

uint64_t _table_bitmap_load(const fs_datablock_t *block) {
    uint64_t bitmap;
    memcpy(&bitmap, block->ptr, sizeof(bitmap));
    return bitmap;
}

void _table_bitmap_store(fs_datablock_t *block, const uint64_t bitmap) {
    memcpy(block->ptr, &bitmap, sizeof(bitmap));
}

table_status_t _table_borrow_slot(table_t *this, const uint32_t rid,
        uint32_t *page_id, uint32_t *slot, uint64_t *bitmap,
        fs_datablock_t **block) {
    _table_refresh(this);
    if(!_table_rid_page(this, rid, page_id)) return TABLE_ERR_RID;

    uint32_t slots = table_get_slots_per_page(&this->head);
    *slot = rid % slots;

    *block = _table_borrow_page(this, *page_id);
    *bitmap = _table_bitmap_load(*block);
    if((*bitmap & ((uint64_t)1 << *slot)) == 0) {
        fs_return(*block);
        *block = NULL;
        return TABLE_ERR_RID;
    }
    return TABLE_OK;
}

void* _table_copy_slot(const table_header_t *p_head,
        const fs_datablock_t *block, const uint32_t slot, size_t *size) {
    *size = table_get_record_size(p_head);
    void *copy = _table_xmalloc(*size);
    memcpy(copy, (const char*)block->ptr + _table_slot_offset(p_head, slot),
            *size);
    return copy;
}