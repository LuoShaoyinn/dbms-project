/* test_catalog_table.c
 *      isolated catalog-level table management tests
 */

#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "catalog/catalog.h"
#include "column/column.h"
#include "constant.h"
#include "fs/fs.h"
#include "table/table.h"

#define DB_MAIN         "ct_main"
#define DB_DUP          "ct_dup"
#define DB_MISSING      "ct_missing"
#define DB_RECREATE     "ct_recreate"
#define DB_PERSIST      "ct_persist"
#define DB_DROP         "ct_drop"

#define LIST_MAX        64

typedef struct _s_name_list {
    char name[LIST_MAX][MAX_DB_NAME_LENGTH];
    size_t count;
} name_list_t;

static void db_path(char *out, size_t size, const char *db) {
    int n = snprintf(out, size, BASE_DIR DB_PREFIX "%s", db);
    assert(n > 0 && (size_t)n < size);
}

static void table_path(char *out, size_t size, const char *db,
        const char *name) {
    int n = snprintf(out, size, BASE_DIR DB_PREFIX "%s/" TABLE_PREFIX "%s",
            db, name);
    assert(n > 0 && (size_t)n < size);
}

static bool path_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static void drop_if_exists(const char *db) {
    char path[MAX_FILENAME];
    db_path(path, sizeof(path), db);
    if(path_exists(path)) assert(catalog_drop_db(db) == 0);
}

static void cleanup_all(void) {
    drop_if_exists(DB_MAIN);
    drop_if_exists(DB_DUP);
    drop_if_exists(DB_MISSING);
    drop_if_exists(DB_RECREATE);
    drop_if_exists(DB_PERSIST);
    drop_if_exists(DB_DROP);
}

static void list_reset(name_list_t *list) {
    list->count = 0;
}

static bool list_contains(const name_list_t *list, const char *name) {
    for(size_t i = 0; i < list->count; ++i)
        if(strcmp(list->name[i], name) == 0) return true;
    return false;
}

static const column_t users_columns[] = {
    {.name = "id", .type = {ELEM_TYPE_I, 4}, .not_null = true},
    {.name = "score", .type = {ELEM_TYPE_D, 8}},
    {.name = "name", .type = {ELEM_TYPE_S, 32}, .not_null = true},
    {.name = "note", .type = {ELEM_TYPE_S, 64}},
};
static const char *users_defaults[] = {NULL, "1.5", "guest", NULL};

static void assert_column(const table_header_t *head, const uint32_t index,
        const char *name, const char *fmt, const bool not_null,
        const char *default_value) {
    assert(index < head->m);
    const column_t *column = &head->columns[index];

    assert(strcmp(column->name, name) == 0);
    elem_type_t expected;
    elem_type_init(&expected, fmt);
    assert(column->type.kind == expected.kind);
    assert(column->type.size == expected.size);
    assert((column->not_null != 0) == not_null);
    const char *value = column_default(head, column);
    if(default_value == NULL) {
        assert(value == NULL);
    } else {
        assert(value != NULL);
        assert(strcmp(value, default_value) == 0);
    }
}

static void assert_users(const table_header_t *head) {
    assert(head->m == 4);
    assert_column(head, 0, "id", "i", true, NULL);
    assert_column(head, 1, "score", "d", false, "1.5");
    assert_column(head, 2, "name", "s32", true, "guest");
    assert_column(head, 3, "note", "s64", false, NULL);
}

static void test_column_helpers(void) {
    column_t column = {.name = "c", .type = {ELEM_TYPE_S, 12}};

    assert(column_size(&column) == 12);
    assert(column_length(&column) == 12);
    assert(strcmp(column_type_name(&column), "VARCHAR") == 0);

    elem_type_init(&column.type, "i");
    assert(column_size(&column) == sizeof(int32_t));
    assert(column_length(&column) == 0);
    assert(strcmp(column_type_name(&column), "INT") == 0);

    elem_type_init(&column.type, "d");
    assert(column_size(&column) == sizeof(double));
    assert(strcmp(column_type_name(&column), "FLOAT") == 0);
}

