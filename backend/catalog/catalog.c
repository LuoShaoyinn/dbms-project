/* catalog.c: stateless management; fs owns all path/directory handling */
#include "fs/fs.h"
#include "table/table.h"
#include "catalog.h"

bool catalog_db_exists(const char *db_name) {
    return fs_exists(fs_name_db(db_name));
}

int catalog_create_db(const char *db_name) {
    if(fs_mkdir(fs_name_db(db_name)) != 0) return -1;
    if(fs_mkdir(fs_name_db_table(db_name, "")) != 0) {
        fs_rmdir(fs_name_db(db_name));
        return -1;
    }
    if(fs_mkdir(fs_name_db_index(db_name, "")) != 0) {
        fs_rmdir(fs_name_db_table(db_name, ""));
        fs_rmdir(fs_name_db(db_name));
        return -1;
    }
    return 0;
}

int catalog_drop_db(const char *db_name) {
    if(fs_clear_dir(fs_name_db_table(db_name, "")) != 0) return -1;
    if(fs_clear_dir(fs_name_db_index(db_name, "")) != 0) return -1;
    if(fs_rmdir(fs_name_db_table(db_name, "")) != 0) return -1;
    if(fs_rmdir(fs_name_db_index(db_name, "")) != 0) return -1;
    return fs_rmdir(fs_name_db(db_name));
}

int catalog_list_dbs(char names[][MAX_DB_NAME_LENGTH], size_t *count) {
    return fs_list(fs_name_db(""), true, names, count);
}

int catalog_list_tables(const char *db_name,
        char names[][MAX_DB_NAME_LENGTH], size_t *count) {
    return fs_list(fs_name_db_table(db_name, ""), false, names, count);
}

int catalog_create_table(const char *db_name, const char *table_name,
        uint32_t m, const column_t *columns, const char *const *defaults) {
    table_t *table;
    if(table_create(&table, db_name, table_name, m, columns, defaults) != 0)
        return -1;
    table_close(table);
    return 0;
}

int catalog_drop_table(const char *db_name, const char *table_name) {
    return fs_remove(fs_name_db_table(db_name, table_name));
}
