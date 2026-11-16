/* column.h
 *      Column metadata for the table header
 *
 * A column stores its SQL name and scalar type inline, so a
 * table header is pointer-free and can be mapped directly from page 0.
 *
 * offset and default_offset are filled in by table_header_build(). offset is
 * relative to the start of a record; default_offset is relative to the start
 * of the table header, with 0 meaning no default or DEFAULT NULL.
 */

#ifndef _H_COLUMN
#define _H_COLUMN

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "constant.h"
#include "elem_type/elem_type.h"

typedef struct _s_column {
    char name[MAX_KEY_NAME_LENGTH];
    elem_type_t type;

    uint32_t offset;          /* byte offset within a record */
    uint32_t default_offset;  /* offset from header start; 0 means NULL */
    uint8_t not_null;
} column_t;

/* Storage size of one value. */
size_t column_size(const column_t *this);

/* VARCHAR capacity; 0 for numeric types. */
uint32_t column_length(const column_t *this);

/* Human-readable SQL type for DESC. */
const char* column_type_name(const column_t *this);

/* Resolve a column default in a built header. */
static inline const char* column_default(const void *header,
        const column_t *this) {
    return this->default_offset == 0
            ? NULL
            : (const char*)header + this->default_offset;
}

#endif // _H_COLUMN
