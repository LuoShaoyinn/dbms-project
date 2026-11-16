/* elem_type.h
 *      Pointer-free descriptor for one scalar value
 */

#ifndef _H_ELEM_TYPE
#define _H_ELEM_TYPE

#include <stddef.h>
#include <stdint.h>

#define ELEM_TYPE_D 'd' /* double */
#define ELEM_TYPE_I 'i' /* int32_t */
#define ELEM_TYPE_S 's' /* fixed-width string */

typedef struct _elem_type_s {
    char kind;
    uint32_t size;
} elem_type_t;

/* Parse a trusted scalar format ("i", "d", "s<n>") once. No allocations. */
int elem_type_init(elem_type_t *this, const char *fmt);
size_t elem_type_sizeof(const elem_type_t *this);

#endif // _H_ELEM_TYPE
