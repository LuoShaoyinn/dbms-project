/* table.c
 *      Simple table
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constant.h"
#include "elem_type/elem_type.h"
#include "fs/fs.h"
#include "table.h"

/* Page 0 layout:
 *      uint32_t m
 *      uint32_t page_count
 *      uint32_t free_page_hint
 *      for each column:
 *          uint32_t key_len, char key_name[key_len]
 *          uint32_t fmt_len, char fmt[fmt_len]
 */

static size_t _table_header_size(const table_header_t *this) {
    size_t size = 3 * sizeof(uint32_t);
    for(uint32_t c = 0; c < this->m; ++c) {
        size += sizeof(uint32_t) + strlen(this->key_name[c]) + 1;
        size += sizeof(uint32_t) + strlen(this->fmt[c].fmt) + 1;
    }
    return size;
}

size_t table_get_null_bitmap_size(const table_header_t *p_head) {
    return ((size_t)p_head->m + 7) / 8;
}

size_t table_get_record_size(const table_header_t *p_head) {
    return table_get_null_bitmap_size(p_head)
            + p_head->offset_presum[p_head->m];
}

uint32_t table_get_slots_per_page(const table_header_t *p_head) {
    size_t record_size = table_get_record_size(p_head);
    if(record_size == 0) return 0;
    size_t slots = (PAGE_SIZE - sizeof(uint64_t)) / record_size;
    return slots > TABLE_SLOT_MAX ? TABLE_SLOT_MAX : (uint32_t)slots;
}

int table_header_validate(const table_header_t *this) {
    if(this->m == 0 || this->m > MAX_COLUMN) return -1;
    if(_table_header_size(this) > PAGE_SIZE) return -1;

    for(uint32_t c = 0; c < this->m; ++c) {
        if(strlen(this->key_name[c]) + 1 > MAX_KEY_NAME_LENGTH) return -1;
        if(strlen(this->fmt[c].fmt) + 1 > MAX_FMT_LENGTH) return -1;
    }

    if(table_get_slots_per_page(this) == 0) return -1;
    return 0;
}

void table_header_init(table_header_t *this, void *p_buffer) {
    char *p = (char*)p_buffer;
    uint32_t m;

    memcpy(&m, p, sizeof(m)); p += sizeof(m);
    if(m > MAX_COLUMN) {
        perror("Too many columns");
        exit(-1);
    }

    this->m = m;
    memcpy(&this->page_count, p, sizeof(this->page_count));
    p += sizeof(this->page_count);
    memcpy(&this->free_page_hint, p, sizeof(this->free_page_hint));
    p += sizeof(this->free_page_hint);

    this->key_name = (char**)malloc(sizeof(char*) * this->m);
    this->fmt = (elem_type_t*)malloc(sizeof(elem_type_t) * this->m);
    this->offset_presum = (size_t*)malloc(sizeof(size_t) * (this->m + 1));
    this->offset_presum[0] = 0;

    for(uint32_t c = 0; c < this->m; ++c) {
        uint32_t key_len;
        memcpy(&key_len, p, sizeof(key_len)); p += sizeof(key_len);
        if(key_len == 0 || key_len > MAX_KEY_NAME_LENGTH) {
            perror("Invalid key name");
            exit(-1);
        }
        this->key_name[c] = (char*)malloc(sizeof(char) * key_len);
        memcpy(this->key_name[c], p, key_len); p += key_len;
        this->key_name[c][key_len - 1] = '\0';

        uint32_t fmt_len;
        memcpy(&fmt_len, p, sizeof(fmt_len)); p += sizeof(fmt_len);
        if(fmt_len == 0 || fmt_len > MAX_FMT_LENGTH) {
            perror("Invalid format");
            exit(-1);
        }
        char *fmt = (char*)malloc(sizeof(char) * fmt_len);
        memcpy(fmt, p, fmt_len); p += fmt_len;
        fmt[fmt_len - 1] = '\0';
        elem_type_init(&this->fmt[c], fmt);
        free(fmt);

        this->offset_presum[c + 1] = this->offset_presum[c]
                + elem_type_sizeof(&this->fmt[c]);
    }
}

