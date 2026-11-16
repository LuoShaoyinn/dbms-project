/* record.c
 *      Direct mapped record access. Inputs and ownership are trusted.
 */
#include <string.h>

#include "record.h"

static record_t _record_view(table_t *table, uint32_t rid,
        fs_datablock_t *block) {
    table_header_t *head = table_get_header(table);
    return (record_t){
        .ptr = (char*)block->ptr + table_get_record_offset_in_page(head, rid),
        .ref_column = head->columns,
        .block = block,
        .rid = rid,
    };
}

record_t record_borrow(table_t *table, uint32_t rid) {
    table_header_t *head = table_get_header(table);
    uint32_t slots = table_get_slots_per_page(head);
    fs_datablock_t *block = table_borrow_page(table, rid / slots + 1);
    table_page_t *page = (table_page_t*)block->ptr;
    if(!(page->slot_bitmap & (UINT64_C(1) << (rid % slots)))) {
        fs_return(block);
        return (record_t){0};
    }
    return _record_view(table, rid, block);
}

record_t record_alloc(table_t *table) {
    table_header_t *head = table_get_header(table);
    uint32_t slots = table_get_slots_per_page(head);
    uint32_t page_id = head->free_page_hint;
    uint32_t slot;
    fs_datablock_t *block;
    table_page_t *page;

    for(;; ++page_id) {
        block = table_borrow_page(table, page_id);
        page = (table_page_t*)block->ptr;
        if(page_id > head->page_count) {
            memset(page, 0, PAGE_SIZE);
            head->page_count = page_id;
        }
        slot = table_page_first_free_slot(head, page->slot_bitmap);
        if(slot < slots) break;
        fs_return(block);
    }

    page->slot_bitmap |= UINT64_C(1) << slot;
    head->free_page_hint = table_page_is_full(head, page->slot_bitmap)
            ? page_id + 1 : page_id;

    record_t record = _record_view(table, (page_id - 1) * slots + slot, block);
    memset(record.ptr, 0, head->record_size);
    return record;
}

void record_return(record_t *this) {
    fs_return(this->block);
}

int record_delete(table_t *table, uint32_t rid) {
    table_header_t *head = table_get_header(table);
    uint32_t slots = table_get_slots_per_page(head);
    uint32_t page_id = rid / slots + 1;
    fs_datablock_t *block = table_borrow_page(table, page_id);
    ((table_page_t*)block->ptr)->slot_bitmap &= ~(UINT64_C(1) << (rid % slots));
    if(page_id < head->free_page_hint) head->free_page_hint = page_id;
    fs_return(block);
    return 0;
}

void* record_field(const record_t *this, uint32_t column) {
    return (char*)this->ptr + this->ref_column[column].offset;
}

bool record_is_null(const record_t *this, uint32_t column) {
    return (((unsigned char*)this->ptr)[column / 8] & (1u << (column % 8))) != 0;
}

void record_set_null(record_t *this, uint32_t column, bool is_null) {
    unsigned char *byte = (unsigned char*)this->ptr + column / 8;
    unsigned char mask = (unsigned char)(1u << (column % 8));
    if(is_null) *byte |= mask;
    else *byte &= (unsigned char)~mask;
}
