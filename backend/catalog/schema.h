/* schema.h
 *      Logical column and table schemas
 *
 * A schema describes SQL-level columns. It is independent of the physical
 * table encoding: backend/table only sees a list of key names and storage
 * formats produced from a schema.
 *
 * Ownership:
 *      schema_create() allocates a schema_t and deep-copies every name and
 *      default value. schema_destroy() frees the whole object, including the
 *      schema_t itself, so callers must not free() it separately.
 *      catalog_get_schema() returns an owned schema_t; release it with
 *      schema_destroy().
 *
 * Storage formats:
 *      INT         -> "i"
 *      FLOAT       -> "d"
 *      VARCHAR(n)  -> "s<n>"
 */

#ifndef _H_SCHEMA
#define _H_SCHEMA

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "constant.h"

typedef enum _s_schema_type {
    SCHEMA_TYPE_INT = 0,
    SCHEMA_TYPE_FLOAT = 1,
    SCHEMA_TYPE_VARCHAR = 2
} schema_type_t;

typedef struct _s_schema_column {
    char *name;             /* owned column name */
    schema_type_t type;
    uint32_t length;        /* VARCHAR length; 0 for INT and FLOAT */
    bool not_null;
    bool has_default;
    char *default_value;    /* owned textual default, NULL without one */
} schema_column_t;

typedef struct _s_schema {
    uint32_t column_count;
    schema_column_t *columns;   /* owned array of column_count entries */
} schema_t;

typedef enum _s_schema_status {
    SCHEMA_OK = 0,
    SCHEMA_ERR = -1,
    SCHEMA_ERR_INVALID = -2,
    SCHEMA_ERR_IO = -3
} schema_status_t;

/* schema_create:
 *      validate columns and build an owned deep copy. Returns
 *      SCHEMA_ERR_INVALID when the description is not a legal schema.
 *      On failure *this is NULL. */
schema_status_t schema_create(schema_t **this, uint32_t column_count,
        const schema_column_t *columns);
void schema_destroy(schema_t *this);

/* schema_validate:
 *      return 0 when the schema is legal, -1 otherwise. Checks:
 *      - column count in [1, MAX_COLUMN]
 *      - legal, unique, case-insensitive column names
 *      - VARCHAR lengths in [1, MAX_VARCHAR_LENGTH]
 *      - defaults parse as their column type
 *      - one record and the serialized schema fit in a page. */
int schema_validate(const schema_t *this);

/* Storage format for one logical column, as consumed by table_create().
 * Returns 0 on success and -1 when the output buffer is too small. */
int schema_format(const schema_column_t *column, char *out, size_t size);

/* Human-readable type name for a frontend's DESC output. */
const char* schema_type_name(schema_type_t type);

/* Serialized metadata. Always fits in one PAGE_SIZE-sized buffer when the
 * schema validates. */
size_t schema_serialized_size(const schema_t *this);
schema_status_t schema_save(const schema_t *this, void *buffer, size_t size);

/* schema_load:
 *      parse and validate metadata from buffer. On success *this is an owned
 *      schema_t; on failure *this is NULL. */
schema_status_t schema_load(schema_t **this, const void *buffer, size_t size);

#endif // _H_SCHEMA