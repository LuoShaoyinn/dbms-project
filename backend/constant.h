/* constant.h
 *      Define limitations
 */

#ifndef _H_CONSTANT
#define _H_CONSTANT

#define MAX_COLUMN              8
#define MAX_KEY_NAME_LENGTH     32
#define MAX_FMT_LENGTH          32
#define MAX_ELEM_N              2048

#define MAX_FILENAME            256
#define PAGE_SIZE               8192
#define FS_LRU_MAXSIZE          256

#define TABLE_SLOT_MAX          64

#define BASE_DIR                "./data/"
#define DB_PREFIX               "db/"
#define TABLE_PREFIX            "table/"
#define INDEX_PREFIX            "index/"

#endif // _H_CONSTANT