/* fs.c
 *      File manager
 */

#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "constant.h"
#include "utils/hash_table/hash_table.h"
#include "utils/list/list.h"
#include "utils/lru/lru.h"
#include "fs.h"


static lru_t        fs_lru;             /* main lru structure */
static list_node_t *fs_page_head;       /* registry of all live pages */
static hash_table_t fs_file_table;      /* filename -> file id */
static list_node_t *fs_file_head;       /* registry of resolved files */
static uint64_t     fs_file_counter;    /* next file id */

typedef struct __fs_file_s {
    uint64_t id;
    list_node_t *ref_list_node;
} fs_file_t;

typedef struct __fs_fp_s { 
    uint64_t file_id;
    uint32_t page_id;
} fs_fp_t;                              /* keys for (file id, pageid) */


static fs_file_t* _fs_file_query(const char *filename) {
    hash_table_elem_t *elem = hash_table_query(&fs_file_table,
            filename, strlen(filename) + 1);
    return elem == NULL ? NULL : (fs_file_t*)elem->ptr;
}

static fs_file_t* _fs_file_insert(const char *filename) {
    fs_file_t *file = (fs_file_t*)malloc(sizeof(fs_file_t));
    file->id = ++fs_file_counter;
    file->ref_list_node = list_node_insert(fs_file_head, file);
    hash_table_insert(&fs_file_table, filename, strlen(filename) + 1, file);
    return file;
}

static void _fs_file_remove(const char *filename) {
    hash_table_elem_t *elem = hash_table_query(&fs_file_table,
            filename, strlen(filename) + 1);
    if(elem == NULL) return;
    fs_file_t *file = (fs_file_t*)elem->ptr;
    hash_table_remove(&fs_file_table, elem);
    list_node_remove(file->ref_list_node);
    free(file);
}

static fs_file_t* _fs_file_resolve(const char *filename) {
    fs_file_t *file = _fs_file_query(filename);
    if(file != NULL) return file;

    /* Absent only means "unresolved". A new mapping gets a fresh id; the
     * mapping table and the page cache share the same process lifetime. */
    return _fs_file_insert(filename);
}


static void _fs_free_page(_fs_page_t* p_fs_page) {
    p_fs_page->is_in_lru = false;
    list_node_remove(p_fs_page->ref_list_node);
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
    fs_file_counter = 0;
    size_t sys_page_size = sysconf(_SC_PAGE_SIZE);
    if(sys_page_size == 0 || PAGE_SIZE % sys_page_size) {
        perror("PAGE_SIZE must be multiple of sys_page_size");
        exit(-1);
    }
    mkdir(BASE_DIR, 0755);
    mkdir(BASE_DIR DB_PREFIX, 0755);
    lru_init(&fs_lru, FS_LRU_MAXSIZE);
    hash_table_init(&fs_file_table);
    fs_page_head = (list_node_t*)malloc(sizeof(list_node_t));
    list_node_init(fs_page_head);
    fs_file_head = (list_node_t*)malloc(sizeof(list_node_t));
    list_node_init(fs_file_head);
}

void fs_destroy() {
    /* The LRU only owns its nodes; pages are owned by the file manager. */
    lru_destroy(&fs_lru);
    while(fs_page_head->nxt != fs_page_head)
        _fs_free_page((_fs_page_t*)fs_page_head->nxt->ptr);
    free(fs_page_head); fs_page_head = NULL;

    /* The hash table owns the keys; the list owns the file records. */
    hash_table_destroy(&fs_file_table);
    while(fs_file_head->nxt != fs_file_head) {
        fs_file_t *file = (fs_file_t*)fs_file_head->nxt->ptr;
        list_node_remove(file->ref_list_node);
        free(file);
    }
    free(fs_file_head); fs_file_head = NULL;
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
    p_fs_page->is_in_lru = false;
    p_fs_page->ref_cnt = 0;
    p_fs_page->ref_list_node = list_node_insert(fs_page_head, p_fs_page);
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

    /* Resolve the name to a file id, then key the cache by that id. */
    fs_file_t *file = _fs_file_resolve(filename);
    fs_fp_t key; 
    memset(&key, 0, sizeof(key));
    key.file_id = file->id;
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
        p_lru_node->ptr = p_fs_page;
        if(p_old_fs_page != NULL) { /* Need free */
            p_old_fs_page->is_in_lru = false;
            _fs_try_unmap_page(p_old_fs_page);
        }
    } 
    free(filename);
    
    /* Construct handle to return */
    fs_datablock_t *p_ret_handle = \
            (fs_datablock_t*)malloc(sizeof(fs_datablock_t));
    p_ret_handle->ptr = (char*)p_fs_page->ptr + offset_in_page;
    p_ret_handle->_ref_fs_page = p_fs_page;
    return p_ret_handle;
}

