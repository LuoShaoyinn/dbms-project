/* record.h
 *      Borrowed view of a fixed-length record; no copying or scan object.
 *
 * Callers supply valid tables, column IDs and allocated-page RIDs. A free
 * slot is represented by ptr == NULL. Enumerate RIDs up to page_count *
 * slots_per_page to traverse a table; a free slot is a hole, not EOF.
 *
 * Every nonempty record owns the original fs_borrow handle. Return it once,
 * before closing the table. Do not copy ownership, retain field pointers
 * after return, or delete a record while another caller is using it.
 */
#ifndef _H_RECORD
#define _H_RECORD

#include "table/table.h"

typedef struct _s_record {
    void *ptr;                    /* NULL bitmap followed by payloads */
    column_t *ref_column;          /* mapped columns, owned by the table */
    fs_datablock_t *block;         /* original fs_borrow token */
    uint32_t rid;
} record_t;

record_t record_borrow(table_t *table, uint32_t rid);
record_t record_alloc(table_t *table);
void record_return(record_t *this);
/* Caller supplies an occupied RID. No compaction or movement of other rows. */
int record_delete(table_t *table, uint32_t rid);

void* record_field(const record_t *this, uint32_t column);
bool record_is_null(const record_t *this, uint32_t column);
void record_set_null(record_t *this, uint32_t column, bool is_null);

#endif // _H_RECORD
