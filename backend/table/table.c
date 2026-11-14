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