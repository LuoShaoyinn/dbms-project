/* catalog.h
 *      Database catalog
 *
 * On-disk layout:
 *      data/db/<database>/
 *          table/      physical table files
 *          schema/     logical schema metadata, one file per table
 *          index/
 *
 * A catalog owns only the selected database name. Table handles and scans are
 * not registered here: callers must close every handle and scan that belongs
 * to a database before calling catalog_drop_db() for it.
 *
 * Returned strings:
 *      catalog_current_db() returns a borrowed pointer owned by the catalog.
 *      It stays valid until the next successful catalog_use_db() or until the
 *      selected database is dropped, or until catalog_destroy().
 *      Names passed to catalog_name_cb are borrowed for the duration of that
 *      callback only. Copy a name to keep it after the callback returns.
 */

#ifndef _H_CATALOG
#define _H_CATALOG

#include <stdbool.h>
#include <stddef.h>

#include "constant.h"
#include "schema.h"

/* Recoverable catalog statuses. Every negative value is an error. */
typedef enum _s_catalog_status {
    CATALOG_OK = 0,
    CATALOG_ERR = -1,
    CATALOG_ERR_EXISTS = -2,
    CATALOG_ERR_NOT_FOUND = -3,
    CATALOG_ERR_NO_DB = -4,
    CATALOG_ERR_INVALID = -5,
    CATALOG_ERR_IO = -6
} catalog_status_t;

typedef struct _s_catalog catalog_t;

/* catalog_name_cb:
 *      receives one borrowed name per entry. Return 0 to continue, or a
 *      non-zero value to stop the listing with CATALOG_ERR. */
typedef int (*catalog_name_cb)(const char *name, void *ctx);

/* catalog_init:
 *      create an empty catalog with no selected database. */
catalog_status_t catalog_init(catalog_t **this);
void catalog_destroy(catalog_t *this);

/* Database management. Names are NUL-terminated identifiers consisting of a
 * letter or underscore followed by letters, digits, or underscores.
 *
 * catalog_create_db:
 *      create data/db/<name>/, table/, and index/. A name that already exists
 *      yields CATALOG_ERR_EXISTS and leaves the existing contents untouched.
 * catalog_drop_db:
 *      remove every file under table/ and index/, then remove the database
 *      directories. Dropping the selected database clears the selection.
 *      Table handles and scans for this database must already be closed.
 * catalog_use_db:
 *      validate an existing database and make it current. The previous
 *      selection is kept when validation fails. */
catalog_status_t catalog_create_db(catalog_t *this, const char *db_name);
catalog_status_t catalog_drop_db(catalog_t *this, const char *db_name);
catalog_status_t catalog_use_db(catalog_t *this, const char *db_name);

/* Table management. Every operation acts on the selected database and
 * returns CATALOG_ERR_NO_DB when none is selected. Table names follow the
 * same identifier rules as database names.
 *
 * catalog_create_table:
 *      validate the logical schema, create the physical table, then persist
 *      its metadata under schema/<table_name>. A table that already exists
 *      yields CATALOG_ERR_EXISTS and leaves the existing table untouched.
 * catalog_drop_table:
 *      remove the physical table and its schema metadata. A missing table
 *      yields CATALOG_ERR_NOT_FOUND.
 * catalog_get_schema:
 *      load a table's logical schema for a frontend's DESC output. On success
 *      *schema is owned by the caller and must be released with
 *      schema_destroy(). A missing table yields CATALOG_ERR_NOT_FOUND.
 *
 * The backend never prints DESC output; callers format the returned schema. */
catalog_status_t catalog_create_table(catalog_t *this, const char *table_name,
        const schema_t *schema);
catalog_status_t catalog_drop_table(catalog_t *this, const char *table_name);
catalog_status_t catalog_get_schema(catalog_t *this, const char *table_name,
        schema_t **schema);

/* catalog_list_dbs:
 *      visit the database directories under data/db/.
 * catalog_list_tables:
 *      visit the regular files under the selected database's table/
 *      directory. Returns CATALOG_ERR_NO_DB when nothing is selected.
 *      A NULL callback validates the listing without visiting names. */
catalog_status_t catalog_list_dbs(catalog_t *this, catalog_name_cb cb,
        void *ctx);
catalog_status_t catalog_list_tables(catalog_t *this, catalog_name_cb cb,
        void *ctx);

/* catalog_current_db:
 *      return the borrowed selected name, or NULL when no database is
 *      selected. */
const char* catalog_current_db(const catalog_t *this);

#endif // _H_CATALOG