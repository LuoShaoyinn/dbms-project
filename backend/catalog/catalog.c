/* catalog.c
 *      Database catalog
 */

#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "constant.h"
#include "fs/fs.h"
#include "catalog.h"
#include "catalog_internal.h"

#define CATALOG_DB_ROOT     BASE_DIR DB_PREFIX

struct _s_catalog {
    char *current_db;           /* owned name, NULL when nothing is selected */
};

static bool _catalog_is_dot(const char *name) {
    return strcmp(name, ".") == 0 || strcmp(name, "..") == 0;
}

bool _catalog_name_ok(const char *name) {
    if(name == NULL) return false;

    size_t len = strlen(name);
    if(len == 0 || len >= MAX_DB_NAME_LENGTH) return false;

    unsigned char first = (unsigned char)name[0];
    if(!isalpha(first) && first != '_') return false;
    for(size_t i = 1; i < len; ++i) {
        unsigned char c = (unsigned char)name[i];
        if(!isalnum(c) && c != '_') return false;
    }
    return true;
}

static int _catalog_db_path(char *out, const char *db_name) {
    int n = snprintf(out, MAX_FILENAME, CATALOG_DB_ROOT "%s", db_name);
    return (n < 0 || (size_t)n >= MAX_FILENAME) ? -1 : 0;
}

int _catalog_sub_path(char *out, const char *db_name, const char *sub) {
    int n = snprintf(out, MAX_FILENAME, CATALOG_DB_ROOT "%s/%s",
            db_name, sub);
    return (n < 0 || (size_t)n >= MAX_FILENAME) ? -1 : 0;
}

int _catalog_child_path(char *out, const char *dir, const char *name) {
    size_t len = strlen(dir);
    int n;
    if(len > 0 && dir[len - 1] == '/')
        n = snprintf(out, MAX_FILENAME, "%s%s", dir, name);
    else
        n = snprintf(out, MAX_FILENAME, "%s/%s", dir, name);
    return (n < 0 || (size_t)n >= MAX_FILENAME) ? -1 : 0;
}

static int _catalog_paths(char *db_path, char *table_path, char *schema_path,
        char *index_path, const char *db_name) {
    if(_catalog_db_path(db_path, db_name) != 0) return -1;
    if(_catalog_sub_path(table_path, db_name, TABLE_PREFIX) != 0) return -1;
    if(_catalog_sub_path(schema_path, db_name, SCHEMA_PREFIX) != 0) return -1;
    if(_catalog_sub_path(index_path, db_name, INDEX_PREFIX) != 0) return -1;
    return 0;
}

static catalog_status_t _catalog_require_dir(const char *path) {
    struct stat st;
    if(stat(path, &st) != 0)
        return errno == ENOENT ? CATALOG_ERR_NOT_FOUND : CATALOG_ERR_IO;
    return S_ISDIR(st.st_mode) ? CATALOG_OK : CATALOG_ERR_NOT_FOUND;
}

char* _catalog_dup_path(const char *path) {
    size_t len = strlen(path) + 1;
    char *copy = (char*)malloc(len);
    if(copy == NULL) {
        perror("catalog: malloc");
        exit(-1);
    }
    memcpy(copy, path, len);
    return copy;
}

static int _catalog_remove_file(const char *path) {
    return fs_remove(_catalog_dup_path(path));
}

static int _catalog_rmdir(const char *path) {
    if(rmdir(path) == 0) return 0;
    return errno == ENOENT ? 0 : -1;
}

catalog_status_t catalog_init(catalog_t **this) {
    if(this == NULL) return CATALOG_ERR_INVALID;
    *this = NULL;

    catalog_t *ctx = (catalog_t*)calloc(1, sizeof(catalog_t));
    if(ctx == NULL) return CATALOG_ERR;

    *this = ctx;
    return CATALOG_OK;
}

void catalog_destroy(catalog_t *this) {
    if(this == NULL) return;
    free(this->current_db);
    free(this);
}

