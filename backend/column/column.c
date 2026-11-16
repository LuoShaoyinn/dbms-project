/* column.c
 *      Column metadata helpers
 */

#include "column.h"

uint32_t column_length(const column_t *this) {
    return this->type.kind == ELEM_TYPE_S ? this->type.size : 0;
}

size_t column_size(const column_t *this) {
    return elem_type_sizeof(&this->type);
}

const char* column_type_name(const column_t *this) {
    switch(this->type.kind) {
    case ELEM_TYPE_I: return "INT";
    case ELEM_TYPE_D: return "FLOAT";
    default: return "VARCHAR";
    }
}
