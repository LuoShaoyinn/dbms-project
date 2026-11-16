/* constant.h
 *      Define limitations
 */

#ifndef _H_CONSTANT
#define _H_CONSTANT

/* CI data bounds: https://thu-db.github.io/dbs-tutorial/faq/
 * Name buffer capacities include the terminating NUL. */
#define MAX_COLUMN              10
#define MAX_TABLE               8
#define MAX_NAME_LENGTH         32
#define MAX_KEY_NAME_LENGTH     (MAX_NAME_LENGTH + 1)
#define MAX_TABLE_NAME_LENGTH   (MAX_NAME_LENGTH + 1)
/* Constraint names: chapter-0/0-3-frontend/#desc, at most 32 characters. */
#define MAX_CONSTRAINT_NAME_LENGTH (MAX_NAME_LENGTH + 1)
#define MAX_VARCHAR_LENGTH      2048
/* Sum of column payload widths, excluding our NULL bitmap:
 * https://thu-db.github.io/dbs-tutorial/chapter-2/intro/ */
#define MAX_RECORD_PAYLOAD      2048

/* Implementation choices, not additional limits imposed by the PA. */
#define MAX_DB_NAME_LENGTH      64

#define MAX_FILENAME            256
#define PAGE_SIZE               8192
#define FS_LRU_MAXSIZE          256

#define TABLE_SLOT_MAX          64

#define BASE_DIR                "./data/"
#define DB_PREFIX               "db/"
#define TABLE_PREFIX            "table/"
#define INDEX_PREFIX            "index/"

#endif // _H_CONSTANT
