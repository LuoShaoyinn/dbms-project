/* test_table_crud.c
 *      isolated record CRUD and scan tests for backend/table
 */

#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "constant.h"
#include "fs/fs.h"
#include "table/table.h"

#define TEST_DB             "crud"
#define TEST_RECORD_SIZE    17      /* 1 NULL bitmap + int + s12 */
#define TEST_PAYLOAD_SIZE   12

#define MANY_N              4200
#define SCAN_N              200
#define PERSIST_N           130
#define PERSIST_DELETED     64

static void ensure_dirs(void) {
    mkdir("data", 0755);
    mkdir("data/db", 0755);
    mkdir("data/db/" TEST_DB, 0755);
    mkdir("data/db/" TEST_DB "/table", 0755);
    mkdir("data/db/" TEST_DB "/index", 0755);
}

static char* table_path(const char *name) {
    return fs_name_db_table(TEST_DB, name);
}

static off_t file_size(const char *name) {
    char *path = table_path(name);
    struct stat st;
    int ret = stat(path, &st);
    free(path);
    assert(ret == 0);
    return st.st_size;
}

static int table_is_missing(const char *name) {
    char *path = table_path(name);
    struct stat st;
    int ret = stat(path, &st);
    free(path);
    return ret != 0;
}

static void fs_start(const char *name) {
    ensure_dirs();
    fs_init();
    fs_remove(table_path(name));
}

static void fs_finish(const char *name) {
    fs_remove(table_path(name));
    fs_destroy();
}

static void record_fill(unsigned char *record, const int id, const int seed) {
    memset(record, 0, TEST_RECORD_SIZE);
    memcpy(record + 1, &id, sizeof(id)); /* NULL bitmap stays zero */
    for(int i = 0; i < TEST_PAYLOAD_SIZE; ++i)
        record[5 + i] = (unsigned char)(seed + i);
}

static int record_id(const void *record) {
    int id;
    memcpy(&id, (const char*)record + 1, sizeof(id));
    return id;
}

static void record_check(const void *record, const int id, const int seed) {
    const unsigned char *bytes = (const unsigned char*)record;
    assert(record_id(record) == id);
    for(int i = 0; i < TEST_PAYLOAD_SIZE; ++i)
        assert(bytes[5 + i] == (unsigned char)(seed + i));
}

static void check_record(table_t *this, const uint32_t rid, const int id,
        const int seed) {
    void *record = NULL;
    size_t size = 0;
    assert(table_get(this, rid, &record, &size) == TABLE_OK);
    assert(size == TEST_RECORD_SIZE);
    record_check(record, id, seed);
    free(record);
}

static uint32_t insert_record(table_t *this, const int id) {
    unsigned char record[TEST_RECORD_SIZE];
    uint32_t rid = UINT32_MAX;
    record_fill(record, id, id);
    assert(table_insert(this, record, sizeof(record), &rid) == TABLE_OK);
    return rid;
}

static table_t* create_table(const char *name) {
    static const char *key_name[] = {"id", "payload"};
    static const char *fmt[] = {"i", "s12"};
    table_t *this = NULL;

    assert(table_create(&this, TEST_DB, name, 2, key_name, fmt)
            == TABLE_OK);
    assert(this != NULL);
    return this;
}

static void test_empty_table(void) {
    fs_start("empty");
    table_t *this = create_table("empty");
    assert(this->head.page_count == 0);

    table_scan_t *scan = NULL;
    assert(table_scan_open(this, &scan) == TABLE_OK);

    uint32_t rid = 0;
    void *record = NULL;
    size_t size = 0;
    assert(table_scan_next(scan, &rid, &record, &size) == TABLE_SCAN_END);
    table_scan_close(scan);

    unsigned char buffer[TEST_RECORD_SIZE];
    record_fill(buffer, 0, 0);
    assert(table_get(this, 0, &record, &size) == TABLE_ERR_RID);
    assert(table_update(this, 0, buffer, sizeof(buffer)) == TABLE_ERR_RID);
    assert(table_delete(this, 0) == TABLE_ERR_RID);
    assert(this->head.page_count == 0);

    table_close(this);
    fs_finish("empty");
}

