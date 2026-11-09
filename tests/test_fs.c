/* test_fs.c
 *      isolated tests for backend/fs
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "fs/fs.h"

#define TEST_PAGE_SIZE 4096
#define TEST_STRESS_PAGES 300

static void ensure_dirs(void) {
    mkdir("data", 0755);
    mkdir("data/db", 0755);
    mkdir("data/db/testdb", 0755);
    mkdir("data/db/testdb/table", 0755);
    mkdir("data/db/testdb/index", 0755);
}

static fs_datablock_t* borrow_table(const char* db, const char* table,
        const size_t offset, const size_t sz) {
    return fs_borrow(fs_name_db_table(db, table), offset, sz);
}

static void test_persist_after_restart(void) {
    ensure_dirs();

    fs_init();
    fs_datablock_t *blk = borrow_table("testdb", "persist", 0, sizeof(int));
    assert(blk != NULL);
    assert(blk->ptr != NULL);
    *(int*)blk->ptr = 0x5a5a1234;
    fs_return(blk);
    fs_destroy();

    /* A fresh mapping must see the data written through mmap. */
    fs_init();
    blk = borrow_table("testdb", "persist", 0, sizeof(int));
    assert(blk != NULL);
    assert(*(int*)blk->ptr == 0x5a5a1234);
    fs_return(blk);

    fs_remove(fs_name_db_table("testdb", "persist"));
    fs_destroy();
}

static void test_pages_and_offsets(void) {
    ensure_dirs();

    fs_init();
    for(int i = 0; i < 16; ++i) {
        fs_datablock_t *blk = borrow_table("testdb", "pages",
                (size_t)i * TEST_PAGE_SIZE, sizeof(int));
        assert(blk != NULL);
        *(int*)blk->ptr = i;
        fs_return(blk);
    }
    for(int i = 0; i < 16; ++i) {
        fs_datablock_t *blk = borrow_table("testdb", "pages",
                (size_t)i * TEST_PAGE_SIZE, sizeof(int));
        assert(blk != NULL);
        assert(*(int*)blk->ptr == i);
        fs_return(blk);
    }

    fs_remove(fs_name_db_table("testdb", "pages"));
    fs_destroy();
}

static void test_ref_count(void) {
    ensure_dirs();

    fs_init();
    fs_datablock_t *blk1 = borrow_table("testdb", "refcnt", 0, sizeof(int));
    assert(blk1 != NULL);
    *(int*)blk1->ptr = 77;

    fs_datablock_t *blk2 = borrow_table("testdb", "refcnt", 0, sizeof(int));
    assert(blk2 != NULL);
    assert(blk2->_ref_fs_page == blk1->_ref_fs_page);
    assert(*(int*)blk2->ptr == 77);

    fs_return(blk1);
    assert(*(int*)blk2->ptr == 77); /* still borrowed by blk2 */

    fs_return(blk2);
    fs_datablock_t *blk3 = borrow_table("testdb", "refcnt", 0, sizeof(int));
    assert(blk3 != NULL);
    assert(*(int*)blk3->ptr == 77);
    fs_return(blk3);

    fs_remove(fs_name_db_table("testdb", "refcnt"));
    fs_destroy();
}

static void test_eviction_stress(void) {
    ensure_dirs();

    fs_init();
    for(int i = 0; i < TEST_STRESS_PAGES; ++i) {
        fs_datablock_t *blk = borrow_table("testdb", "stress",
                (size_t)i * TEST_PAGE_SIZE, sizeof(int));
        assert(blk != NULL);
        *(int*)blk->ptr = i;
        fs_return(blk);
    }
    for(int i = 0; i < TEST_STRESS_PAGES; ++i) {
        fs_datablock_t *blk = borrow_table("testdb", "stress",
                (size_t)i * TEST_PAGE_SIZE, sizeof(int));
        assert(blk != NULL);
        assert(*(int*)blk->ptr == i);
        fs_return(blk);
    }

    fs_remove(fs_name_db_table("testdb", "stress"));
    fs_destroy();
}

static void test_index_name(void) {
    char* name = fs_name_db_index("testdb", "idx");
    assert(name != NULL);
    assert(strstr(name, "data/db/testdb/index/idx") != NULL);
    free(name);
}

int main(void) {
    test_index_name();
    test_persist_after_restart();
    test_pages_and_offsets();
    test_ref_count();
    test_eviction_stress();
    return 0;
}