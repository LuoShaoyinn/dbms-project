/* catalog_table.c
 *      Catalog-level table creation, drop, and schema lookup
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "constant.h"
#include "fs/fs.h"
#include "table/table.h"
#include "catalog.h"
#include "catalog_internal.h"

static int _catalog_table_paths(char *table_path, char *schema_path,
        const char *db_name, const char *table_name) {
    char table_dir[MAX_FILENAME];
    char schema_dir[MAX_FILENAME];

    if(_catalog_sub_path(table_dir, db_name, TABLE_PREFIX) != 0) return -1;
    if(_catalog_sub_path(schema_dir, db_name, SCHEMA_PREFIX) != 0) return -1;
    if(_catalog_child_path(table_path, table_dir, table_name) != 0) return -1;
    if(_catalog_child_path(schema_path, schema_dir, table_name) != 0) return -1;
    return 0;
}

catalog_status_t catalog_create_table(catalog_t *this, const char *table_name,
        const schema_t *schema) {
    if(this == NULL) return CATALOG_ERR_INVALID;

    const char *db_name = catalog_current_db(this);
    if(db_name == NULL) return CATALOG_ERR_NO_DB;
    if(!_catalog_name_ok(table_name)) return CATALOG_ERR_INVALID;
    if(schema == NULL || schema_validate(schema) != 0)
        return CATALOG_ERR_INVALID;

    char table_path[MAX_FILENAME];
    char schema_path[MAX_FILENAME];
    if(_catalog_table_paths(table_path, schema_path, db_name,
            table_name) != 0)
        return CATALOG_ERR_INVALID;

    if(fs_exists(_catalog_dup_path(table_path))
            || fs_exists(_catalog_dup_path(schema_path)))
        return CATALOG_ERR_EXISTS;

    /* Translate SQL-level columns into the physical table's key/format
     * lists. schema_validate() already proved these buffers are large
     * enough and that one record fits in a data page. */
    const char *key_name[MAX_COLUMN];
    const char *fmt[MAX_COLUMN];
    char fmt_buf[MAX_COLUMN][MAX_FMT_LENGTH];
    for(uint32_t c = 0; c < schema->column_count; ++c) {
        key_name[c] = schema->columns[c].name;
        if(schema_format(&schema->columns[c], fmt_buf[c],
                sizeof(fmt_buf[c])) != 0)
            return CATALOG_ERR_INVALID;
        fmt[c] = fmt_buf[c];
    }

    table_t *table = NULL;
    table_status_t tstatus = table_create(&table, db_name, table_name,
            schema->column_count, key_name, fmt);
    switch(tstatus) {
    case TABLE_OK:
        break;
    case TABLE_ERR_EXISTS:
        return CATALOG_ERR_EXISTS;
    case TABLE_ERR_INVALID:
        return CATALOG_ERR_INVALID;
    case TABLE_ERR_IO:
        return CATALOG_ERR_IO;
    default:
        return CATALOG_ERR;
    }
    table_close(table);

    /* Keep logical metadata beside the physical table so table listing can
     * stay limited to the table/ directory. */
    int created = fs_create(_catalog_dup_path(schema_path));
    if(created != 0) {
        fs_remove(_catalog_dup_path(table_path));
        return created == 1 ? CATALOG_ERR_EXISTS : CATALOG_ERR_IO;
    }

    fs_datablock_t *block = fs_borrow(_catalog_dup_path(schema_path), 0,
            PAGE_SIZE);
    schema_status_t sstatus = schema_save(schema, block->ptr, PAGE_SIZE);
    fs_return(block);
    if(sstatus != SCHEMA_OK) {
        fs_remove(_catalog_dup_path(schema_path));
        fs_remove(_catalog_dup_path(table_path));
        return CATALOG_ERR_IO;
    }

    return CATALOG_OK;
}

catalog_status_t catalog_drop_table(catalog_t *this, const char *table_name) {
    if(this == NULL) return CATALOG_ERR_INVALID;

    const char *db_name = catalog_current_db(this);
    if(db_name == NULL) return CATALOG_ERR_NO_DB;
    if(!_catalog_name_ok(table_name)) return CATALOG_ERR_INVALID;

    char table_path[MAX_FILENAME];
    char schema_path[MAX_FILENAME];
    if(_catalog_table_paths(table_path, schema_path, db_name,
            table_name) != 0)
        return CATALOG_ERR_INVALID;

    bool table_exists = fs_exists(_catalog_dup_path(table_path));
    bool schema_exists = fs_exists(_catalog_dup_path(schema_path));
    if(!table_exists && !schema_exists) return CATALOG_ERR_NOT_FOUND;

    if(table_exists && fs_remove(_catalog_dup_path(table_path)) != 0)
        return CATALOG_ERR_IO;
    if(schema_exists && fs_remove(_catalog_dup_path(schema_path)) != 0)
        return CATALOG_ERR_IO;

    return CATALOG_OK;
}

catalog_status_t catalog_get_schema(catalog_t *this, const char *table_name,
        schema_t **schema) {
    if(this == NULL || schema == NULL) return CATALOG_ERR_INVALID;
    *schema = NULL;

    const char *db_name = catalog_current_db(this);
    if(db_name == NULL) return CATALOG_ERR_NO_DB;
    if(!_catalog_name_ok(table_name)) return CATALOG_ERR_INVALID;

    char table_path[MAX_FILENAME];
    char schema_path[MAX_FILENAME];
    if(_catalog_table_paths(table_path, schema_path, db_name,
            table_name) != 0)
        return CATALOG_ERR_INVALID;

    if(!fs_exists(_catalog_dup_path(table_path))) {
        return CATALOG_ERR_NOT_FOUND;
    }
    if(!fs_exists(_catalog_dup_path(schema_path))) {
        return CATALOG_ERR_NOT_FOUND;
    }

    fs_datablock_t *block = fs_borrow(_catalog_dup_path(schema_path), 0,
            PAGE_SIZE);
    schema_status_t sstatus = schema_load(schema, block->ptr, PAGE_SIZE);
    fs_return(block);

    if(sstatus != SCHEMA_OK) {
        *schema = NULL;
        return CATALOG_ERR_INVALID;
    }
    return CATALOG_OK;
}