static void test_many_records(void) {
    fs_start("many");
    table_t *this = create_table("many");
    assert(table_get_slots_per_page(&this->head) == TABLE_SLOT_MAX);

    for(uint32_t i = 0; i < MANY_N; ++i)
        assert(insert_record(this, (int)i) == i);

    assert(this->head.page_count ==
            (MANY_N + TABLE_SLOT_MAX - 1) / TABLE_SLOT_MAX);

    for(uint32_t i = 0; i < MANY_N; ++i)
        check_record(this, i, (int)i, (int)i);

    table_close(this);
    fs_finish("many");
}

static void test_full_slot_bitmap(void) {
    fs_start("bitmap");
    table_t *this = create_table("bitmap");

    for(uint32_t i = 0; i < TABLE_SLOT_MAX; ++i)
        assert(insert_record(this, (int)i) == i);
    assert(this->head.page_count == 1);

    /* The first page must have all 64 bits set, including bit 63. */
    char *path = table_path("bitmap");
    fs_datablock_t *block = fs_borrow(path, PAGE_SIZE, PAGE_SIZE);
    uint64_t bitmap;
    memcpy(&bitmap, block->ptr, sizeof(bitmap));
    assert(bitmap == ~(uint64_t)0);
    fs_return(block);

    check_record(this, TABLE_SLOT_MAX - 1, TABLE_SLOT_MAX - 1,
            TABLE_SLOT_MAX - 1);

    assert(insert_record(this, TABLE_SLOT_MAX) == TABLE_SLOT_MAX);
    assert(this->head.page_count == 2);

    path = table_path("bitmap");
    block = fs_borrow(path, (size_t)2 * PAGE_SIZE, PAGE_SIZE);
    memcpy(&bitmap, block->ptr, sizeof(bitmap));
    assert(bitmap == (uint64_t)1);
    fs_return(block);

    check_record(this, TABLE_SLOT_MAX, TABLE_SLOT_MAX, TABLE_SLOT_MAX);
    table_close(this);
    fs_finish("bitmap");
}

static void test_update_in_place(void) {
    fs_start("update");
    table_t *this = create_table("update");

    for(int i = 0; i < 3; ++i)
        assert(insert_record(this, i) == (uint32_t)i);
    uint32_t pages = this->head.page_count;

    unsigned char buffer[TEST_RECORD_SIZE];
    record_fill(buffer, 42, 99);
    assert(table_update(this, 1, buffer, sizeof(buffer)) == TABLE_OK);

    check_record(this, 0, 0, 0);
    check_record(this, 1, 42, 99);
    check_record(this, 2, 2, 2);
    assert(this->head.page_count == pages);

    assert(table_update(this, 1, buffer, sizeof(buffer) - 1)
            == TABLE_ERR_INVALID);

    table_close(this);
    fs_finish("update");
}

static void test_delete_edges(void) {
    fs_start("delete");
    table_t *this = create_table("delete");

    for(int i = 0; i < 5; ++i)
        assert(insert_record(this, i) == (uint32_t)i);
    uint32_t pages = this->head.page_count;

    assert(table_delete(this, 0) == TABLE_OK);
    assert(table_delete(this, 2) == TABLE_OK);
    assert(table_delete(this, 4) == TABLE_OK);
    assert(table_delete(this, 2) == TABLE_ERR_RID);
    assert(this->head.page_count == pages);

    uint32_t dead[] = {0, 2, 4};
    unsigned char buffer[TEST_RECORD_SIZE];
    record_fill(buffer, 9, 9);
    for(size_t i = 0; i < sizeof(dead) / sizeof(dead[0]); ++i) {
        void *record = NULL;
        size_t size = 0;
        assert(table_get(this, dead[i], &record, &size)
                == TABLE_ERR_RID);
        assert(table_update(this, dead[i], buffer, sizeof(buffer))
                == TABLE_ERR_RID);
        assert(table_delete(this, dead[i]) == TABLE_ERR_RID);
    }

    check_record(this, 1, 1, 1);
    check_record(this, 3, 3, 3);

    table_close(this);
    fs_finish("delete");
}

