/* test_catalog.c
 *      isolated database catalog tests for backend/catalog
 */

#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "catalog/catalog.h"
#include "constant.h"
#include "fs/fs.h"
#include "table/table.h"

#define DB_ALPHA        "cat_alpha"
#define DB_BETA         "cat_beta"
#define DB_DUP          "cat_dup"
#define DB_MISSING      "cat_missing"
#define DB_USE          "cat_use"
#define DB_ABSENT       "cat_absent"
#define DB_LIST         "cat_list"
#define DB_DROP         "cat_dropdb"
#define DB_RECREATE     "cat_recreate"
#define DB_SELECTED     "cat_selected"
#define DB_OTHER        "cat_other"
#define DB_PERSIST      "cat_persist"

#define TEST_RECORD_SIZE    17      /* 1 NULL bitmap + int + s12 */
#define LIST_MAX            64
#define NAME_MAX            128

typedef struct _s_name_list {
    char name[LIST_MAX][NAME_MAX];
    size_t count;
} name_list_t;

static catalog_t *catalog;

static void db_path(char *out, size_t size, const char *db) {
    int n = snprintf(out, size, BASE_DIR DB_PREFIX "%s", db);
    assert(n > 0 && (size_t)n < size);
}

static void table_dir_path(char *out, size_t size, const char *db) {
    int n = snprintf(out, size, BASE_DIR DB_PREFIX "%s/" TABLE_PREFIX, db);
    assert(n > 0 && (size_t)n < size);
}

