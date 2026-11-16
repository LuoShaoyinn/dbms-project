/* test_catalog.c: stateless database management */
#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "catalog/catalog.h"
#include "record/record.h"

#define DB_A "cat_alpha"
#define DB_B "cat_beta"
#define DB_MISSING "cat_missing"

typedef struct {
    char names[64][MAX_DB_NAME_LENGTH];
    size_t count;
} names_t;

static bool contains(const names_t *list, const char *name) {
    for(size_t i = 0; i < list->count; ++i)
        if(strcmp(list->names[i], name) == 0) return true;
    return false;
}

static void cleanup(void) {
    const char *dbs[] = {DB_A, DB_B, DB_MISSING};
    for(unsigned i = 0; i < 3; ++i)
        if(catalog_db_exists(dbs[i])) assert(catalog_drop_db(dbs[i]) == 0);
}

static void make_table(const char *db, int32_t value) {
    const column_t col = {.name = "id", .type = {ELEM_TYPE_I, 4}};
    assert(catalog_create_table(db, "same", 1, &col, NULL) == 0);
    table_t *table;
    assert(table_open(&table, db, "same") == 0);
    record_t record = record_alloc(table);
    memcpy(record_field(&record, 0), &value, sizeof(value));
    record_return(&record);
    table_close(table);
}

static void check_table(const char *db, int32_t expected) {
    table_t *table;
    assert(table_open(&table, db, "same") == 0);
    record_t record = record_borrow(table, 0);
    int32_t value;
    memcpy(&value, record_field(&record, 0), sizeof(value));
    assert(value == expected);
    record_return(&record);
    table_close(table);
}

int main(void) {
    fs_init();
    cleanup();
    char cwd[MAX_FILENAME], after[MAX_FILENAME];
    assert(getcwd(cwd, sizeof(cwd)) != NULL);

    assert(!catalog_db_exists(DB_MISSING));
    assert(catalog_drop_db(DB_MISSING) == -1);
    names_t list = {0};
    assert(catalog_list_tables(DB_MISSING, list.names, &list.count) == -1);
    assert(list.count == 0);

    assert(catalog_create_db(DB_A) == 0);
    assert(catalog_create_db(DB_B) == 0);
    assert(catalog_db_exists(DB_A) && catalog_db_exists(DB_B));
    struct stat st;
    assert(stat(BASE_DIR DB_PREFIX DB_A "/" TABLE_PREFIX, &st) == 0);
    assert(S_ISDIR(st.st_mode));
    assert(stat(BASE_DIR DB_PREFIX DB_A "/" INDEX_PREFIX, &st) == 0);
    assert(S_ISDIR(st.st_mode));

    assert(catalog_list_dbs(list.names, &list.count) == 0);
    assert(contains(&list, DB_A) && contains(&list, DB_B));
    size_t count = list.count;
    assert(catalog_list_dbs(list.names, &list.count) == 0);
    assert(list.count == count); /* listing replaces, rather than appends */

    /* Same table name in two databases, without USE or hidden selection. */
    make_table(DB_A, 11);
    make_table(DB_B, 22);
    assert(catalog_create_db(DB_A) == -1);
    check_table(DB_B, 22);
    check_table(DB_A, 11);

    list = (names_t){0};
    assert(catalog_list_tables(DB_A, list.names, &list.count) == 0);
    assert(list.count == 1 && contains(&list, "same"));

    /* Listing ignores nested directories rather than treating them as tables. */
    const char *nested = BASE_DIR DB_PREFIX DB_A "/" TABLE_PREFIX "nested";
    assert(mkdir(nested, 0755) == 0);
    list = (names_t){0};
    assert(catalog_list_tables(DB_A, list.names, &list.count) == 0);
    assert(list.count == 1 && !contains(&list, "nested"));
    assert(rmdir(nested) == 0);

    fs_destroy();
    fs_init(); /* no catalog initialization or session restoration needed */
    list = (names_t){0};
    assert(catalog_list_dbs(list.names, &list.count) == 0);
    assert(contains(&list, DB_A) && contains(&list, DB_B));
    check_table(DB_A, 11);
    check_table(DB_B, 22);

    assert(catalog_drop_db(DB_A) == 0);
    assert(!catalog_db_exists(DB_A));
    check_table(DB_B, 22);
    assert(catalog_create_db(DB_A) == 0);
    make_table(DB_A, 99); /* recreated names cannot see cached old file data */
    check_table(DB_A, 99);
    check_table(DB_B, 22);

    assert(getcwd(after, sizeof(after)) != NULL);
    assert(strcmp(cwd, after) == 0);
    cleanup();
    fs_destroy();
    return 0;
}