static void test_hole_reuse(void) {
    fs_start("reuse");
    table_t *this = create_table("reuse");

    for(int i = 0; i < 65; ++i)
        assert(insert_record(this, i) == (uint32_t)i);
    assert(this->head.page_count == 2);
    assert(this->head.free_page_hint == 2);
    uint32_t pages = this->head.page_count;

    /* A hole on page 1 must pull the hint back and avoid growing the file. */
    assert(table_delete(this, 10) == TABLE_OK);
    assert(this->head.free_page_hint == 1);
    assert(insert_record(this, 1000) == 10);
    assert(this->head.page_count == pages);
    check_record(this, 10, 1000, 1000);

    /* The same works for a hole on the last page. */
    assert(table_delete(this, 64) == TABLE_OK);
    assert(this->head.free_page_hint == 2);
    assert(insert_record(this, 1001) == 64);
    assert(this->head.page_count == pages);
    check_record(this, 64, 1001, 1001);

    table_close(this);
    fs_finish("reuse");
}

static void test_scan_skips_holes(void) {
    fs_start("scan");
    table_t *this = create_table("scan");

    for(int i = 0; i < SCAN_N; ++i)
        assert(insert_record(this, i) == (uint32_t)i);

    bool deleted[SCAN_N];
    memset(deleted, 0, sizeof(deleted));
    for(int i = 0; i < SCAN_N; i += 3) {
        assert(table_delete(this, (uint32_t)i) == TABLE_OK);
        deleted[i] = true;
    }
    assert(table_delete(this, SCAN_N - 1) == TABLE_OK);
    deleted[SCAN_N - 1] = true;

    table_scan_t *scan = NULL;
    assert(table_scan_open(this, &scan) == TABLE_OK);

    bool seen[SCAN_N];
    memset(seen, 0, sizeof(seen));
    bool first = true;
    uint32_t previous = 0;
    uint32_t count = 0;

    for(;;) {
        uint32_t rid = 0;
        void *record = NULL;
        size_t size = 0;
        table_status_t status = table_scan_next(scan, &rid, &record, &size);
        if(status == TABLE_SCAN_END) break;

        assert(status == TABLE_OK);
        assert(rid < SCAN_N);
        assert(!deleted[rid]);
        assert(!seen[rid]);
        if(!first) assert(rid > previous);
        first = false;
        previous = rid;
        seen[rid] = true;
        ++count;

        assert(size == TEST_RECORD_SIZE);
        record_check(record, (int)rid, (int)rid);
        free(record);
    }
    table_scan_close(scan);

    uint32_t live = 0;
    for(int i = 0; i < SCAN_N; ++i) {
        if(deleted[i]) continue;
        ++live;
        assert(seen[i]);
    }
    assert(count == live);

    table_close(this);
    fs_finish("scan");
}

static void verify_persist(void) {
    table_t *this = NULL;
    assert(table_open(&this, TEST_DB, "persist") == TABLE_OK);
    assert(this->head.page_count == 3);
    assert(this->head.free_page_hint == 2);

    for(uint32_t rid = 0; rid < PERSIST_N; ++rid) {
        if(rid == PERSIST_DELETED) {
            void *record = NULL;
            size_t size = 0;
            assert(table_get(this, rid, &record, &size)
                    == TABLE_ERR_RID);
            continue;
        }
        check_record(this, rid, (int)rid, (int)rid);
    }

    table_close(this);
}

