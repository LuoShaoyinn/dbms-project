/* catalog.h
 *      Stateless filesystem-backed database/table management.
 *
 * A database is data/db/<name>/, containing table/ and index/. Page 0 of
 * each table file holds its columns and defaults. There is no catalog
 * object, registry, or selected database here: the caller owns session state
 * and passes db_name explicitly. Inputs are trusted.
 *
 * Status operations return 0 or -1. Return every record and close its table
 * before dropping a table/database. All operations require fs_init().
 */
#ifndef _H_CATALOG
#define _H_CATALOG

#include <stdbool.h>
#include "column/column.h"
#include "fs/fs.h"

bool catalog_db_exists(const char *db_name);
int catalog_create_db(const char *db_name);
int catalog_drop_db(const char *db_name);

/* Existing files are left untouched on duplicate creation. Defaults are
 * textual values; NULL means no default, "" is an empty string default. */
int catalog_create_table(const char *db_name, const char *table_name,
        uint32_t m, const column_t *columns, const char *const *defaults);
int catalog_drop_table(const char *db_name, const char *table_name);

/* The caller supplies enough name rows. *count is reset on every call.
 * A missing database is an error, not an empty table list. */
int catalog_list_dbs(char names[][MAX_DB_NAME_LENGTH], size_t *count);
int catalog_list_tables(const char *db_name,
        char names[][MAX_DB_NAME_LENGTH], size_t *count);

/* DESC needs no catalog wrapper: table_open() and table_get_header() expose
 * the mapped column metadata. */
#endif // _H_CATALOG
