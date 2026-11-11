/* elem_type.c
 *      Dynamic type for elements
 */

#include <ctype.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "elem_type.h"

#define MAX_ELEM_N 2048

const char ELEM_TYPE_D = 'd'; /* double */
const char ELEM_TYPE_I = 'i'; /* int32_t */
const char ELEM_TYPE_S = 's'; /* char[len] */

static int _elem_read_int(char **p) {
    int ret = 0;
    while(isdigit(*(++(*p)))) 
        ret = ret * 10 + ((**p) ^ 48);
    --(*p); /* let the caller's ++p reach the next type character */
    return ret;
}

void elem_type_init(elem_type_t *this, const char* fmt) {
    static size_t sz[MAX_ELEM_N];

    this->n = 0;
    size_t len = strlen(fmt);
    this->fmt = (char*)malloc(sizeof(char) * (len + 1));
    strcpy(this->fmt, fmt);

    for(char *p = (char*)fmt; *p; p++) {
        switch(*p) {
        case 'i': sz[this->n++] = sizeof(int32_t); break; 
        case 'd': sz[this->n++] = sizeof(double); break;
        case 's': sz[this->n++] = _elem_read_int(&p); break;
        default: perror("Invalid fmt"); exit(-1); 
        }
    }

    /* offset_presum[k] is the offset of element k, offset_presum[n] the
     * total size of all elements. */
    this->offset_presum = (size_t*)malloc(sizeof(size_t) * (this->n + 1));
    this->offset_presum[0] = 0;
    for(uint32_t i = 0; i < this->n; ++i) 
        this->offset_presum[i + 1] = this->offset_presum[i] + sz[i];
}

void elem_type_save(elem_type_t *this, char* fmt) {
    strcpy(fmt, this->fmt);
}

void elem_type_destory(elem_type_t *this) {
    free(this->fmt); free(this->offset_presum);
}

size_t elem_type_sizeof(elem_type_t *this) {
    return this->offset_presum[this->n];
}

size_t elem_type_total_n(elem_type_t *this) { return this->n; }

void* elem_type_kth(elem_type_t *this, void* start_ptr, const uint32_t kth) {
    return (char*)start_ptr + this->offset_presum[kth];
}