const char* catalog_current_db(const catalog_t *this) {
    return this == NULL ? NULL : this->current_db;
}

catalog_status_t catalog_create_db(catalog_t *this, const char *db_name) {
    if(this == NULL || !_catalog_name_ok(db_name))
        return CATALOG_ERR_INVALID;

    char db_path[MAX_FILENAME];
    char table_path[MAX_FILENAME];
    char schema_path[MAX_FILENAME];
    char index_path[MAX_FILENAME];
    if(_catalog_paths(db_path, table_path, schema_path, index_path,
            db_name) != 0)
        return CATALOG_ERR_INVALID;

    struct stat st;
    if(stat(db_path, &st) == 0) return CATALOG_ERR_EXISTS;
    if(errno != ENOENT) return CATALOG_ERR_IO;

    if(mkdir(db_path, 0755) != 0) {
        if(errno == EEXIST) return CATALOG_ERR_EXISTS;
        return CATALOG_ERR_IO;
    }

    /* Remove only the directories created by this call. rmdir() also refuses
     * to delete a non-empty directory, so pre-existing contents are safe. */
    if(mkdir(table_path, 0755) != 0) {
        int err = errno;
        rmdir(db_path);
        return err == EEXIST ? CATALOG_ERR_EXISTS : CATALOG_ERR_IO;
    }

    if(mkdir(index_path, 0755) != 0) {
        int err = errno;
        rmdir(table_path);
        rmdir(db_path);
        return err == EEXIST ? CATALOG_ERR_EXISTS : CATALOG_ERR_IO;
    }

    if(mkdir(schema_path, 0755) != 0) {
        int err = errno;
        rmdir(index_path);
        rmdir(table_path);
        rmdir(db_path);
        return err == EEXIST ? CATALOG_ERR_EXISTS : CATALOG_ERR_IO;
    }

    return CATALOG_OK;
}

catalog_status_t catalog_use_db(catalog_t *this, const char *db_name) {
    if(this == NULL || !_catalog_name_ok(db_name))
        return CATALOG_ERR_INVALID;

    char db_path[MAX_FILENAME];
    char table_path[MAX_FILENAME];
    char schema_path[MAX_FILENAME];
    char index_path[MAX_FILENAME];
    if(_catalog_paths(db_path, table_path, schema_path, index_path,
            db_name) != 0)
        return CATALOG_ERR_INVALID;

    /* Validate completely before touching the current selection. */
    catalog_status_t status = _catalog_require_dir(db_path);
    if(status == CATALOG_OK) status = _catalog_require_dir(table_path);
    if(status == CATALOG_OK) status = _catalog_require_dir(schema_path);
    if(status == CATALOG_OK) status = _catalog_require_dir(index_path);
    if(status != CATALOG_OK) return status;

    size_t len = strlen(db_name) + 1;
    char *copy = (char*)malloc(len);
    if(copy == NULL) return CATALOG_ERR;
    memcpy(copy, db_name, len);

    free(this->current_db);
    this->current_db = copy;
    return CATALOG_OK;
}

/* Remove the regular files directly inside one database subdirectory. Nested
 * directories are not traversed; the on-disk layout is assumed valid. */
static catalog_status_t _catalog_remove_dir_files(const char *path) {
    DIR *dir = opendir(path);
    if(dir == NULL)
        return errno == ENOENT ? CATALOG_OK : CATALOG_ERR_IO;

    catalog_status_t status = CATALOG_OK;
    for(;;) {
        errno = 0;
        struct dirent *entry = readdir(dir);
        if(entry == NULL) {
            if(errno != 0) status = CATALOG_ERR_IO;
            break;
        }
        if(_catalog_is_dot(entry->d_name)) continue;

        char child[MAX_FILENAME];
        if(_catalog_child_path(child, path, entry->d_name) != 0) {
            status = CATALOG_ERR_INVALID;
            break;
        }

        struct stat st;
        if(stat(child, &st) != 0) {
            if(errno == ENOENT) continue;
            status = CATALOG_ERR_IO;
            break;
        }
        if(S_ISDIR(st.st_mode)) continue;

        if(_catalog_remove_file(child) != 0) {
            status = CATALOG_ERR_IO;
            break;
        }
    }

    if(closedir(dir) != 0 && status == CATALOG_OK)
        status = CATALOG_ERR_IO;
    return status;
}