int table_header_save(table_header_t *this, void *p_buffer) {
    if(table_header_validate(this) != 0) return -1;

    char *p = (char*)p_buffer;

    memcpy(p, &this->m, sizeof(this->m)); p += sizeof(this->m);
    memcpy(p, &this->page_count, sizeof(this->page_count));
    p += sizeof(this->page_count);
    memcpy(p, &this->free_page_hint, sizeof(this->free_page_hint));
    p += sizeof(this->free_page_hint);

    for(uint32_t c = 0; c < this->m; ++c) {
        uint32_t key_len = (uint32_t)strlen(this->key_name[c]) + 1;
        memcpy(p, &key_len, sizeof(key_len)); p += sizeof(key_len);
        memcpy(p, this->key_name[c], key_len); p += key_len;

        uint32_t fmt_len = (uint32_t)strlen(this->fmt[c].fmt) + 1;
        memcpy(p, &fmt_len, sizeof(fmt_len)); p += sizeof(fmt_len);
        elem_type_save(&this->fmt[c], p); p += fmt_len;
    }

    return 0;
}

void table_header_destory(table_header_t *this) {
    for(uint32_t c = 0; c < this->m; ++c) {
        free(this->key_name[c]);
        elem_type_destory(&this->fmt[c]);
    }
    free(this->key_name);
    free(this->fmt);
    free(this->offset_presum);
}

uint32_t table_header_search_key_name(table_header_t *this, \
        const char *keyname) {
    for(uint32_t c = 0; c < this->m; ++c)
        if(strcmp(this->key_name[c], keyname) == 0) return c;
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
    return slots == 0
            || table_page_first_free_slot(p_head, slot_bitmap) == slots;
}

size_t table_get_cell_offset(const table_header_t *p_head, const uint32_t c) {
    return table_get_null_bitmap_size(p_head) + p_head->offset_presum[c];
}

uint32_t table_get_pageid(const table_header_t *p_head, const uint32_t rid) {
    uint32_t slots = table_get_slots_per_page(p_head);
    if(slots == 0) return 0;
    return rid / slots + 1;
}

size_t table_get_record_offset_in_page(const table_header_t *p_head,
        const uint32_t rid) {
    uint32_t slots = table_get_slots_per_page(p_head);
    if(slots == 0) return 0;
    return sizeof(uint64_t) + (rid % slots) * table_get_record_size(p_head);
}

size_t table_get_page_offset_in_file(const table_header_t *p_head,
        const uint32_t rid) {
    return (size_t)table_get_pageid(p_head, rid) * PAGE_SIZE
            + table_get_record_offset_in_page(p_head, rid);
}

/* ---- record CRUD ---- */

struct _s_table_scan {
    table_t *table;
    uint32_t page_id;
    uint32_t slot;
    fs_datablock_t *block;      /* borrowed current page, or NULL */
};

static char* _table_path_dup(const char *path) {
    if(path == NULL) return NULL;
    size_t len = strlen(path) + 1;
    char *copy = (char*)malloc(len);
    if(copy != NULL) memcpy(copy, path, len);
    return copy;
}

static fs_datablock_t* _table_borrow_page(const table_t *this,
        const uint32_t page_id) {
    char *path = _table_path_dup(this->path);
    if(path == NULL) return NULL;
    return fs_borrow(path, (size_t)page_id * PAGE_SIZE, PAGE_SIZE);
}

static table_status_t _table_save_header(table_t *this) {
    fs_datablock_t *block = _table_borrow_page(this, 0);
    if(block == NULL) return TABLE_ERR_IO;

    int ret = table_header_save(&this->head, block->ptr);
    fs_return(block);
    return ret == 0 ? TABLE_OK : TABLE_ERR_INVALID;
}

static int _table_header_build(table_header_t *this, const uint32_t m,
        const char *const *key_name, const char *const *fmt,
        const uint32_t page_count, const uint32_t free_page_hint) {
    this->m = m;
    this->page_count = page_count;
    this->free_page_hint = free_page_hint;
    this->key_name = (char**)malloc(sizeof(char*) * m);
    this->fmt = (elem_type_t*)malloc(sizeof(elem_type_t) * m);
    this->offset_presum = (size_t*)malloc(sizeof(size_t) * (m + 1));
    if(this->key_name == NULL || this->fmt == NULL
            || this->offset_presum == NULL) {
        free(this->key_name);
        free(this->fmt);
        free(this->offset_presum);
        this->m = 0;
        return -1;
    }

    this->offset_presum[0] = 0;
    for(uint32_t c = 0; c < m; ++c) {
        size_t key_len = strlen(key_name[c]) + 1;
        this->key_name[c] = (char*)malloc(key_len);
        if(this->key_name[c] == NULL) {
            this->m = c;
            table_header_destory(this);
            return -1;
        }
        memcpy(this->key_name[c], key_name[c], key_len);

        elem_type_init(&this->fmt[c], fmt[c]);
        this->offset_presum[c + 1] = this->offset_presum[c]
                + elem_type_sizeof(&this->fmt[c]);
    }
    return 0;
}

