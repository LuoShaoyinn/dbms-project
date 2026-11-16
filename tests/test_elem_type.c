/* test_elem_type.c: scalar descriptors have no allocations or pointers */
#include <assert.h>
#include <stdint.h>

#include "elem_type/elem_type.h"

int main(void) {
    elem_type_t type;
    assert(elem_type_init(&type, "i") == 0);
    assert(type.kind == ELEM_TYPE_I);
    assert(elem_type_sizeof(&type) == sizeof(int32_t));
    assert(elem_type_init(&type, "d") == 0);
    assert(type.kind == ELEM_TYPE_D);
    assert(elem_type_sizeof(&type) == sizeof(double));
    assert(elem_type_init(&type, "s2048") == 0);
    assert(type.kind == ELEM_TYPE_S);
    assert(elem_type_sizeof(&type) == 2048);

    char fmt[] = "s128";
    assert(elem_type_init(&type, fmt) == 0);
    fmt[1] = '9';
    assert(type.size == 128); /* no dependence on the source format */
    elem_type_t copy = type;
    assert(copy.kind == ELEM_TYPE_S && copy.size == 128);
    return 0;
}
