/* test_record.c: direct borrow/modify/return and RID traversal */
#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "record/record.h"

#define TEST_DB "record_test"
#define MANY_N 4200

static const column_t columns[] = {
    {.name = "id", .type = {ELEM_TYPE_I, 4}},
    {.name = "payload", .type = {ELEM_TYPE_S, 12}},
};

static void start(const char *name) {
    fs_init();
    mkdir(BASE_DIR DB_PREFIX TEST_DB, 0755);
    mkdir(BASE_DIR DB_PREFIX TEST_DB "/" TABLE_PREFIX, 0755);
    fs_remove(fs_name_db_table(TEST_DB, name));
}

static void finish(const char *name) {
    fs_remove(fs_name_db_table(TEST_DB, name));
    fs_destroy();
}

static table_t* create(const char *name) {
    table_t *table;
    assert(table_create(&table, TEST_DB, name, 2, columns, NULL) == 0);
    return table;
}

static uint32_t insert(table_t *table, int32_t value) {
    record_t record = record_alloc(table);
    assert(record.ptr != NULL);
    assert(record.ref_column == table_get_header(table)->columns);
    assert((char*)record.ptr >= (char*)record.block->ptr);
    memcpy(record_field(&record, 0), &value, sizeof(value));
    memset(record_field(&record, 1), (unsigned char)value, 12);
    uint32_t rid = record.rid;
    record_return(&record);
    return rid;
}

static void check(table_t *table, uint32_t rid, int32_t value) {
    record_t record = record_borrow(table, rid);
    assert(record.ptr != NULL && record.rid == rid);
    int32_t actual;
    memcpy(&actual, record_field(&record, 0), sizeof(actual));
    assert(actual == value);
    for(unsigned i = 0; i < 12; ++i)
        assert(((unsigned char*)record_field(&record, 1))[i] == (unsigned char)value);
    record_return(&record);
}

static void test_crud_and_traversal(void) {
    start("many");
    table_t *table = create("many");
    table_header_t *head = table_get_header(table);
    assert(head->page_count == 0);
    for(uint32_t i = 0; i < MANY_N; ++i) assert(insert(table, (int32_t)i) == i);
    assert(head->page_count > 64);
    for(uint32_t i = 0; i < MANY_N; ++i) check(table, i, (int32_t)i);

    fs_datablock_t *page = fs_borrow(fs_name_db_table(TEST_DB, "many"),
            PAGE_SIZE, PAGE_SIZE);
    assert(((table_page_t*)page->ptr)->slot_bitmap == UINT64_MAX);
    fs_return(page);

    record_t record = record_borrow(table, 63);
    int32_t changed = 9999;
    memcpy(record_field(&record, 0), &changed, sizeof(changed));
    record_return(&record);
    record = record_borrow(table, 63);
    int32_t actual;
    memcpy(&actual, record_field(&record, 0), sizeof(actual));
    assert(actual == changed);
    record_return(&record);
    check(table, 62, 62);
    check(table, 64, 64);

    assert(record_delete(table, 0) == 0);
    assert(record_delete(table, 2048) == 0);
    assert(record_delete(table, MANY_N - 1) == 0);
    uint32_t count = 0;
    uint32_t slots = table_get_slots_per_page(head);
    for(uint32_t rid = 0; rid < head->page_count * slots; ++rid) {
        record = record_borrow(table, rid);
        if(record.ptr == NULL) continue;
        assert(rid != 0 && rid != 2048 && rid != MANY_N - 1);
        assert(rid < MANY_N);
        ++count;
        record_return(&record);
    }
    assert(count == MANY_N - 3);
    uint32_t pages = head->page_count;
    assert(insert(table, 8000) == 0);
    assert(insert(table, 8001) == 2048);
    assert(insert(table, 8002) == MANY_N - 1);
    assert(head->page_count == pages);
    table_close(table);
    finish("many");
}