static void _table_normalize_hint(table_t *this) {
    if(this->head.page_count == 0) {
        this->head.free_page_hint = 1;
    } else if(this->head.free_page_hint == 0
            || this->head.free_page_hint > this->head.page_count) {
        this->head.free_page_hint = 1;
    }
}

/* Resolve a RID only when its page is allocated. Occupancy is checked by the
 * caller after the page is borrowed. */
static int _table_rid_page(const table_t *this, const uint32_t rid,
        uint32_t *page_id) {
    uint32_t slots = table_get_slots_per_page(&this->head);
    if(slots == 0) return 0;

    uint64_t page_index = (uint64_t)rid / slots;
    if(page_index >= this->head.page_count) return 0;

    *page_id = (uint32_t)page_index + 1;
    return 1;
}

table_status_t table_create(table_t **this, const char *db_name,
        const char *table_name, const uint32_t m,
        const char *const *key_name, const char *const *fmt) {
    if(this == NULL || db_name == NULL || table_name == NULL) {
        return TABLE_ERR_INVALID;
    }
    if(m == 0 || m > MAX_COLUMN || key_name == NULL || fmt == NULL) {
        return TABLE_ERR_INVALID;
    }
    for(uint32_t c = 0; c < m; ++c) {
        if(key_name[c] == NULL || fmt[c] == NULL) {
            return TABLE_ERR_INVALID;
        }
    }
    *this = NULL;

    table_t *table = (table_t*)malloc(sizeof(table_t));
    if(table == NULL) return TABLE_ERR;

    /* Validate before touching the filesystem so a duplicate create cannot
     * modify the existing file. */
    if(_table_header_build(&table->head, m, key_name, fmt, 0, 1) != 0) {
        free(table);
        return TABLE_ERR;
    }
    if(table_header_validate(&table->head) != 0) {
        table_header_destory(&table->head);
        free(table);
        return TABLE_ERR_INVALID;
    }

    table->path = fs_name_db_table(db_name, table_name);
    if(table->path == NULL) {
        table_header_destory(&table->head);
        free(table);
        return TABLE_ERR;
    }

    char *probe = _table_path_dup(table->path);
    int created = fs_create(probe);
    if(created != 0) {
        table_header_destory(&table->head);
        free(table->path);
        free(table);
        return created == 1 ? TABLE_ERR_EXISTS : TABLE_ERR_IO;
    }

    if(_table_save_header(table) != TABLE_OK) {
        fs_remove(_table_path_dup(table->path));
        table_header_destory(&table->head);
        free(table->path);
        free(table);
        return TABLE_ERR_IO;
    }

    *this = table;
    return TABLE_OK;
}

table_status_t table_open(table_t **this, const char *db_name,
        const char *table_name) {
    if(this == NULL || db_name == NULL || table_name == NULL) {
        return TABLE_ERR_INVALID;
    }
    *this = NULL;

    table_t *table = (table_t*)malloc(sizeof(table_t));
    if(table == NULL) return TABLE_ERR;

    table->path = fs_name_db_table(db_name, table_name);
    if(table->path == NULL) {
        free(table);
        return TABLE_ERR;
    }

    /* Probe with open(2) so a missing table is never created. */
    char *probe = _table_path_dup(table->path);
    if(probe == NULL) {
        free(table->path);
        free(table);
        return TABLE_ERR;
    }
    if(!fs_exists(probe)) {
        free(table->path);
        free(table);
        return TABLE_ERR_NOT_FOUND;
    }

    fs_datablock_t *block = _table_borrow_page(table, 0);
    if(block == NULL) {
        free(table->path);
        free(table);
        return TABLE_ERR_IO;
    }
    table_header_init(&table->head, block->ptr);
    fs_return(block);

    if(table_header_validate(&table->head) != 0) {
        table_header_destory(&table->head);
        free(table->path);
        free(table);
        return TABLE_ERR_INVALID;
    }
    _table_normalize_hint(table);

    *this = table;
    return TABLE_OK;
}