static void test_restart_persistence(void) {
    fs_start("persist");
    table_t *this = create_table("persist");

    for(int i = 0; i < PERSIST_N; ++i)
        assert(insert_record(this, i) == (uint32_t)i);
    assert(this->head.page_count == 3);
    assert(this->head.free_page_hint == 3);

    assert(table_delete(this, PERSIST_DELETED) == TABLE_OK);
    assert(this->head.free_page_hint == 2);
    table_close(this);
    fs_destroy();

    /* Same-process reopen. */
    fs_init();
    verify_persist();
    fs_destroy();

    /* Separate-process restart. */
    pid_t pid = fork();
    assert(pid >= 0);
    if(pid == 0) {
        fs_init();
        verify_persist();
        fs_destroy();
        exit(0);
    }

    int status = 0;
    assert(waitpid(pid, &status, 0) == pid);
    assert(WIFEXITED(status));
    assert(WEXITSTATUS(status) == 0);

    fs_init();
    fs_remove(table_path("persist"));
    fs_destroy();
}

static void test_missing_open(void) {
    fs_start("missing");
    assert(table_is_missing("missing"));

    table_t *this = NULL;
    assert(table_open(&this, TEST_DB, "missing") == TABLE_ERR_NOT_FOUND);
    assert(this == NULL);
    assert(table_is_missing("missing"));

    fs_finish("missing");
}

static void test_duplicate_create(void) {
    fs_start("dup");
    table_t *this = create_table("dup");
    assert(insert_record(this, 7) == 0);

    off_t size = file_size("dup");
    uint32_t pages = this->head.page_count;
    uint32_t hint = this->head.free_page_hint;
    table_close(this);

    static const char *key_name[] = {"only"};
    static const char *fmt[] = {"s64"};
    table_t *duplicate = NULL;
    assert(table_create(&duplicate, TEST_DB, "dup", 1, key_name, fmt)
            == TABLE_ERR_EXISTS);
    assert(duplicate == NULL);
    assert(file_size("dup") == size);

    assert(table_open(&this, TEST_DB, "dup") == TABLE_OK);
    assert(this->head.m == 2);
    assert(this->head.page_count == pages);
    assert(this->head.free_page_hint == hint);
    check_record(this, 0, 7, 7);
    table_close(this);

    fs_finish("dup");
}

static void test_out_of_range_rid(void) {
    fs_start("bounds");
    table_t *this = create_table("bounds");
    uint32_t slots = table_get_slots_per_page(&this->head);

    off_t empty_size = file_size("bounds");
    assert(empty_size == PAGE_SIZE);

    void *record = NULL;
    size_t size = 0;
    unsigned char buffer[TEST_RECORD_SIZE];
    record_fill(buffer, 1, 1);

    assert(table_get(this, 0, &record, &size) == TABLE_ERR_RID);
    assert(table_update(this, 0, buffer, sizeof(buffer)) == TABLE_ERR_RID);
    assert(table_delete(this, 0) == TABLE_ERR_RID);
    assert(file_size("bounds") == empty_size);
    assert(this->head.page_count == 0);

    assert(insert_record(this, 1) == 0);
    off_t one_page_size = file_size("bounds");
    assert(this->head.page_count == 1);

    uint32_t bad[] = {slots, slots * 10, UINT32_MAX};
    for(size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        assert(table_get(this, bad[i], &record, &size) == TABLE_ERR_RID);
        assert(table_update(this, bad[i], buffer, sizeof(buffer))
                == TABLE_ERR_RID);
        assert(table_delete(this, bad[i]) == TABLE_ERR_RID);
        assert(file_size("bounds") == one_page_size);
        assert(this->head.page_count == 1);
    }

    /* An allocated page with a free slot is still an invalid target. */
    assert(table_get(this, 1, &record, &size) == TABLE_ERR_RID);
    assert(table_update(this, 1, buffer, sizeof(buffer)) == TABLE_ERR_RID);
    assert(table_delete(this, 1) == TABLE_ERR_RID);
    assert(file_size("bounds") == one_page_size);
    assert(this->head.page_count == 1);

    table_close(this);
    fs_finish("bounds");
}

int main(void) {
    test_empty_table();
    test_many_records();
    test_full_slot_bitmap();
    test_update_in_place();
    test_delete_edges();
    test_hole_reuse();
    test_scan_skips_holes();
    test_restart_persistence();
    test_missing_open();
    test_duplicate_create();
    test_out_of_range_rid();
    return 0;
}