catalog_status_t catalog_drop_db(catalog_t *this, const char *db_name) {
    if(this == NULL || !_catalog_name_ok(db_name))
        return CATALOG_ERR_INVALID;

    char db_path[MAX_FILENAME];
    char table_path[MAX_FILENAME];
    char schema_path[MAX_FILENAME];
    char index_path[MAX_FILENAME];
    if(_catalog_paths(db_path, table_path, schema_path, index_path,
            db_name) != 0)
        return CATALOG_ERR_INVALID;

    catalog_status_t status = _catalog_require_dir(db_path);
    if(status != CATALOG_OK) return status;

    status = _catalog_remove_dir_files(table_path);
    if(status == CATALOG_OK)
        status = _catalog_remove_dir_files(schema_path);
    if(status == CATALOG_OK)
        status = _catalog_remove_dir_files(index_path);
    if(status != CATALOG_OK) return status;

    if(_catalog_rmdir(table_path) != 0) return CATALOG_ERR_IO;
    if(_catalog_rmdir(schema_path) != 0) return CATALOG_ERR_IO;
    if(_catalog_rmdir(index_path) != 0) return CATALOG_ERR_IO;
    if(_catalog_rmdir(db_path) != 0) return CATALOG_ERR_IO;

    if(this->current_db != NULL
            && strcmp(this->current_db, db_name) == 0) {
        free(this->current_db);
        this->current_db = NULL;
    }
    return CATALOG_OK;
}

/* Visit direct entries of one directory. want_dir selects directories,
 * otherwise regular files. */
static catalog_status_t _catalog_list_dir(const char *path,
        const bool want_dir, catalog_name_cb cb, void *ctx) {
    DIR *dir = opendir(path);
    if(dir == NULL)
        return errno == ENOENT ? CATALOG_OK : CATALOG_ERR_IO;

    catalog_status_t status = CATALOG_OK;
    for(;;) {
        errno = 0;
        struct dirent *entry = readdir(dir);
        if(entry == NULL) {
            if(errno != 0) status = CATALOG_ERR_IO;
            break;
        }
        if(_catalog_is_dot(entry->d_name)) continue;

        char child[MAX_FILENAME];
        if(_catalog_child_path(child, path, entry->d_name) != 0) {
            status = CATALOG_ERR_INVALID;
            break;
        }

        struct stat st;
        if(stat(child, &st) != 0) {
            if(errno == ENOENT) continue;
            status = CATALOG_ERR_IO;
            break;
        }

        bool match = want_dir ? S_ISDIR(st.st_mode) : S_ISREG(st.st_mode);
        if(!match) continue;

        if(cb != NULL && cb(entry->d_name, ctx) != 0) {
            status = CATALOG_ERR;
            break;
        }
    }

    if(closedir(dir) != 0 && status == CATALOG_OK)
        status = CATALOG_ERR_IO;
    return status;
}

catalog_status_t catalog_list_dbs(catalog_t *this, catalog_name_cb cb,
        void *ctx) {
    if(this == NULL) return CATALOG_ERR_INVALID;
    return _catalog_list_dir(CATALOG_DB_ROOT, true, cb, ctx);
}

catalog_status_t catalog_list_tables(catalog_t *this, catalog_name_cb cb,
        void *ctx) {
    if(this == NULL) return CATALOG_ERR_INVALID;
    if(this->current_db == NULL) return CATALOG_ERR_NO_DB;

    char table_path[MAX_FILENAME];
    if(_catalog_sub_path(table_path, this->current_db, TABLE_PREFIX) != 0)
        return CATALOG_ERR_INVALID;
    return _catalog_list_dir(table_path, false, cb, ctx);
}