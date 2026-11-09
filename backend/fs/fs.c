/* fs.c
 *      File manager
 */

#define _POSIX_C_SOURCE 200809L

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "utils/hash_table/hash_table.h"
#include "fs.h"


#define MAX_FILENAME    256
#define PAGE_SIZE       4096
#define FS_LRU_MAXSIZE  256

static lru_t        fs_lru;             /* main lru structure */

typedef struct __fs_fp_s { 
    char filename[MAX_FILENAME];
    uint32_t page_id;
} fs_fp_t;                              /* keys for (file, pageid) */


static void _fs_free_page(_fs_page_t* p_fs_page) {
    if(p_fs_page == NULL) return;
    p_fs_page->is_in_lru = false;
    munmap(p_fs_page->ptr, PAGE_SIZE);
    close(p_fs_page->fd);
    free(p_fs_page);
}

static bool _fs_try_unmap_page(_fs_page_t* p_fs_page) {
    if(p_fs_page->is_in_lru) return false;  /* still cached */
    if(p_fs_page->ref_cnt != 0) return false; /* still borrowed */
    _fs_free_page(p_fs_page);
    return true;
}

void fs_init() {
    size_t sys_page_size = sysconf(_SC_PAGE_SIZE);
    if(sys_page_size == 0 || PAGE_SIZE % sys_page_size) {
        perror("PAGE_SIZE must be multiple of sys_page_size");
        exit(-1);
    }
    mkdir(BASE_DIR, 0755);
    mkdir(BASE_DIR DB_PREFIX, 0755);
    lru_init(&fs_lru, FS_LRU_MAXSIZE);
}

void fs_destroy() {
    /* The LRU only owns its nodes; pages are owned by the file manager. */
    for(list_node_t *nd = fs_lru.head->nxt; nd != fs_lru.head; nd = nd->nxt) {
        lru_node_t *p_lru_node = (lru_node_t*)nd->ptr;
        _fs_free_page((_fs_page_t*)p_lru_node->ptr);
        p_lru_node->ptr = NULL;
    }
    lru_destroy(&fs_lru);
}

static _fs_page_t* _fs_map_page(const char* filename, const size_t page_id) {
    int fd = open(filename, O_CREAT | O_RDWR, 0644);
    if(fd == -1) { perror("Can not open file"); exit(-1); }
    struct stat st;
    off_t file_size = (off_t)(PAGE_SIZE * (page_id + 1));
    if(fstat(fd, &st) == -1) {
        perror("Can not stat file");
        close(fd);
        exit(-1);
    }
    if(st.st_size < file_size && ftruncate(fd, file_size) == -1) {
        perror("Can not resize file");
        close(fd);
        exit(-1);
    }
    _fs_page_t* p_fs_page = (_fs_page_t*)malloc(sizeof(_fs_page_t));
    p_fs_page->fd = fd;
    p_fs_page->ptr = mmap(NULL, PAGE_SIZE, 
            PROT_WRITE | PROT_READ, MAP_SHARED, 
            fd, PAGE_SIZE * page_id);
    if(p_fs_page->ptr == MAP_FAILED) {
        perror("mmap() failed");
        free(p_fs_page);
        close(fd);
        exit(-1);
    }
    return p_fs_page;
}

fs_datablock_t* fs_borrow(fs_tmp_string filename, 
        const size_t offset, const size_t sz) {
    /* Location */
    size_t page_id = offset / PAGE_SIZE;
    size_t offset_in_page = offset - page_id * PAGE_SIZE;
    if(offset_in_page + sz > PAGE_SIZE) {
        perror("Cross page access isn't supported");
        exit(-1);
    }

    /* Create Key */
    fs_fp_t key; 
    memset(&key, 0, sizeof(key));
    strncpy(key.filename, filename, MAX_FILENAME - 1);
    key.page_id = (uint32_t)page_id;

    /* Access Datastructure */
    lru_node_t *p_new_lru_node = NULL;
    _fs_page_t *p_old_fs_page = NULL;
    lru_node_t *p_lru_node = lru_access(&fs_lru, &key, sizeof(key),
            (void**)&p_new_lru_node, (void**)&p_old_fs_page);
    _fs_page_t* p_fs_page = NULL; 
    if(p_new_lru_node == NULL) { /* already in the lru_access */
        p_fs_page = (_fs_page_t*)p_lru_node->ptr;
        ++(p_fs_page->ref_cnt);
    }
    else {  
        p_fs_page = _fs_map_page(filename, page_id);
        p_fs_page->is_in_lru = true;
        p_fs_page->ref_cnt = 1;
        p_fs_page->ref_lru_node = p_lru_node;
        p_lru_node->ptr = p_fs_page;
        if(p_old_fs_page != NULL) { /* Need free */
            p_old_fs_page->is_in_lru = false;
            _fs_try_unmap_page(p_old_fs_page);
        }
    } 
    free(filename);
    
    /* Construct handle to return */
    fs_datablock_t *p_ret_handle = (fs_datablock_t*)malloc(sizeof(fs_datablock_t));
    p_ret_handle->ptr = (char*)p_fs_page->ptr + offset_in_page;
    p_ret_handle->_ref_fs_page = p_fs_page;
    return p_ret_handle;
}

void fs_return(fs_datablock_t *p_datablock) {
    _fs_page_t *p_fs_page = p_datablock->_ref_fs_page;
    if(p_fs_page->ref_cnt > 0) --(p_fs_page->ref_cnt);
    _fs_try_unmap_page(p_fs_page);
    free(p_datablock);
}

void fs_remove(fs_tmp_string filename) { remove(filename); free(filename); }

fs_tmp_string fs_name_db_table(const char* db_name, const char* table_name) {
    char* ret = (char*)malloc(sizeof(char) * MAX_FILENAME);
    snprintf(ret, MAX_FILENAME, BASE_DIR DB_PREFIX "%s/" TABLE_PREFIX "%s", db_name, table_name);
    return ret;
}
fs_tmp_string fs_name_db_index(const char* db_name, const char* index_name) {
    char* ret = (char*)malloc(sizeof(char) * MAX_FILENAME);
    snprintf(ret, MAX_FILENAME, BASE_DIR DB_PREFIX "%s/" INDEX_PREFIX "%s", db_name, index_name);
    return ret;
}