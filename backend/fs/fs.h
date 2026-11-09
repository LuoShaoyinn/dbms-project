/* fs.h
 *      File manager
 */

#ifndef _H_FS
#define _H_FS

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "utils/lru/lru.h"

#ifdef _WIN32
#error "Windows isn't supported!"
#endif

/*  dbms.elf
 *  data/
 *      sys/
 *          <reserved>
 *      db/
 *          <db_name>/
 *              table/
 *                  <table_name>
 *              index/
 *                  <btree>
 *
 */

#define BASE_DIR        "./data/"
#define DB_PREFIX       "db/"
#define TABLE_PREFIX    "table/"
#define INDEX_PREFIX    "index/"

typedef struct __s_fs_page {
    void* ptr;                  /* always point to the mapped page mem */
    int fd;                     /* must use kernel style file descriptor */
    uint32_t ref_cnt;
    bool is_in_lru;
    lru_node_t *ref_lru_node;
} _fs_page_t;

typedef struct _s_fs_datablock {
    void* ptr;                  /* this ptr points to the offset */
    _fs_page_t *_ref_fs_page;    /* a pointer to the internal datastructure */
} fs_datablock_t;

typedef char* fs_tmp_string;    /* will be auto free */

void fs_init();
void fs_destroy();
fs_tmp_string fs_name_db_table(const char* db_name, const char* table_name);
fs_tmp_string fs_name_db_index(const char* db_name, const char* index_name);
fs_datablock_t* fs_borrow(fs_tmp_string filename, const size_t offset, const size_t sz);
void fs_return(fs_datablock_t *datablock);
void fs_remove(fs_tmp_string path);

#endif // _H_FS
