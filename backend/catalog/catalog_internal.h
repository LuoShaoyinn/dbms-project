/* catalog_internal.h
 *      Helpers shared by catalog.c and catalog_table.c
 *
 * Not part of the public catalog API.
 */

#ifndef _H_CATALOG_INTERNAL
#define _H_CATALOG_INTERNAL

#include <stdbool.h>

#include "constant.h"
#include "catalog/catalog.h"

/* Identifier rules shared by database and table names. */
bool _catalog_name_ok(const char *name);

/* Path builders. Each returns 0 on success, -1 when out[] is too small. */
int _catalog_sub_path(char *out, const char *db_name, const char *sub);
int _catalog_child_path(char *out, const char *dir, const char *name);

/* Duplicate a path into a malloc() buffer owned by the caller. The fs_*
 * functions consume such buffers, so every call needs a fresh copy. */
char* _catalog_dup_path(const char *path);

#endif // _H_CATALOG_INTERNAL