static void test_create_and_describe(void) {
    const char *db_name = DB_MAIN;
    assert(catalog_create_db(db_name) == 0);

    assert(catalog_create_table(db_name, "users", 4, users_columns,
            users_defaults) == 0);

    char path[MAX_FILENAME];
    table_path(path, sizeof(path), DB_MAIN, "users");
    assert(path_exists(path));

    name_list_t list;
    list_reset(&list);
    assert(catalog_list_tables(db_name, list.name, &list.count) == 0);
    assert(list_contains(&list, "users"));

    table_t *table = NULL;
    assert(table_open(&table, DB_MAIN, "users") == 0);
    table_header_t *head = table_get_header(table);

    assert_users(head);
    assert(head->record_size == 1 + 4 + 8 + 32 + 64);
    assert(table_get_record_size(head) == head->record_size);
    assert(head->columns[0].offset == 1);
    assert(head->columns[1].offset == 5);
    assert(head->columns[2].offset == 13);
    assert(head->columns[3].offset == 45);
    assert(strcmp(column_type_name(&head->columns[2]), "VARCHAR") == 0);

    table_close(table);

    assert(catalog_drop_table(db_name, "users") == 0);
    assert(catalog_drop_db(db_name) == 0);
}

static void test_duplicate_preserves_table(void) {
    const char *db_name = DB_DUP;
    assert(catalog_create_db(db_name) == 0);

    const column_t first[] = {
        {.name = "id", .type = {ELEM_TYPE_I, 4}, .not_null = true},
        {.name = "label", .type = {ELEM_TYPE_S, 16}}
    };
    assert(catalog_create_table(db_name, "dup", 2, first, NULL)
            == 0);

    const column_t second[] = {{.name = "value", .type = {ELEM_TYPE_D, 8}}};
    assert(catalog_create_table(db_name, "dup", 1, second, NULL)
            == -1);

    table_t *table = NULL;
    assert(table_open(&table, DB_DUP, "dup") == 0);
    table_header_t *head = table_get_header(table);
    assert(head->m == 2);
    assert_column(head, 0, "id", "i", true, NULL);
    assert_column(head, 1, "label", "s16", false, NULL);
    table_close(table);

    assert(catalog_drop_db(db_name) == 0);
}

static void test_missing_table(void) {
    const char *db_name = DB_MISSING;
    assert(catalog_create_db(db_name) == 0);

    table_t *table = NULL;
    assert(table_open(&table, DB_MISSING, "ghost") == -1);
    assert(table == NULL);
    assert(catalog_drop_table(db_name, "ghost") == -1);

    assert(catalog_drop_db(db_name) == 0);
}

static void test_statement_limits(void) {
    const char *db_name = DB_MAIN;
    assert(catalog_create_db(db_name) == 0);

    column_t columns[MAX_COLUMN] = {0};
    for(uint32_t c = 0; c < MAX_COLUMN; ++c) {
        snprintf(columns[c].name, sizeof(columns[c].name), "c%u", c);
        elem_type_init(&columns[c].type, "i");
    }
    memset(columns[0].name, 'a', MAX_NAME_LENGTH);
    columns[0].name[MAX_NAME_LENGTH] = '\0';

    char name[MAX_TABLE_NAME_LENGTH];
    memset(name, 't', MAX_NAME_LENGTH);
    name[MAX_NAME_LENGTH] = '\0';
    assert(catalog_create_table(db_name, name, MAX_COLUMN, columns, NULL) == 0);

    table_t *table;
    assert(table_open(&table, DB_MAIN, name) == 0);
    table_header_t *head = table_get_header(table);
    assert(head == table->header_page->ptr);
    assert(head->m == 10);
    assert(strlen(head->columns[0].name) == 32);
    assert(head->columns[0].offset == 2); /* ten columns need two NULL bytes */
    assert(head->record_size == 2 + 10 * sizeof(int32_t));
    table_close(table);

    /* Exercise the maximum CI string width and a full-width default. */
    column_t text = {.name = "text", .type = {ELEM_TYPE_S, 2048}};
    char value[MAX_VARCHAR_LENGTH + 1];
    memset(value, 'x', MAX_VARCHAR_LENGTH);
    value[MAX_VARCHAR_LENGTH] = '\0';
    const char *defaults[] = {value};
    assert(catalog_create_table(db_name, "wide", 1, &text, defaults) == 0);
    assert(table_open(&table, DB_MAIN, "wide") == 0);
    head = table_get_header(table);
    assert(column_length(&head->columns[0]) == 2048);
    assert(head->record_size == 1 + MAX_RECORD_PAYLOAD);
    assert(strcmp(column_default(head, &head->columns[0]), value) == 0);
    table_close(table);
    assert(catalog_drop_db(db_name) == 0);
}