static bool path_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static bool path_is_dir(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static void drop_if_exists(const char *db) {
    catalog_status_t status = catalog_drop_db(catalog, db);
    assert(status == CATALOG_OK || status == CATALOG_ERR_NOT_FOUND);
}

static void cleanup_all(void) {
    drop_if_exists(DB_ALPHA);
    drop_if_exists(DB_BETA);
    drop_if_exists(DB_DUP);
    drop_if_exists(DB_MISSING);
    drop_if_exists(DB_USE);
    drop_if_exists(DB_ABSENT);
    drop_if_exists(DB_LIST);
    drop_if_exists(DB_DROP);
    drop_if_exists(DB_RECREATE);
    drop_if_exists(DB_SELECTED);
    drop_if_exists(DB_OTHER);
    drop_if_exists(DB_PERSIST);
}

static void list_reset(name_list_t *list) {
    list->count = 0;
}

static int list_collect(const char *name, void *ctx) {
    name_list_t *list = (name_list_t*)ctx;
    assert(list->count < LIST_MAX);
    size_t len = strlen(name);
    assert(len < NAME_MAX);
    memcpy(list->name[list->count], name, len + 1);
    ++list->count;
    return 0;
}

static bool list_contains(const name_list_t *list, const char *name) {
    for(size_t i = 0; i < list->count; ++i)
        if(strcmp(list->name[i], name) == 0) return true;
    return false;
}

static void record_fill(unsigned char *record, const int id) {
    memset(record, 0, TEST_RECORD_SIZE);
    memcpy(record + 1, &id, sizeof(id));
}

static int record_id(const void *record) {
    int id;
    memcpy(&id, (const char*)record + 1, sizeof(id));
    return id;
}

static uint32_t insert_id(table_t *this, const int id) {
    unsigned char record[TEST_RECORD_SIZE];
    uint32_t rid = UINT32_MAX;
    record_fill(record, id);
    assert(table_insert(this, record, sizeof(record), &rid) == TABLE_OK);
    return rid;
}

static void create_record_table(table_t **this, const char *db,
        const char *name) {
    static const char *key_name[] = {"id", "payload"};
    static const char *fmt[] = {"i", "s12"};
    assert(table_create(this, db, name, 2, key_name, fmt) == TABLE_OK);
}

static void test_initially_no_db(void) {
    assert(catalog_current_db(catalog) == NULL);

    name_list_t list;
    list_reset(&list);
    assert(catalog_list_tables(catalog, list_collect, &list)
            == CATALOG_ERR_NO_DB);
    assert(list.count == 0);
}

static void test_invalid_names(void) {
    char too_long[MAX_DB_NAME_LENGTH + 1];
    memset(too_long, 'a', sizeof(too_long) - 1);
    too_long[sizeof(too_long) - 1] = '\0';

    assert(catalog_create_db(catalog, NULL) == CATALOG_ERR_INVALID);
    assert(catalog_create_db(catalog, "") == CATALOG_ERR_INVALID);
    assert(catalog_create_db(catalog, "9starts") == CATALOG_ERR_INVALID);
    assert(catalog_create_db(catalog, "bad/name") == CATALOG_ERR_INVALID);
    assert(catalog_create_db(catalog, "bad name") == CATALOG_ERR_INVALID);
    assert(catalog_create_db(catalog, too_long) == CATALOG_ERR_INVALID);
    assert(catalog_use_db(catalog, "bad/name") == CATALOG_ERR_INVALID);
    assert(catalog_drop_db(catalog, "bad/name") == CATALOG_ERR_INVALID);
}

static void test_create_list_use(void) {
    char path[MAX_FILENAME];
    db_path(path, sizeof(path), DB_ALPHA);
    assert(!path_exists(path));
    db_path(path, sizeof(path), DB_BETA);
    assert(!path_exists(path));

    assert(catalog_create_db(catalog, DB_ALPHA) == CATALOG_OK);
    assert(catalog_create_db(catalog, DB_BETA) == CATALOG_OK);

    db_path(path, sizeof(path), DB_ALPHA);
    assert(path_is_dir(path));
    table_dir_path(path, sizeof(path), DB_ALPHA);
    assert(path_is_dir(path));
    snprintf(path, sizeof(path), BASE_DIR DB_PREFIX "%s/" INDEX_PREFIX,
            DB_ALPHA);
    assert(path_is_dir(path));

    name_list_t list;
    list_reset(&list);
    assert(catalog_list_dbs(catalog, list_collect, &list) == CATALOG_OK);
    assert(list_contains(&list, DB_ALPHA));
    assert(list_contains(&list, DB_BETA));

    assert(catalog_use_db(catalog, DB_ALPHA) == CATALOG_OK);
    assert(strcmp(catalog_current_db(catalog), DB_ALPHA) == 0);
    assert(catalog_use_db(catalog, DB_BETA) == CATALOG_OK);
    assert(strcmp(catalog_current_db(catalog), DB_BETA) == 0);

    drop_if_exists(DB_ALPHA);
    assert(strcmp(catalog_current_db(catalog), DB_BETA) == 0);
    drop_if_exists(DB_BETA);
    assert(catalog_current_db(catalog) == NULL);
}

static void check_record(table_t *this, const uint32_t rid, const int id) {
    void *record = NULL;
    size_t size = 0;
    assert(table_get(this, rid, &record, &size) == TABLE_OK);
    assert(size == TEST_RECORD_SIZE);
    assert(record_id(record) == id);
    free(record);
}

static void test_duplicate_preserves_contents(void) {
    assert(catalog_create_db(catalog, DB_DUP) == CATALOG_OK);

    table_t *this = NULL;
    create_record_table(&this, DB_DUP, "records");
    for(int i = 0; i < 3; ++i)
        assert(insert_id(this, i) == (uint32_t)i);
    table_close(this);

    assert(catalog_create_db(catalog, DB_DUP) == CATALOG_ERR_EXISTS);

    assert(table_open(&this, DB_DUP, "records") == TABLE_OK);
    for(uint32_t rid = 0; rid < 3; ++rid)
        check_record(this, rid, (int)rid);
    table_close(this);

    drop_if_exists(DB_DUP);
}

static void test_missing_operations(void) {
    drop_if_exists(DB_MISSING);

    assert(catalog_use_db(catalog, DB_MISSING) == CATALOG_ERR_NOT_FOUND);
    assert(catalog_drop_db(catalog, DB_MISSING) == CATALOG_ERR_NOT_FOUND);
}

static void test_failed_use_keeps_selection(void) {
    drop_if_exists(DB_USE);
    drop_if_exists(DB_ABSENT);
    assert(catalog_create_db(catalog, DB_USE) == CATALOG_OK);

    assert(catalog_use_db(catalog, DB_USE) == CATALOG_OK);
    assert(strcmp(catalog_current_db(catalog), DB_USE) == 0);

    assert(catalog_use_db(catalog, DB_ABSENT) == CATALOG_ERR_NOT_FOUND);
    assert(strcmp(catalog_current_db(catalog), DB_USE) == 0);

    drop_if_exists(DB_USE);
    assert(catalog_current_db(catalog) == NULL);
}

static void test_table_listing(void) {
    name_list_t list;
    list_reset(&list);
    assert(catalog_list_tables(catalog, list_collect, &list)
            == CATALOG_ERR_NO_DB);

    assert(catalog_create_db(catalog, DB_LIST) == CATALOG_OK);
    assert(catalog_use_db(catalog, DB_LIST) == CATALOG_OK);

    list_reset(&list);
    assert(catalog_list_tables(catalog, list_collect, &list) == CATALOG_OK);
    assert(list.count == 0);

    table_t *t1 = NULL;
    table_t *t2 = NULL;
    create_record_table(&t1, DB_LIST, "t1");
    create_record_table(&t2, DB_LIST, "t2");
    table_close(t2);
    table_close(t1);

    /* A nested directory is not a table file and must not be listed. */
    char table_dir[MAX_FILENAME];
    char nested[MAX_FILENAME];
    table_dir_path(table_dir, sizeof(table_dir), DB_LIST);
    int n = snprintf(nested, sizeof(nested), "%snested", table_dir);
    assert(n > 0 && (size_t)n < sizeof(nested));
    assert(mkdir(nested, 0755) == 0);

    list_reset(&list);
    assert(catalog_list_tables(catalog, list_collect, &list) == CATALOG_OK);
    assert(list_contains(&list, "t1"));
    assert(list_contains(&list, "t2"));
    assert(!list_contains(&list, "nested"));
    assert(list.count == 2);

    assert(rmdir(nested) == 0);
    drop_if_exists(DB_LIST);
    assert(catalog_current_db(catalog) == NULL);
}

static void test_drop_with_tables(void) {
    assert(catalog_create_db(catalog, DB_DROP) == CATALOG_OK);

    table_t *this = NULL;
    create_record_table(&this, DB_DROP, "payloads");
    for(int i = 0; i < 10; ++i)
        assert(insert_id(this, i) == (uint32_t)i);
    table_close(this);

    char path[MAX_FILENAME];
    db_path(path, sizeof(path), DB_DROP);
    assert(path_exists(path));

    assert(catalog_drop_db(catalog, DB_DROP) == CATALOG_OK);
    assert(!path_exists(path));

    name_list_t list;
    list_reset(&list);
    assert(catalog_list_dbs(catalog, list_collect, &list) == CATALOG_OK);
    assert(!list_contains(&list, DB_DROP));
}

static void test_drop_recreate_same_names(void) {
    assert(catalog_create_db(catalog, DB_RECREATE) == CATALOG_OK);

    table_t *this = NULL;
    create_record_table(&this, DB_RECREATE, "same");
    assert(insert_id(this, 10) == 0);
    assert(insert_id(this, 11) == 1);
    table_close(this);

    assert(catalog_drop_db(catalog, DB_RECREATE) == CATALOG_OK);

    assert(table_open(&this, DB_RECREATE, "same") == TABLE_ERR_NOT_FOUND);

    assert(catalog_create_db(catalog, DB_RECREATE) == CATALOG_OK);
    assert(table_open(&this, DB_RECREATE, "same") == TABLE_ERR_NOT_FOUND);

    create_record_table(&this, DB_RECREATE, "same");
    assert(insert_id(this, 99) == 0);

    table_scan_t *scan = NULL;
    assert(table_scan_open(this, &scan) == TABLE_OK);

    uint32_t rid = 0;
    void *record = NULL;
    size_t size = 0;
    assert(table_scan_next(scan, &rid, &record, &size) == TABLE_OK);
    assert(rid == 0);
    assert(size == TEST_RECORD_SIZE);
    assert(record_id(record) == 99);
    free(record);

    assert(table_scan_next(scan, &rid, &record, &size) == TABLE_SCAN_END);
    table_scan_close(scan);
    table_close(this);

    drop_if_exists(DB_RECREATE);
}

static void test_drop_selected_vs_other(void) {
    assert(catalog_create_db(catalog, DB_SELECTED) == CATALOG_OK);
    assert(catalog_create_db(catalog, DB_OTHER) == CATALOG_OK);

    assert(catalog_use_db(catalog, DB_SELECTED) == CATALOG_OK);

    assert(catalog_drop_db(catalog, DB_OTHER) == CATALOG_OK);
    assert(strcmp(catalog_current_db(catalog), DB_SELECTED) == 0);

    assert(catalog_drop_db(catalog, DB_SELECTED) == CATALOG_OK);
    assert(catalog_current_db(catalog) == NULL);
}

static void test_reinitialize(void) {
    assert(catalog_create_db(catalog, DB_PERSIST) == CATALOG_OK);
    assert(catalog_use_db(catalog, DB_PERSIST) == CATALOG_OK);

    catalog_destroy(catalog);
    catalog = NULL;
    fs_destroy();
    fs_init();
    assert(catalog_init(&catalog) == CATALOG_OK);
    assert(catalog_current_db(catalog) == NULL);

    name_list_t list;
    list_reset(&list);
    assert(catalog_list_dbs(catalog, list_collect, &list) == CATALOG_OK);
    assert(list_contains(&list, DB_PERSIST));

    assert(catalog_use_db(catalog, DB_PERSIST) == CATALOG_OK);
    assert(strcmp(catalog_current_db(catalog), DB_PERSIST) == 0);

    drop_if_exists(DB_PERSIST);
    assert(catalog_current_db(catalog) == NULL);
}

int main(void) {
    fs_init();
    assert(catalog_init(&catalog) == CATALOG_OK);
    cleanup_all();
    assert(catalog_current_db(catalog) == NULL);

    test_initially_no_db();
    test_invalid_names();
    test_create_list_use();
    test_duplicate_preserves_contents();
    test_missing_operations();
    test_failed_use_keeps_selection();
    test_table_listing();
    test_drop_with_tables();
    test_drop_recreate_same_names();
    test_drop_selected_vs_other();
    test_reinitialize();

    cleanup_all();
    catalog_destroy(catalog);
    fs_destroy();
    return 0;
}