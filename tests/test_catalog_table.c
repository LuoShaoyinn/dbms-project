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
#include "constant.h"
#include "fs/fs.h"
#include "table/table.h"

#define DB_MAIN         "ct_main"
#define DB_DUP          "ct_dup"
#define DB_MISSING      "ct_missing"
#define DB_INVALID      "ct_invalid"
#define DB_RECREATE     "ct_recreate"
#define DB_PERSIST      "ct_persist"
#define DB_DROP         "ct_drop"

#define LIST_MAX        64
#define NAME_MAX        128

typedef struct _s_name_list {
    char name[LIST_MAX][NAME_MAX];
    size_t count;
} name_list_t;

static catalog_t *catalog;

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

static void schema_path(char *out, size_t size, const char *db,
        const char *name) {
    int n = snprintf(out, size, BASE_DIR DB_PREFIX "%s/" SCHEMA_PREFIX "%s",
            db, name);
    assert(n > 0 && (size_t)n < size);
}

static bool path_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static void drop_if_exists(const char *db) {
    catalog_status_t status = catalog_drop_db(catalog, db);
    assert(status == CATALOG_OK || status == CATALOG_ERR_NOT_FOUND);
}

static void cleanup_all(void) {
    drop_if_exists(DB_MAIN);
    drop_if_exists(DB_DUP);
    drop_if_exists(DB_MISSING);
    drop_if_exists(DB_INVALID);
    drop_if_exists(DB_RECREATE);
    drop_if_exists(DB_PERSIST);
    drop_if_exists(DB_DROP);
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

static schema_t* schema_make(const uint32_t count,
        const schema_column_t *columns) {
    schema_t *schema = NULL;
    assert(schema_create(&schema, count, columns) == SCHEMA_OK);
    return schema;
}

static void assert_column(const schema_t *schema, const uint32_t index,
        const char *name, const schema_type_t type, const uint32_t length,
        const bool not_null, const bool has_default,
        const char *default_value) {
    assert(index < schema->column_count);
    const schema_column_t *column = &schema->columns[index];

    assert(strcmp(column->name, name) == 0);
    assert(column->type == type);
    assert(column->length == length);
    assert(column->not_null == not_null);
    assert(column->has_default == has_default);
    if(has_default) {
        assert(column->default_value != NULL);
        assert(strcmp(column->default_value, default_value) == 0);
    } else {
        assert(column->default_value == NULL);
    }
}

static schema_t* users_schema(void) {
    const schema_column_t columns[] = {
        {.name = "id", .type = SCHEMA_TYPE_INT, .not_null = true},
        {.name = "score", .type = SCHEMA_TYPE_FLOAT,
                .has_default = true, .default_value = "1.5"},
        {.name = "name", .type = SCHEMA_TYPE_VARCHAR, .length = 32,
                .not_null = true, .has_default = true,
                .default_value = "guest"},
        {.name = "note", .type = SCHEMA_TYPE_VARCHAR, .length = 64}
    };
    return schema_make(4, columns);
}

static void assert_users_schema(const schema_t *schema) {
    assert(schema->column_count == 4);
    assert_column(schema, 0, "id", SCHEMA_TYPE_INT, 0, true, false, NULL);
    assert_column(schema, 1, "score", SCHEMA_TYPE_FLOAT, 0, false, true,
            "1.5");
    assert_column(schema, 2, "name", SCHEMA_TYPE_VARCHAR, 32, true, true,
            "guest");
    assert_column(schema, 3, "note", SCHEMA_TYPE_VARCHAR, 64, false, false,
            NULL);
}

static void test_schema_format_names(void) {
    char out[MAX_FMT_LENGTH];

    const schema_column_t i = {.name = "i", .type = SCHEMA_TYPE_INT};
    const schema_column_t d = {.name = "d", .type = SCHEMA_TYPE_FLOAT};
    const schema_column_t s = {.name = "s", .type = SCHEMA_TYPE_VARCHAR,
            .length = 12};

    assert(schema_format(&i, out, sizeof(out)) == 0);
    assert(strcmp(out, "i") == 0);
    assert(schema_format(&d, out, sizeof(out)) == 0);
    assert(strcmp(out, "d") == 0);
    assert(schema_format(&s, out, sizeof(out)) == 0);
    assert(strcmp(out, "s12") == 0);

    assert(strcmp(schema_type_name(SCHEMA_TYPE_INT), "INT") == 0);
    assert(strcmp(schema_type_name(SCHEMA_TYPE_FLOAT), "FLOAT") == 0);
    assert(strcmp(schema_type_name(SCHEMA_TYPE_VARCHAR), "VARCHAR") == 0);
}

static void test_schema_create_rejects_invalid(void) {
    const schema_column_t columns[] = {
        {.name = "9bad", .type = SCHEMA_TYPE_INT}
    };
    schema_t *schema = (schema_t*)1;
    assert(schema_create(&schema, 1, columns) == SCHEMA_ERR_INVALID);
    assert(schema == NULL);
}

static void test_no_db(void) {
    schema_t *schema = users_schema();
    schema_t *loaded = NULL;

    assert(catalog_create_table(catalog, "users", schema)
            == CATALOG_ERR_NO_DB);
    assert(catalog_get_schema(catalog, "users", &loaded)
            == CATALOG_ERR_NO_DB);
    assert(loaded == NULL);
    assert(catalog_drop_table(catalog, "users") == CATALOG_ERR_NO_DB);

    schema_destroy(schema);
}

static void test_create_and_describe(void) {
    assert(catalog_create_db(catalog, DB_MAIN) == CATALOG_OK);
    assert(catalog_use_db(catalog, DB_MAIN) == CATALOG_OK);

    schema_t *schema = users_schema();
    assert(catalog_create_table(catalog, "users", schema) == CATALOG_OK);
    schema_destroy(schema);

    char path[MAX_FILENAME];
    table_path(path, sizeof(path), DB_MAIN, "users");
    assert(path_exists(path));
    schema_path(path, sizeof(path), DB_MAIN, "users");
    assert(path_exists(path));

    name_list_t list;
    list_reset(&list);
    assert(catalog_list_tables(catalog, list_collect, &list) == CATALOG_OK);
    assert(list_contains(&list, "users"));

    schema_t *loaded = NULL;
    assert(catalog_get_schema(catalog, "users", &loaded) == CATALOG_OK);
    assert_users_schema(loaded);
    schema_destroy(loaded);

    /* The physical table must use the formats derived from the schema:
     * 1-byte NULL bitmap + int + double + s32 + s64. */
    table_t *table = NULL;
    assert(table_open(&table, DB_MAIN, "users") == TABLE_OK);
    assert(table->head.m == 4);
    assert(strcmp(table->head.key_name[0], "id") == 0);
    assert(strcmp(table->head.fmt[0].fmt, "i") == 0);
    assert(strcmp(table->head.fmt[1].fmt, "d") == 0);
    assert(strcmp(table->head.fmt[2].fmt, "s32") == 0);
    assert(strcmp(table->head.fmt[3].fmt, "s64") == 0);
    assert(table_get_record_size(&table->head) == 1 + 4 + 8 + 32 + 64);
    table_close(table);

    assert(catalog_drop_table(catalog, "users") == CATALOG_OK);
    assert(catalog_drop_db(catalog, DB_MAIN) == CATALOG_OK);
}

static void test_duplicate_preserves_schema(void) {
    assert(catalog_create_db(catalog, DB_DUP) == CATALOG_OK);
    assert(catalog_use_db(catalog, DB_DUP) == CATALOG_OK);

    const schema_column_t first[] = {
        {.name = "id", .type = SCHEMA_TYPE_INT, .not_null = true},
        {.name = "label", .type = SCHEMA_TYPE_VARCHAR, .length = 16}
    };
    schema_t *original = schema_make(2, first);
    assert(catalog_create_table(catalog, "dup", original) == CATALOG_OK);

    const schema_column_t second[] = {
        {.name = "value", .type = SCHEMA_TYPE_FLOAT}
    };
    schema_t *replacement = schema_make(1, second);
    assert(catalog_create_table(catalog, "dup", replacement)
            == CATALOG_ERR_EXISTS);
    schema_destroy(replacement);
    schema_destroy(original);

    schema_t *loaded = NULL;
    assert(catalog_get_schema(catalog, "dup", &loaded) == CATALOG_OK);
    assert(loaded->column_count == 2);
    assert_column(loaded, 0, "id", SCHEMA_TYPE_INT, 0, true, false, NULL);
    assert_column(loaded, 1, "label", SCHEMA_TYPE_VARCHAR, 16, false, false,
            NULL);
    schema_destroy(loaded);

    table_t *table = NULL;
    assert(table_open(&table, DB_DUP, "dup") == TABLE_OK);
    assert(table->head.m == 2);
    table_close(table);

    assert(catalog_drop_db(catalog, DB_DUP) == CATALOG_OK);
}

static void test_missing_table(void) {
    assert(catalog_create_db(catalog, DB_MISSING) == CATALOG_OK);
    assert(catalog_use_db(catalog, DB_MISSING) == CATALOG_OK);

    schema_t *loaded = NULL;
    assert(catalog_get_schema(catalog, "ghost", &loaded)
            == CATALOG_ERR_NOT_FOUND);
    assert(loaded == NULL);
    assert(catalog_drop_table(catalog, "ghost") == CATALOG_ERR_NOT_FOUND);

    assert(catalog_drop_db(catalog, DB_MISSING) == CATALOG_OK);
}

static void assert_invalid_schema(const char *name, const schema_t *schema) {
    char path[MAX_FILENAME];
    table_path(path, sizeof(path), DB_INVALID, name);
    assert(!path_exists(path));

    assert(catalog_create_table(catalog, name, schema)
            == CATALOG_ERR_INVALID);
    assert(!path_exists(path));
}

static void test_invalid_schemas(void) {
    assert(catalog_create_db(catalog, DB_INVALID) == CATALOG_OK);
    assert(catalog_use_db(catalog, DB_INVALID) == CATALOG_OK);

    /* Zero columns. */
    const schema_t empty = {0, NULL};
    assert_invalid_schema("bad_empty", &empty);

    /* Too many columns. */
    schema_column_t too_many[MAX_COLUMN + 1];
    char too_many_names[MAX_COLUMN + 1][8];
    memset(too_many, 0, sizeof(too_many));
    for(uint32_t c = 0; c <= MAX_COLUMN; ++c) {
        snprintf(too_many_names[c], sizeof(too_many_names[c]), "c%u", c);
        too_many[c].name = too_many_names[c];
        too_many[c].type = SCHEMA_TYPE_INT;
    }
    const schema_t too_many_schema = {MAX_COLUMN + 1, too_many};
    assert_invalid_schema("bad_many", &too_many_schema);

    /* Duplicate names are rejected case-insensitively. */
    schema_column_t dup[] = {
        {.name = "id", .type = SCHEMA_TYPE_INT},
        {.name = "ID", .type = SCHEMA_TYPE_INT}
    };
    const schema_t dup_schema = {2, dup};
    assert_invalid_schema("bad_dup", &dup_schema);

    /* Illegal column names. */
    schema_column_t bad_name[] = {
        {.name = "9id", .type = SCHEMA_TYPE_INT}
    };
    const schema_t bad_name_schema = {1, bad_name};
    assert_invalid_schema("bad_name", &bad_name_schema);

    schema_column_t long_name[] = {
        {.name = "this_column_name_is_far_too_long_for_the_table",
                .type = SCHEMA_TYPE_INT}
    };
    const schema_t long_name_schema = {1, long_name};
    assert_invalid_schema("bad_long_name", &long_name_schema);

    /* Type-specific size limits. */
    schema_column_t int_len[] = {
        {.name = "id", .type = SCHEMA_TYPE_INT, .length = 4}
    };
    const schema_t int_len_schema = {1, int_len};
    assert_invalid_schema("bad_int_len", &int_len_schema);

    schema_column_t varchar_zero[] = {
        {.name = "v", .type = SCHEMA_TYPE_VARCHAR, .length = 0}
    };
    const schema_t varchar_zero_schema = {1, varchar_zero};
    assert_invalid_schema("bad_varchar_zero", &varchar_zero_schema);

    schema_column_t varchar_huge[] = {
        {.name = "v", .type = SCHEMA_TYPE_VARCHAR,
                .length = MAX_VARCHAR_LENGTH + 1}
    };
    const schema_t varchar_huge_schema = {1, varchar_huge};
    assert_invalid_schema("bad_varchar_huge", &varchar_huge_schema);

    /* Defaults must parse as their column type. */
    schema_column_t int_default[] = {
        {.name = "id", .type = SCHEMA_TYPE_INT, .has_default = true,
                .default_value = "12x"}
    };
    const schema_t int_default_schema = {1, int_default};
    assert_invalid_schema("bad_int_default", &int_default_schema);

    schema_column_t float_default[] = {
        {.name = "score", .type = SCHEMA_TYPE_FLOAT, .has_default = true,
                .default_value = "NaN"}
    };
    const schema_t float_default_schema = {1, float_default};
    assert_invalid_schema("bad_float_default", &float_default_schema);

    schema_column_t varchar_default[] = {
        {.name = "v", .type = SCHEMA_TYPE_VARCHAR, .length = 4,
                .has_default = true, .default_value = "toolong"}
    };
    const schema_t varchar_default_schema = {1, varchar_default};
    assert_invalid_schema("bad_varchar_default", &varchar_default_schema);

    schema_column_t hidden_default[] = {
        {.name = "id", .type = SCHEMA_TYPE_INT, .default_value = "1"}
    };
    const schema_t hidden_default_schema = {1, hidden_default};
    assert_invalid_schema("bad_hidden_default", &hidden_default_schema);

    schema_column_t missing_default[] = {
        {.name = "id", .type = SCHEMA_TYPE_INT, .has_default = true,
                .default_value = NULL}
    };
    const schema_t missing_default_schema = {1, missing_default};
    assert_invalid_schema("bad_missing_default", &missing_default_schema);

    /* One record must fit in a data page. */
    schema_column_t wide[MAX_COLUMN];
    char wide_names[MAX_COLUMN][8];
    memset(wide, 0, sizeof(wide));
    for(uint32_t c = 0; c < MAX_COLUMN; ++c) {
        snprintf(wide_names[c], sizeof(wide_names[c]), "w%u", c);
        wide[c].name = wide_names[c];
        wide[c].type = SCHEMA_TYPE_VARCHAR;
        wide[c].length = MAX_VARCHAR_LENGTH;
    }
    const schema_t wide_schema = {MAX_COLUMN, wide};
    assert_invalid_schema("bad_wide", &wide_schema);

    /* The serialized metadata must fit in one page even when the record
     * itself does. */
    schema_column_t meta[MAX_COLUMN];
    char meta_names[MAX_COLUMN][MAX_KEY_NAME_LENGTH];
    char meta_defaults[MAX_COLUMN][1001];
    memset(meta, 0, sizeof(meta));
    for(uint32_t c = 0; c < MAX_COLUMN; ++c) {
        snprintf(meta_names[c], sizeof(meta_names[c]),
                "column_%u_0123456789012345", c);
        memset(meta_defaults[c], 'a', 1000);
        meta_defaults[c][1000] = '\0';
        meta[c].name = meta_names[c];
        meta[c].type = SCHEMA_TYPE_VARCHAR;
        meta[c].length = 1000;
        meta[c].has_default = true;
        meta[c].default_value = meta_defaults[c];
    }
    const schema_t meta_schema = {MAX_COLUMN, meta};
    assert_invalid_schema("bad_meta", &meta_schema);

    /* Invalid table names never create files either. */
    schema_column_t ok_column[] = {
        {.name = "id", .type = SCHEMA_TYPE_INT}
    };
    const schema_t ok_schema = {1, ok_column};
    assert(catalog_create_table(catalog, "9bad", &ok_schema)
            == CATALOG_ERR_INVALID);
    assert(catalog_create_table(catalog, "bad/name", &ok_schema)
            == CATALOG_ERR_INVALID);

    name_list_t list;
    list_reset(&list);
    assert(catalog_list_tables(catalog, list_collect, &list) == CATALOG_OK);
    assert(list.count == 0);

    assert(catalog_drop_db(catalog, DB_INVALID) == CATALOG_OK);
}

static void test_drop_and_recreate(void) {
    assert(catalog_create_db(catalog, DB_RECREATE) == CATALOG_OK);
    assert(catalog_use_db(catalog, DB_RECREATE) == CATALOG_OK);

    const schema_column_t first[] = {
        {.name = "id", .type = SCHEMA_TYPE_INT, .not_null = true},
        {.name = "name", .type = SCHEMA_TYPE_VARCHAR, .length = 8,
                .not_null = true, .has_default = true,
                .default_value = "anon"}
    };
    schema_t *first_schema = schema_make(2, first);
    assert(catalog_create_table(catalog, "swap", first_schema) == CATALOG_OK);
    schema_destroy(first_schema);

    char path[MAX_FILENAME];
    table_path(path, sizeof(path), DB_RECREATE, "swap");
    assert(path_exists(path));
    schema_path(path, sizeof(path), DB_RECREATE, "swap");
    assert(path_exists(path));

    assert(catalog_drop_table(catalog, "swap") == CATALOG_OK);
    table_path(path, sizeof(path), DB_RECREATE, "swap");
    assert(!path_exists(path));
    schema_path(path, sizeof(path), DB_RECREATE, "swap");
    assert(!path_exists(path));

    schema_t *loaded = NULL;
    assert(catalog_get_schema(catalog, "swap", &loaded)
            == CATALOG_ERR_NOT_FOUND);
    assert(loaded == NULL);
    assert(catalog_drop_table(catalog, "swap") == CATALOG_ERR_NOT_FOUND);

    const schema_column_t second[] = {
        {.name = "k", .type = SCHEMA_TYPE_INT},
        {.name = "v", .type = SCHEMA_TYPE_FLOAT},
        {.name = "tag", .type = SCHEMA_TYPE_VARCHAR, .length = 4}
    };
    schema_t *second_schema = schema_make(3, second);
    assert(catalog_create_table(catalog, "swap", second_schema)
            == CATALOG_OK);
    schema_destroy(second_schema);

    assert(catalog_get_schema(catalog, "swap", &loaded) == CATALOG_OK);
    assert(loaded->column_count == 3);
    assert_column(loaded, 0, "k", SCHEMA_TYPE_INT, 0, false, false, NULL);
    assert_column(loaded, 1, "v", SCHEMA_TYPE_FLOAT, 0, false, false, NULL);
    assert_column(loaded, 2, "tag", SCHEMA_TYPE_VARCHAR, 4, false, false,
            NULL);
    schema_destroy(loaded);

    table_t *table = NULL;
    assert(table_open(&table, DB_RECREATE, "swap") == TABLE_OK);
    assert(table->head.m == 3);
    table_close(table);

    name_list_t list;
    list_reset(&list);
    assert(catalog_list_tables(catalog, list_collect, &list) == CATALOG_OK);
    assert(list.count == 1);
    assert(list_contains(&list, "swap"));

    assert(catalog_drop_db(catalog, DB_RECREATE) == CATALOG_OK);
}

static void test_persistence(void) {
    assert(catalog_create_db(catalog, DB_PERSIST) == CATALOG_OK);
    assert(catalog_use_db(catalog, DB_PERSIST) == CATALOG_OK);

    const schema_column_t columns[] = {
        {.name = "id", .type = SCHEMA_TYPE_INT, .not_null = true},
        {.name = "label", .type = SCHEMA_TYPE_VARCHAR, .length = 20,
                .has_default = true, .default_value = "x"}
    };
    schema_t *schema = schema_make(2, columns);
    assert(catalog_create_table(catalog, "saved", schema) == CATALOG_OK);
    schema_destroy(schema);

    catalog_destroy(catalog);
    catalog = NULL;
    fs_destroy();
    fs_init();
    assert(catalog_init(&catalog) == CATALOG_OK);
    assert(catalog_current_db(catalog) == NULL);

    assert(catalog_use_db(catalog, DB_PERSIST) == CATALOG_OK);

    schema_t *loaded = NULL;
    assert(catalog_get_schema(catalog, "saved", &loaded) == CATALOG_OK);
    assert(loaded->column_count == 2);
    assert_column(loaded, 0, "id", SCHEMA_TYPE_INT, 0, true, false, NULL);
    assert_column(loaded, 1, "label", SCHEMA_TYPE_VARCHAR, 20, false, true,
            "x");
    schema_destroy(loaded);

    name_list_t list;
    list_reset(&list);
    assert(catalog_list_tables(catalog, list_collect, &list) == CATALOG_OK);
    assert(list_contains(&list, "saved"));

    assert(catalog_drop_db(catalog, DB_PERSIST) == CATALOG_OK);
}

static void test_drop_database_removes_schema(void) {
    assert(catalog_create_db(catalog, DB_DROP) == CATALOG_OK);
    assert(catalog_use_db(catalog, DB_DROP) == CATALOG_OK);

    schema_t *schema = users_schema();
    assert(catalog_create_table(catalog, "gone", schema) == CATALOG_OK);
    schema_destroy(schema);

    char path[MAX_FILENAME];
    schema_path(path, sizeof(path), DB_DROP, "gone");
    assert(path_exists(path));

    assert(catalog_drop_db(catalog, DB_DROP) == CATALOG_OK);
    db_path(path, sizeof(path), DB_DROP);
    assert(!path_exists(path));

    assert(catalog_create_db(catalog, DB_DROP) == CATALOG_OK);
    assert(catalog_use_db(catalog, DB_DROP) == CATALOG_OK);

    schema_t *loaded = NULL;
    assert(catalog_get_schema(catalog, "gone", &loaded)
            == CATALOG_ERR_NOT_FOUND);
    assert(loaded == NULL);

    name_list_t list;
    list_reset(&list);
    assert(catalog_list_tables(catalog, list_collect, &list) == CATALOG_OK);
    assert(list.count == 0);

    assert(catalog_drop_db(catalog, DB_DROP) == CATALOG_OK);
}

int main(void) {
    fs_init();
    assert(catalog_init(&catalog) == CATALOG_OK);
    cleanup_all();
    assert(catalog_current_db(catalog) == NULL);

    test_schema_format_names();
    test_schema_create_rejects_invalid();
    test_no_db();
    test_create_and_describe();
    test_duplicate_preserves_schema();
    test_missing_table();
    test_invalid_schemas();
    test_drop_and_recreate();
    test_persistence();
    test_drop_database_removes_schema();

    cleanup_all();
    catalog_destroy(catalog);
    fs_destroy();
    return 0;
}