static void test_drop_and_recreate(void) {
    const char *db_name = DB_RECREATE;
    assert(catalog_create_db(db_name) == 0);

    const column_t first[] = {
        {.name = "id", .type = {ELEM_TYPE_I, 4}, .not_null = true},
        {.name = "name", .type = {ELEM_TYPE_S, 8}, .not_null = true}
    };
    const char *first_defaults[] = {NULL, "anon"};
    assert(catalog_create_table(db_name, "swap", 2, first, first_defaults)
            == 0);

    char path[MAX_FILENAME];
    table_path(path, sizeof(path), DB_RECREATE, "swap");
    assert(path_exists(path));

    assert(catalog_drop_table(db_name, "swap") == 0);
    table_path(path, sizeof(path), DB_RECREATE, "swap");
    assert(!path_exists(path));

    table_t *table = NULL;
    assert(table_open(&table, DB_RECREATE, "swap") == -1);
    assert(table == NULL);
    assert(catalog_drop_table(db_name, "swap") == -1);

    const column_t second[] = {
        {.name = "k", .type = {ELEM_TYPE_I, 4}},
        {.name = "v", .type = {ELEM_TYPE_D, 8}},
        {.name = "tag", .type = {ELEM_TYPE_S, 4}}
    };
    assert(catalog_create_table(db_name, "swap", 3, second, NULL)
            == 0);

    assert(table_open(&table, DB_RECREATE, "swap") == 0);
    table_header_t *head = table_get_header(table);
    assert(head->m == 3);
    assert_column(head, 0, "k", "i", false, NULL);
    assert_column(head, 1, "v", "d", false, NULL);
    assert_column(head, 2, "tag", "s4", false, NULL);
    table_close(table);

    name_list_t list;
    list_reset(&list);
    assert(catalog_list_tables(db_name, list.name, &list.count) == 0);
    assert(list.count == 1);
    assert(list_contains(&list, "swap"));

    assert(catalog_drop_db(db_name) == 0);
}

static void test_persistence(void) {
    const char *db_name = DB_PERSIST;
    assert(catalog_create_db(db_name) == 0);

    const column_t columns[] = {
        {.name = "id", .type = {ELEM_TYPE_I, 4}, .not_null = true},
        {.name = "label", .type = {ELEM_TYPE_S, 20}}
    };
    const char *defaults[] = {NULL, "x"};
    assert(catalog_create_table(db_name, "saved", 2, columns, defaults)
            == 0);

    fs_destroy();
    fs_init();
    assert(catalog_db_exists(db_name));

    table_t *table = NULL;
    assert(table_open(&table, DB_PERSIST, "saved") == 0);
    table_header_t *head = table_get_header(table);
    assert(head->m == 2);
    assert_column(head, 0, "id", "i", true, NULL);
    assert_column(head, 1, "label", "s20", false, "x");
    table_close(table);

    name_list_t list;
    list_reset(&list);
    assert(catalog_list_tables(db_name, list.name, &list.count) == 0);
    assert(list_contains(&list, "saved"));

    assert(catalog_drop_db(db_name) == 0);
}

static void test_drop_database_removes_tables(void) {
    const char *db_name = DB_DROP;
    assert(catalog_create_db(db_name) == 0);

    assert(catalog_create_table(db_name, "gone", 4, users_columns,
            users_defaults) == 0);

    char path[MAX_FILENAME];
    table_path(path, sizeof(path), DB_DROP, "gone");
    assert(path_exists(path));

    assert(catalog_drop_db(db_name) == 0);
    db_path(path, sizeof(path), DB_DROP);
    assert(!path_exists(path));

    assert(catalog_create_db(db_name) == 0);

    table_t *table = NULL;
    assert(table_open(&table, DB_DROP, "gone") == -1);
    assert(table == NULL);

    name_list_t list;
    list_reset(&list);
    assert(catalog_list_tables(db_name, list.name, &list.count) == 0);
    assert(list.count == 0);

    assert(catalog_drop_db(db_name) == 0);
}

int main(void) {
    fs_init();
    cleanup_all();

    test_column_helpers();
    test_create_and_describe();
    test_duplicate_preserves_table();
    test_missing_table();
    test_statement_limits();
    test_drop_and_recreate();
    test_persistence();
    test_drop_database_removes_tables();

    cleanup_all();
    fs_destroy();
    return 0;
}