static void test_nulls_and_numeric_fields(void) {
    start("nulls");
    column_t cols[MAX_COLUMN] = {0};
    for(uint32_t i = 0; i < MAX_COLUMN; ++i)
        cols[i].type = (elem_type_t){ELEM_TYPE_I, 4};
    cols[1].type = (elem_type_t){ELEM_TYPE_D, 8};
    table_t *table;
    assert(table_create(&table, TEST_DB, "nulls", MAX_COLUMN, cols, NULL) == 0);
    record_t record = record_alloc(table);
    double value = 3.25, actual;
    memcpy(record_field(&record, 1), &value, sizeof(value));
    record_set_null(&record, 0, true);
    record_set_null(&record, 8, true);
    record_set_null(&record, 9, true);
    record_set_null(&record, 8, false);
    assert(record_is_null(&record, 0));
    assert(!record_is_null(&record, 1));
    assert(!record_is_null(&record, 8));
    assert(record_is_null(&record, 9));
    record_return(&record);
    record = record_borrow(table, 0);
    memcpy(&actual, record_field(&record, 1), sizeof(actual));
    assert(actual == value && record_is_null(&record, 9));
    record_return(&record);
    table_close(table);
    finish("nulls");
}

static void verify_persist(void) {
    table_t *table;
    assert(table_open(&table, TEST_DB, "persist") == 0);
    assert(table_get_header(table)->page_count == 3);
    for(uint32_t rid = 0; rid < 130; ++rid) {
        if(rid == 64) {
            record_t hole = record_borrow(table, rid);
            assert(hole.ptr == NULL);
        } else check(table, rid, (int32_t)rid);
    }
    table_close(table);
}

static void test_restart(void) {
    start("persist");
    table_t *table = create("persist");
    for(uint32_t i = 0; i < 130; ++i) insert(table, (int32_t)i);
    assert(record_delete(table, 64) == 0);
    table_close(table);
    fs_destroy();
    fs_init();
    verify_persist();
    fs_destroy();
    pid_t child = fork();
    assert(child >= 0);
    if(child == 0) {
        fs_init();
        verify_persist();
        fs_destroy();
        exit(0);
    }
    int status;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    fs_init();
    assert(table_open(&table, TEST_DB, "persist") == 0);
    assert(insert(table, 7000) == 64); /* persisted free-page hint */
    check(table, 64, 7000);
    table_close(table);
    finish("persist");
}

static void test_evicted_borrows_and_two_handles(void) {
    start("pinned");
    table_t *a = create("pinned");
    assert(insert(a, 10) == 0);
    record_t held = record_borrow(a, 0);
    for(uint32_t i = 0; i <= FS_LRU_MAXSIZE; ++i) {
        fs_datablock_t *page = fs_borrow(fs_name_db_table(TEST_DB, "filler"),
                (size_t)i * PAGE_SIZE, PAGE_SIZE);
        fs_return(page);
    }
    assert(!held.block->_ref_fs_page->is_in_lru);
    assert(!a->header_page->_ref_fs_page->is_in_lru);
    int32_t value = 123;
    memcpy(record_field(&held, 0), &value, sizeof(value));
    record_return(&held); /* original borrow remains returnable after eviction */

    table_t *b;
    assert(table_open(&b, TEST_DB, "pinned") == 0);
    assert(insert(b, 11) == 1);
    assert(insert(a, 12) == 2);
    check(a, 1, 11);
    check(b, 2, 12);
    record_t record = record_borrow(b, 0);
    memcpy(&value, record_field(&record, 0), sizeof(value));
    assert(value == 123);
    record_return(&record);
    table_close(a);
    assert(insert(b, 13) == 3);
    table_close(b);
    fs_remove(fs_name_db_table(TEST_DB, "filler"));
    finish("pinned");
}

static void test_append_hint_and_large_records(void) {
    start("large");
    const column_t column = {.name = "payload", .type = {ELEM_TYPE_S, 8183}};
    table_t *table;
    assert(table_create(&table, TEST_DB, "large", 1, &column, NULL) == 0);
    table_header_t *head = table_get_header(table);
    assert(table_get_slots_per_page(head) == 1);
    for(uint32_t i = 0; i < 128; ++i) {
        record_t record = record_alloc(table);
        assert(record.rid == i);
        assert(head->free_page_hint == head->page_count + 1);
        memcpy(record_field(&record, 0), &i, sizeof(i));
        record_return(&record);
    }
    assert(record_delete(table, 0) == 0);
    record_t record = record_alloc(table);
    assert(record.rid == 0 && head->page_count == 128);
    for(size_t i = 0; i < head->record_size; ++i)
        assert(((unsigned char*)record.ptr)[i] == 0);
    record_return(&record);
    table_close(table);
    finish("large");
}

int main(void) {
    test_crud_and_traversal();
    test_nulls_and_numeric_fields();
    test_restart();
    test_evicted_borrows_and_two_handles();
    test_append_hint_and_large_records();
    return 0;
}
