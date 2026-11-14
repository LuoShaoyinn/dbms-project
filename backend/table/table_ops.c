/* table_ops.c
 *      Table lifecycle, record CRUD, and scans
 */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "constant.h"
#include "elem_type/elem_type.h"
#include "fs/fs.h"
#include "table.h"
#include "table_page.h"

struct _s_table_scan {
    table_t *table;
    uint32_t page_id;
    uint32_t slot;
    fs_datablock_t *block;      /* borrowed current page, or NULL */
};

static table_t* _table_new(const char *db_name, const char *table_name) {
    table_t *table = (table_t*)_table_xmalloc(sizeof(table_t));
    memset(table, 0, sizeof(table_t));
    table->path = fs_name_db_table(db_name, table_name);
    return table;
}

static void _table_discard(table_t *table) {
    table_header_destory(&table->head);
    free(table->path);
    free(table);
}

static void _table_header_build(table_header_t *this, const uint32_t m,
        const char *const *key_name, const char *const *fmt) {
    this->m = m;
    this->page_count = 0;
    this->free_page_hint = 1;
    this->key_name = (char**)_table_xmalloc(sizeof(char*) * m);
    this->fmt = (elem_type_t*)_table_xmalloc(sizeof(elem_type_t) * m);
    this->offset_presum = (size_t*)_table_xmalloc(sizeof(size_t) * (m + 1));

    this->offset_presum[0] = 0;
    for(uint32_t c = 0; c < m; ++c) {
        size_t key_len = strlen(key_name[c]) + 1;
        this->key_name[c] = (char*)_table_xmalloc(key_len);
        memcpy(this->key_name[c], key_name[c], key_len);

        elem_type_init(&this->fmt[c], fmt[c]);
        this->offset_presum[c + 1] = this->offset_presum[c]
                + elem_type_sizeof(&this->fmt[c]);
    }
}

table_status_t table_create(table_t **this, const char *db_name,
        const char *table_name, const uint32_t m,
        const char *const *key_name, const char *const *fmt) {
    *this = NULL;
    table_t *table = _table_new(db_name, table_name);
    _table_header_build(&table->head, m, key_name, fmt);

    int created = fs_create(_table_path_dup(table->path));
    if(created != 0) {
        _table_discard(table);
        return created == 1 ? TABLE_ERR_EXISTS : TABLE_ERR_IO;
    }

    _table_save_header(table);
    *this = table;
    return TABLE_OK;
}

table_status_t table_open(table_t **this, const char *db_name,
        const char *table_name) {
    *this = NULL;
    table_t *table = _table_new(db_name, table_name);

    /* Probe with open(2) so a missing table is never created. */
    if(!fs_exists(_table_path_dup(table->path))) {
        _table_discard(table);
        return TABLE_ERR_NOT_FOUND;
    }

    fs_datablock_t *block = _table_borrow_page(table, 0);
    table_header_init(&table->head, block->ptr);
    fs_return(block);
    _table_normalize_hint(table);

    *this = table;
    return TABLE_OK;
}

void table_close(table_t *this) {
    _table_discard(this);
}

table_status_t table_insert(table_t *this, const void *record,
        const size_t size, uint32_t *rid) {
    size_t record_size = table_get_record_size(&this->head);
    if(size != record_size) return TABLE_ERR_INVALID;

    uint32_t slots = table_get_slots_per_page(&this->head);
    _table_refresh(this);

    uint32_t page_count = this->head.page_count;
    uint32_t start = page_count == 0 ? 1 : this->head.free_page_hint;
    uint32_t page_id = 0;
    uint32_t slot = 0;
    uint32_t new_rid = 0;
    uint64_t bitmap = 0;
    fs_datablock_t *block = NULL;

    /* Pages before free_page_hint are full, so inspect only the hint and
     * later pages. This keeps append-only inserts from rescanning every
     * earlier full page. */
    for(uint64_t i = start; i <= page_count; ++i) {
        uint32_t candidate = (uint32_t)i;

        block = _table_borrow_page(this, candidate);
        bitmap = _table_bitmap_load(block);
        slot = table_page_first_free_slot(&this->head, bitmap);
        if(slot < slots) {
            page_id = candidate;
            break;
        }
        fs_return(block);
        block = NULL;
    }

    if(page_id == 0) {
        if(page_count == UINT32_MAX) return TABLE_ERR_FULL;
        page_id = page_count + 1;

        if(!_table_make_rid(page_id, 0, slots, &new_rid))
            return TABLE_ERR_FULL;

        block = _table_borrow_page(this, page_id);
        memset(block->ptr, 0, PAGE_SIZE);
        bitmap = 0;
        slot = 0;
        page_count = page_id;
    } else if(!_table_make_rid(page_id, slot, slots, &new_rid)) {
        fs_return(block);
        return TABLE_ERR_FULL;
    }

    bitmap |= (uint64_t)1 << slot;
    _table_bitmap_store(block, bitmap);
    memcpy((char*)block->ptr + _table_slot_offset(&this->head, slot),
            record, record_size);
    fs_return(block);

    this->head.page_count = page_count;
    if(table_page_is_full(&this->head, bitmap) && page_id < page_count) {
        this->head.free_page_hint = page_id + 1;
    } else {
        /* Keep the hint on the last full page instead of wrapping to page 1;
         * the next append then checks that page and grows the file. */
        this->head.free_page_hint = page_id;
    }

    _table_save_header(this);
    *rid = new_rid;
    return TABLE_OK;
}