void table_close(table_t *this) {
    if(this == NULL) return;
    table_header_destory(&this->head);
    free(this->path);
    free(this);
}

table_status_t table_insert(table_t *this, const void *record,
        const size_t size, uint32_t *rid) {
    if(this == NULL || record == NULL || rid == NULL) {
        return TABLE_ERR_INVALID;
    }
    if(size != table_get_record_size(&this->head)) {
        return TABLE_ERR_INVALID;
    }

    uint32_t slots = table_get_slots_per_page(&this->head);
    if(slots == 0) return TABLE_ERR_INVALID;

    _table_normalize_hint(this);
    uint32_t page_count = this->head.page_count;
    uint32_t start = page_count == 0 ? 1 : this->head.free_page_hint;
    uint32_t page_id = 0;
    uint32_t slot = 0;
    uint64_t bitmap = 0;
    fs_datablock_t *block = NULL;

    /* Prefer pages from free_page_hint, wrapping through existing pages
     * before growing the file. */
    for(uint32_t i = 0; i < page_count; ++i) {
        uint32_t candidate = start + i;
        if(candidate > page_count) candidate -= page_count;

        block = _table_borrow_page(this, candidate);
        if(block == NULL) return TABLE_ERR_IO;

        memcpy(&bitmap, block->ptr, sizeof(bitmap));
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

        uint64_t rid64 = ((uint64_t)page_id - 1) * slots;
        if(rid64 > UINT32_MAX) return TABLE_ERR_FULL;

        block = _table_borrow_page(this, page_id);
        if(block == NULL) return TABLE_ERR_IO;
        memset(block->ptr, 0, PAGE_SIZE);
        bitmap = 0;
        slot = 0;
        page_count = page_id;
    } else {
        uint64_t rid64 = ((uint64_t)page_id - 1) * slots + slot;
        if(rid64 > UINT32_MAX) {
            fs_return(block);
            return TABLE_ERR_FULL;
        }
    }

    size_t record_size = table_get_record_size(&this->head);
    bitmap |= (uint64_t)1 << slot;
    memcpy(block->ptr, &bitmap, sizeof(bitmap));
    size_t offset = sizeof(uint64_t) + (size_t)slot * record_size;
    memcpy((char*)block->ptr + offset, record, record_size);
    fs_return(block);

    this->head.page_count = page_count;
    if(table_page_is_full(&this->head, bitmap)) {
        this->head.free_page_hint = page_id >= page_count ? 1 : page_id + 1;
    } else {
        this->head.free_page_hint = page_id;
    }

    table_status_t status = _table_save_header(this);
    if(status != TABLE_OK) return status;

    *rid = (uint32_t)(((uint64_t)page_id - 1) * slots + slot);
    return TABLE_OK;
}

table_status_t table_get(table_t *this, const uint32_t rid,
        void **record, size_t *size) {
    if(this == NULL || record == NULL || size == NULL) {
        return TABLE_ERR_INVALID;
    }

    uint32_t page_id;
    if(!_table_rid_page(this, rid, &page_id)) return TABLE_ERR_RID;

    uint32_t slots = table_get_slots_per_page(&this->head);
    uint32_t slot = rid % slots;

    fs_datablock_t *block = _table_borrow_page(this, page_id);
    if(block == NULL) return TABLE_ERR_IO;

    uint64_t bitmap;
    memcpy(&bitmap, block->ptr, sizeof(bitmap));
    if((bitmap & ((uint64_t)1 << slot)) == 0) {
        fs_return(block);
        return TABLE_ERR_RID;
    }

    size_t record_size = table_get_record_size(&this->head);
    void *copy = malloc(record_size);
    if(copy == NULL) {
        fs_return(block);
        return TABLE_ERR;
    }

    size_t offset = sizeof(uint64_t) + (size_t)slot * record_size;
    memcpy(copy, (char*)block->ptr + offset, record_size);
    fs_return(block);

    *record = copy;
    *size = record_size;
    return TABLE_OK;
}

