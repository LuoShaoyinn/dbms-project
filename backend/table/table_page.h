/* table_page.h
 *      Internal page helpers for backend/table
 *
 * Inputs are assumed valid. Allocation failures are fatal, so these helpers
 * only report semantic errors such as a free/out-of-range RID.
 */

#ifndef _H_TABLE_PAGE
#define _H_TABLE_PAGE

#include <stddef.h>
#include <stdint.h>

#include "fs/fs.h"
#include "table.h"

/* Fatal-on-failure allocation. */
void* _table_xmalloc(size_t size);

/* Path and page access. */
char* _table_path_dup(const char *path);
fs_datablock_t* _table_borrow_page(const table_t *this, uint32_t page_id);

/* Allocation metadata stored in page 0. */
void _table_save_header(table_t *this);
void _table_refresh(table_t *this);
void _table_normalize_hint(table_t *this);

/* RID mapping. */
int _table_rid_page(const table_t *this, uint32_t rid, uint32_t *page_id);
int _table_make_rid(uint32_t page_id, uint32_t slot, uint32_t slots,
        uint32_t *rid);

/* Slot layout and bitmap access. */
size_t _table_slot_offset(const table_header_t *p_head, uint32_t slot);
uint64_t _table_bitmap_load(const fs_datablock_t *block);
void _table_bitmap_store(fs_datablock_t *block, uint64_t bitmap);

/* Locate an occupied RID and borrow its page. The caller must fs_return()
 * *block. On failure *block is NULL. */
table_status_t _table_borrow_slot(table_t *this, uint32_t rid,
        uint32_t *page_id, uint32_t *slot, uint64_t *bitmap,
        fs_datablock_t **block);

/* Copy one slot into a caller-owned buffer. */
void* _table_copy_slot(const table_header_t *p_head,
        const fs_datablock_t *block, uint32_t slot, size_t *size);

#endif // _H_TABLE_PAGE