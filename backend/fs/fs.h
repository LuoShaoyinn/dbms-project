/* fs.h
 *      File manager
 */

#ifndef _H_FS
#define _H_FS

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "constant.h"
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

typedef struct __fs_page_s {
    void* ptr;                  /* always point to the mapped page mem */
    int fd;                     /* must use kernel style file descriptor */
    uint32_t ref_cnt;
    bool is_in_lru;
    uint64_t file_id;           /* identity of the mapped file lifetime */
    lru_node_t *ref_lru_node;
    list_node_t *ref_list_node; /* live page registry */
} _fs_page_t;

typedef struct _fs_datablock_s {
    void* ptr;                  /* this ptr points to the offset */
    _fs_page_t *_ref_fs_page;    /* a pointer to the internal datastructure */
} fs_datablock_t;

typedef char* fs_tmp_string;    /* will be auto free */

void fs_init();
void fs_destroy();
fs_tmp_string fs_name_db_table(const char* db_name, const char* table_name);
fs_tmp_string fs_name_db_index(const char* db_name, const char* index_name);
fs_datablock_t* fs_borrow(fs_tmp_string filename, \
        const size_t offset, const size_t sz);
void fs_return(fs_datablock_t *datablock);

/* fs_create:
 *      create a new file exclusively, without touching an existing one.
 *      Returns 0 on success, 1 when the file already exists, -1 on any other
 *      open failure. The filename is consumed in every case. */
int fs_create(fs_tmp_string filename);

/* fs_exists:
 *      probe a path with open(2); never creates the file.
 *      The filename is consumed. */
bool fs_exists(fs_tmp_string filename);

/* fs_remove:
 *      unlink a file and drop its name -> file id mapping.
 *      Cached pages stay valid because the LRU is keyed by file id; a later
 *      file with the same name gets a new id.
 *      Returns 0 when the file is absent afterwards, -1 on unlink failure.
 *      The filename is consumed in every case. */
int fs_remove(fs_tmp_string path);

#endif // _H_FS