void fs_return(fs_datablock_t *p_datablock) {
    _fs_page_t *p_fs_page = p_datablock->_ref_fs_page;
    --p_fs_page->ref_cnt;
    _fs_try_unmap_page(p_fs_page);
    free(p_datablock);
}

int fs_create(fs_tmp_string filename) {
    int ret = 0;
    int fd = open(filename, O_CREAT | O_EXCL | O_RDWR, 0644);
    if(fd == -1) ret = -1;
    else close(fd);

    free(filename);
    return ret;
}

bool fs_exists(fs_tmp_string filename) {
    int fd = open(filename, O_RDONLY);
    bool found = fd >= 0;
    if(found) close(fd);

    free(filename);
    return found;
}

int fs_remove(fs_tmp_string filename) {
    /* Pages are keyed by file id, so the unlinked file's cached pages are
     * unreachable from a later file with the same name. */
    int ret = unlink(filename);
    if(ret == 0 || errno == ENOENT)
        _fs_file_remove(filename);

    free(filename);
    return ret;
}

static fs_tmp_string _fs_name(const char *fmt, ...) {
    char *path = (char*)malloc(MAX_FILENAME);
    va_list args;
    va_start(args, fmt);
    vsnprintf(path, MAX_FILENAME, fmt, args);
    va_end(args);
    return path;
}

fs_tmp_string fs_name_db(const char *db_name) {
    return _fs_name(BASE_DIR DB_PREFIX "%s", db_name);
}

fs_tmp_string fs_name_db_table(const char* db_name, const char* table_name) {
    return _fs_name(BASE_DIR DB_PREFIX "%s/" TABLE_PREFIX "%s", db_name, table_name);
}

fs_tmp_string fs_name_db_index(const char* db_name, const char* index_name) {
    return _fs_name(BASE_DIR DB_PREFIX "%s/" INDEX_PREFIX "%s", db_name, index_name);
}

int fs_mkdir(fs_tmp_string path) {
    int ret = mkdir(path, 0755);
    free(path);
    return ret;
}

int fs_rmdir(fs_tmp_string path) {
    int ret = rmdir(path);
    free(path);
    return ret;
}

int fs_list(fs_tmp_string path, bool directories,
        char names[][MAX_DB_NAME_LENGTH], size_t *count) {
    *count = 0;
    DIR *dir = opendir(path);
    if(dir == NULL) {
        free(path);
        return -1;
    }
    int ret = 0;
    for(;;) {
        errno = 0;
        struct dirent *entry = readdir(dir);
        if(entry == NULL) {
            if(errno != 0) ret = -1;
            break;
        }
        /* Database/table names contain no dots; skip directory bookkeeping. */
        if(entry->d_name[0] == '.') continue;

        fs_tmp_string child = _fs_name("%s%s", path, entry->d_name);
        struct stat st;
        int status = stat(child, &st);
        free(child);
        if(status != 0) {
            ret = -1;
            break;
        }
        bool match = directories ? S_ISDIR(st.st_mode) : S_ISREG(st.st_mode);
        if(match) strcpy(names[(*count)++], entry->d_name);
    }
    if(closedir(dir) != 0) ret = -1;
    free(path);
    return ret;
}

int fs_clear_dir(fs_tmp_string path) {
    DIR *dir = opendir(path);
    if(dir == NULL) {
        free(path);
        return -1;
    }
    int ret = 0;
    for(;;) {
        errno = 0;
        struct dirent *entry = readdir(dir);
        if(entry == NULL) {
            if(errno != 0) ret = -1;
            break;
        }
        if(entry->d_name[0] == '.') continue;
        if(fs_remove(_fs_name("%s%s", path, entry->d_name)) != 0) {
            ret = -1;
            break;
        }
    }
    if(closedir(dir) != 0) ret = -1;
    free(path);
    return ret;
}