table_status_t table_update(table_t *this, const uint32_t rid,
        const void *record, const size_t size) {
    if(this == NULL || record == NULL) return TABLE_ERR_INVALID;
    if(size != table_get_record_size(&this->head)) {
        return TABLE_ERR_INVALID;
    }

    uint32_t page_id;
    if(!_table_rid_page(this, rid, &page_id)) return TABLE_ERR_RID;

    uint32_t slots = table_get_slots_per_page(&this->head);
    uint32_t slot = rid % slots;

    fs_datablock_t *block = _table_borrow_page(this, page_id);
    if(block == NULL) return TABLE_ERR_IO;

    uint64_t bitmap;
    memcpy(&bitmap, block->ptr, sizeof(bitmap));
    if((bitmap & ((uint64_t)1 << slot)) == 0) {
        fs_return(block);
        return TABLE_ERR_RID;
    }

    size_t offset = sizeof(uint64_t) + (size_t)slot * size;
    memcpy((char*)block->ptr + offset, record, size);
    fs_return(block);
    return TABLE_OK;
}

table_status_t table_delete(table_t *this, const uint32_t rid) {
    if(this == NULL) return TABLE_ERR_INVALID;

    uint32_t page_id;
    if(!_table_rid_page(this, rid, &page_id)) return TABLE_ERR_RID;

    uint32_t slots = table_get_slots_per_page(&this->head);
    uint32_t slot = rid % slots;

    fs_datablock_t *block = _table_borrow_page(this, page_id);
    if(block == NULL) return TABLE_ERR_IO;

    uint64_t bitmap;
    memcpy(&bitmap, block->ptr, sizeof(bitmap));
    if((bitmap & ((uint64_t)1 << slot)) == 0) {
        fs_return(block);
        return TABLE_ERR_RID;
    }

    bitmap &= ~((uint64_t)1 << slot);
    memcpy(block->ptr, &bitmap, sizeof(bitmap));
    fs_return(block);

    /* Revisit the earliest freed page, but never move the hint backwards
     * past pages that were already full. */
    _table_normalize_hint(this);
    if(page_id < this->head.free_page_hint) {
        this->head.free_page_hint = page_id;
    }

    return _table_save_header(this);
}

table_status_t table_scan_open(table_t *this, table_scan_t **scan) {
    if(this == NULL || scan == NULL) return TABLE_ERR_INVALID;

    table_scan_t *state = (table_scan_t*)malloc(sizeof(table_scan_t));
    if(state == NULL) return TABLE_ERR;

    state->table = this;
    state->page_id = 1;
    state->slot = 0;
    state->block = NULL;

    *scan = state;
    return TABLE_OK;
}

table_status_t table_scan_next(table_scan_t *scan, uint32_t *rid,
        void **record, size_t *size) {
    if(scan == NULL || rid == NULL || record == NULL || size == NULL) {
        return TABLE_ERR_INVALID;
    }

    table_t *this = scan->table;
    uint32_t slots = table_get_slots_per_page(&this->head);
    size_t record_size = table_get_record_size(&this->head);

    while(scan->page_id <= this->head.page_count) {
        if(slots == 0) break;

        if(scan->block == NULL) {
            scan->block = _table_borrow_page(this, scan->page_id);
            if(scan->block == NULL) return TABLE_ERR_IO;
        }

        uint64_t bitmap;
        memcpy(&bitmap, scan->block->ptr, sizeof(bitmap));

        while(scan->slot < slots) {
            uint32_t slot = scan->slot;
            if((bitmap & ((uint64_t)1 << slot)) == 0) {
                ++scan->slot;
                continue;
            }

            uint64_t rid64 = ((uint64_t)scan->page_id - 1) * slots + slot;
            if(rid64 > UINT32_MAX) {
                fs_return(scan->block);
                scan->block = NULL;
                return TABLE_ERR_IO;
            }

            void *copy = malloc(record_size);
            if(copy == NULL) {
                fs_return(scan->block);
                scan->block = NULL;
                return TABLE_ERR;
            }

            size_t offset = sizeof(uint64_t) + (size_t)slot * record_size;
            memcpy(copy, (char*)scan->block->ptr + offset, record_size);
            ++scan->slot;

            *rid = (uint32_t)rid64;
            *record = copy;
            *size = record_size;
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
    if(scan == NULL) return;
    if(scan->block != NULL) fs_return(scan->block);
    free(scan);
}