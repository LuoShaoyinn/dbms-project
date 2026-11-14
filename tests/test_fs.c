/* test_fs.c
 *      isolated tests for backend/fs
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "constant.h"
#include "fs/fs.h"

#define TEST_STRESS_PAGES 300
#define TEST_RECREATE_PAGES 8

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
                (size_t)i * PAGE_SIZE, sizeof(int));
        assert(blk != NULL);
        *(int*)blk->ptr = i;
        fs_return(blk);
    }
    for(int i = 0; i < 16; ++i) {
        fs_datablock_t *blk = borrow_table("testdb", "pages",
                (size_t)i * PAGE_SIZE, sizeof(int));
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
                (size_t)i * PAGE_SIZE, sizeof(int));
        assert(blk != NULL);
        *(int*)blk->ptr = i;
        fs_return(blk);
    }
    for(int i = 0; i < TEST_STRESS_PAGES; ++i) {
        fs_datablock_t *blk = borrow_table("testdb", "stress",
                (size_t)i * PAGE_SIZE, sizeof(int));
        assert(blk != NULL);
        assert(*(int*)blk->ptr == i);
        fs_return(blk);
    }

    fs_remove(fs_name_db_table("testdb", "stress"));
    fs_destroy();
}

static void test_remove_recreate(void) {
    ensure_dirs();
    fs_init();

    /* Drop anything left behind by an earlier run. */
    fs_remove(fs_name_db_table("testdb", "recreate"));

    fs_datablock_t *blk = borrow_table("testdb", "recreate", 0, sizeof(int));
    assert(blk != NULL);
    *(int*)blk->ptr = 0x11223344;
    fs_return(blk);

    assert(fs_remove(fs_name_db_table("testdb", "recreate")) == 0);

    /* The recreated file must not expose the deleted file's mapping. */
    blk = borrow_table("testdb", "recreate", 0, sizeof(int));
    assert(blk != NULL);
    assert(*(int*)blk->ptr == 0);
    *(int*)blk->ptr = 0x55667788;
    fs_return(blk);

    assert(fs_remove(fs_name_db_table("testdb", "recreate")) == 0);
    fs_destroy();
}

static void test_remove_recreate_multiple_pages(void) {
    ensure_dirs();
    fs_init();

    fs_remove(fs_name_db_table("testdb", "recreate_pages"));

    for(int i = 0; i < TEST_RECREATE_PAGES; ++i) {
        fs_datablock_t *blk = borrow_table("testdb", "recreate_pages",
                (size_t)i * PAGE_SIZE, sizeof(int));
        assert(blk != NULL);
        *(int*)blk->ptr = 0x1000 + i;
        fs_return(blk);
    }

    assert(fs_remove(fs_name_db_table("testdb", "recreate_pages")) == 0);

    for(int i = 0; i < TEST_RECREATE_PAGES; ++i) {
        fs_datablock_t *blk = borrow_table("testdb", "recreate_pages",
                (size_t)i * PAGE_SIZE, sizeof(int));
        assert(blk != NULL);
        assert(*(int*)blk->ptr == 0);
        *(int*)blk->ptr = 0x2000 + i;
        fs_return(blk);
    }

    assert(fs_remove(fs_name_db_table("testdb", "recreate_pages")) == 0);
    fs_destroy();
}

static void test_remove_borrowed_page(void) {
    ensure_dirs();
    fs_init();

    fs_remove(fs_name_db_table("testdb", "borrowed"));

    fs_datablock_t *old = borrow_table("testdb", "borrowed", 0, sizeof(int));
    assert(old != NULL);
    *(int*)old->ptr = 0x12345678;

    /* Deletion succeeds while borrowed: the unlinked inode stays alive. */
    assert(fs_remove(fs_name_db_table("testdb", "borrowed")) == 0);
    assert(*(int*)old->ptr == 0x12345678);

    /* A new file with the same name gets a different inode and fresh pages. */
    fs_datablock_t *fresh = borrow_table("testdb", "borrowed", 0, sizeof(int));
    assert(fresh != NULL);
    assert(fresh->_ref_fs_page != old->_ref_fs_page);
    assert(*(int*)fresh->ptr == 0);
    *(int*)fresh->ptr = 0x87654321;

    fs_return(fresh);
    fs_return(old);

    assert(fs_remove(fs_name_db_table("testdb", "borrowed")) == 0);
    fs_destroy();
}

static void test_remove_borrowed_evicted_page(void) {
    ensure_dirs();
    fs_init();

    fs_remove(fs_name_db_table("testdb", "evicted"));
    fs_remove(fs_name_db_table("testdb", "evicted_filler"));

    fs_datablock_t *old = borrow_table("testdb", "evicted", 0, sizeof(int));
    assert(old != NULL);
    *(int*)old->ptr = 0x0badf00d;

    /* Push the borrowed page out of the LRU, leaving it only borrowed. */
    for(int i = 0; i <= FS_LRU_MAXSIZE; ++i) {
        fs_datablock_t *filler = borrow_table("testdb", "evicted_filler",
                (size_t)i * PAGE_SIZE, sizeof(int));
        assert(filler != NULL);
        *(int*)filler->ptr = i;
        fs_return(filler);
    }

    /* Deletion succeeds and the evicted-but-borrowed mapping stays valid. */
    assert(fs_remove(fs_name_db_table("testdb", "evicted")) == 0);
    assert(*(int*)old->ptr == 0x0badf00d);

    fs_datablock_t *fresh = borrow_table("testdb", "evicted", 0, sizeof(int));
    assert(fresh != NULL);
    assert(fresh->_ref_fs_page != old->_ref_fs_page);
    assert(*(int*)fresh->ptr == 0);

    fs_return(fresh);
    fs_return(old);

    assert(fs_remove(fs_name_db_table("testdb", "evicted")) == 0);

    fs_remove(fs_name_db_table("testdb", "evicted_filler"));
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
    test_remove_recreate();
    test_remove_recreate_multiple_pages();
    test_remove_borrowed_page();
    test_remove_borrowed_evicted_page();
    return 0;
}