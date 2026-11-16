/* elem_type.c
 *      Scalar type helpers; inputs are trusted
 */

#include <stdint.h>
#include <stdlib.h>

#include "elem_type.h"

int elem_type_init(elem_type_t *this, const char *fmt) {
    this->kind = fmt[0];
    switch(this->kind) {
    case ELEM_TYPE_I: this->size = sizeof(int32_t); break;
    case ELEM_TYPE_D: this->size = sizeof(double); break;
    case ELEM_TYPE_S: this->size = (uint32_t)strtoul(fmt + 1, NULL, 10); break;
    }
    return 0;
}

size_t elem_type_sizeof(const elem_type_t *this) {
    return this->size;
}
