/* fs.h
 *      File manager
 */

#ifndef _H_FS
#define _H_FS

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "constant.h"
#include "utils/list/list.h"

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
 *                  <table_name>     (page 0 is the table header)
 *              index/
 *                  <btree>
 *
 */

typedef struct __fs_page_s {
    void* ptr;                  /* always point to the mapped page mem */
    int fd;                     /* must use kernel style file descriptor */
    uint32_t ref_cnt;
    bool is_in_lru;
    list_node_t *ref_list_node; /* live page registry */
} _fs_page_t;

typedef struct _fs_datablock_s {
    void* ptr;                  /* this ptr points to the offset */
    _fs_page_t *_ref_fs_page;    /* a pointer to the internal datastructure */
} fs_datablock_t;

typedef char* fs_tmp_string;    /* will be auto free */

void fs_init();
void fs_destroy();
fs_tmp_string fs_name_db(const char *db_name); /* "" names the database root */
/* Empty table/index names produce directory paths ending in '/'. */
fs_tmp_string fs_name_db_table(const char* db_name, const char* table_name);
fs_tmp_string fs_name_db_index(const char* db_name, const char* index_name);
fs_datablock_t* fs_borrow(fs_tmp_string filename, \
        const size_t offset, const size_t sz);
void fs_return(fs_datablock_t *datablock);

/* fs_create:
 *      create a new file exclusively, without touching an existing one.
 *      Returns 0 on success, -1 on failure (including an existing file).
 *      The filename is consumed in every case. */
int fs_create(fs_tmp_string filename);

/* fs_exists:
 *      probe a path with open(2); never creates the file.
 *      The filename is consumed. */
bool fs_exists(fs_tmp_string filename);

/* fs_remove:
 *      unlink a file and drop its name -> file id mapping.
 *      Cached pages stay valid because the LRU is keyed by file id; a later
 *      file with the same name gets a new id.
 *      Returns 0 on successful unlink, -1 on failure (including absence).
 *      The filename is consumed in every case. */
int fs_remove(fs_tmp_string path);

/* Directory operations consume their paths. Names/layout are trusted. */
int fs_mkdir(fs_tmp_string path);
int fs_rmdir(fs_tmp_string path);
/* Directory path ends in '/'. Fill names and reset *count to the number
 * written. The caller guarantees enough rows. Select directories or files. */
int fs_list(fs_tmp_string path, bool directories,
        char names[][MAX_DB_NAME_LENGTH], size_t *count);
int fs_clear_dir(fs_tmp_string path); /* flat file directory, no nested traversal */

#endif // _H_FS