table_status_t table_get(table_t *this, const uint32_t rid,
        void **record, size_t *size) {
    uint32_t page_id = 0;
    uint32_t slot = 0;
    uint64_t bitmap = 0;
    fs_datablock_t *block = NULL;
    table_status_t status = _table_borrow_slot(this, rid, &page_id, &slot,
            &bitmap, &block);
    if(status != TABLE_OK) return status;

    *record = _table_copy_slot(&this->head, block, slot, size);
    fs_return(block);
    return TABLE_OK;
}

table_status_t table_update(table_t *this, const uint32_t rid,
        const void *record, const size_t size) {
    if(size != table_get_record_size(&this->head)) {
        return TABLE_ERR_INVALID;
    }

    uint32_t page_id = 0;
    uint32_t slot = 0;
    uint64_t bitmap = 0;
    fs_datablock_t *block = NULL;
    table_status_t status = _table_borrow_slot(this, rid, &page_id, &slot,
            &bitmap, &block);
    if(status != TABLE_OK) return status;

    memcpy((char*)block->ptr + _table_slot_offset(&this->head, slot),
            record, size);
    fs_return(block);
    return TABLE_OK;
}

table_status_t table_delete(table_t *this, const uint32_t rid) {
    uint32_t page_id = 0;
    uint32_t slot = 0;
    uint64_t bitmap = 0;
    fs_datablock_t *block = NULL;
    table_status_t status = _table_borrow_slot(this, rid, &page_id, &slot,
            &bitmap, &block);
    if(status != TABLE_OK) return status;

    bitmap &= ~((uint64_t)1 << slot);
    _table_bitmap_store(block, bitmap);
    fs_return(block);

    /* Revisit the earliest freed page, but never move the hint backwards
     * past pages that were already full. */
    _table_normalize_hint(this);
    if(page_id < this->head.free_page_hint) {
        this->head.free_page_hint = page_id;
    }

    _table_save_header(this);
    return TABLE_OK;
}

table_status_t table_scan_open(table_t *this, table_scan_t **scan) {
    _table_refresh(this);

    table_scan_t *state = (table_scan_t*)_table_xmalloc(sizeof(table_scan_t));
    state->table = this;
    state->page_id = 1;
    state->slot = 0;
    state->block = NULL;

    *scan = state;
    return TABLE_OK;
}

table_status_t table_scan_next(table_scan_t *scan, uint32_t *rid,
        void **record, size_t *size) {
    table_t *this = scan->table;
    uint32_t slots = table_get_slots_per_page(&this->head);

    while(scan->page_id <= this->head.page_count) {
        if(scan->block == NULL)
            scan->block = _table_borrow_page(this, scan->page_id);

        uint64_t bitmap = _table_bitmap_load(scan->block);
        while(scan->slot < slots) {
            uint32_t slot = scan->slot;
            if((bitmap & ((uint64_t)1 << slot)) == 0) {
                ++scan->slot;
                continue;
            }

            uint32_t value = 0;
            if(!_table_make_rid(scan->page_id, slot, slots, &value)) {
                fs_return(scan->block);
                scan->block = NULL;
                return TABLE_ERR_IO;
            }

            *record = _table_copy_slot(&this->head, scan->block, slot, size);
            ++scan->slot;

            *rid = value;
            return TABLE_OK;
        }

        fs_return(scan->block);
        scan->block = NULL;
        ++scan->page_id;
        scan->slot = 0;
    }

    return TABLE_SCAN_END;
}

void table_scan_close(table_scan_t *scan) {
    if(scan->block != NULL) fs_return(scan->block);
    free(scan);
}