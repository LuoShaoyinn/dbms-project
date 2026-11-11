/* elem_type.h
 *      Dynamic type for elements
 */

#ifndef _H_ELEM_TYPE
#define _H_ELEM_TYPE

#include <stddef.h>
#include <stdint.h>

extern const char ELEM_TYPE_D; /* double */
extern const char ELEM_TYPE_I; /* int32_t */
extern const char ELEM_TYPE_S; /* char[len] */

typedef struct _elem_type_s {
    uint32_t n;
    size_t *offset_presum;
    char* fmt;
} elem_type_t;

void elem_type_init(elem_type_t *this, const char* fmt);
void elem_type_save(elem_type_t *this, char* fmt);
void elem_type_destory(elem_type_t *this);
size_t elem_type_sizeof(elem_type_t *this);
size_t elem_type_total_n(elem_type_t *this);
void* elem_type_kth(elem_type_t *this, void* start_ptr, const uint32_t kth);

#endif // _H_ELEM_TYPE
