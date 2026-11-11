/* test_elem_type.c
 *      isolated tests for backend/elem_type
 */

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "elem_type/elem_type.h"

static void test_single_int(void) {
    elem_type_t et;
    char record[16] = {0};
    char saved[16] = {0};
    int32_t value = -12345, read = 0;

    elem_type_init(&et, "i");
    assert(elem_type_total_n(&et) == 1);
    assert(elem_type_sizeof(&et) == sizeof(int32_t));

    int32_t *p_int = (int32_t*)elem_type_kth(&et, record, 0);
    assert((char*)p_int == record);
    memcpy(p_int, &value, sizeof value);
    memcpy(&read, record, sizeof read);
    assert(read == value);

    elem_type_save(&et, saved);
    assert(strcmp(saved, "i") == 0);

    elem_type_destory(&et);
}

static void test_single_double(void) {
    elem_type_t et;
    char record[16] = {0};
    double value = 3.25, read = 0;

    elem_type_init(&et, "d");
    assert(elem_type_total_n(&et) == 1);
    assert(elem_type_sizeof(&et) == sizeof(double));

    double *p_double = (double*)elem_type_kth(&et, record, 0);
    assert((char*)p_double == record);
    memcpy(p_double, &value, sizeof value);
    memcpy(&read, record, sizeof read);
    assert(read == value);

    elem_type_destory(&et);
}

static void test_string_length(void) {
    elem_type_t et;
    char saved[16] = {0};

    elem_type_init(&et, "s32");
    assert(elem_type_total_n(&et) == 1);
    assert(elem_type_sizeof(&et) == 32);

    elem_type_save(&et, saved);
    assert(strcmp(saved, "s32") == 0);

    elem_type_destory(&et);
}

static void test_mixed_offsets(void) {
    elem_type_t et;
    char record[32] = {0};
    char saved[32] = {0};
    int32_t value_int = 30240262, read_int = 0;
    double value_double = 2.5, read_double = 0;

    /* int32_t, double, char[8] -> offsets 0, 4, 12; total 20. */
    elem_type_init(&et, "ids8");
    assert(elem_type_total_n(&et) == 3);
    assert(elem_type_sizeof(&et) == sizeof(int32_t) + sizeof(double) + 8);

    int32_t *p_int = (int32_t*)elem_type_kth(&et, record, 0);
    double *p_double = (double*)elem_type_kth(&et, record, 1);
    char *p_string = (char*)elem_type_kth(&et, record, 2);

    assert((char*)p_int == record);
    assert((char*)p_double == record + sizeof(int32_t));
    assert(p_string == record + sizeof(int32_t) + sizeof(double));

    /* Records are packed, so a double may be unaligned. */
    memcpy(p_int, &value_int, sizeof value_int);
    memcpy(p_double, &value_double, sizeof value_double);
    memcpy(p_string, "database", 8);

    memcpy(&read_int, record, sizeof read_int);
    memcpy(&read_double, record + sizeof(int32_t), sizeof read_double);
    assert(read_int == value_int);
    assert(read_double == value_double);
    assert(memcmp(record + 12, "database", 8) == 0);

    elem_type_save(&et, saved);
    assert(strcmp(saved, "ids8") == 0);

    elem_type_destory(&et);
}

static void test_multiple_strings(void) {
    elem_type_t et;
    char record[32] = {0};

    /* Two adjacent string columns: offsets 0 and 4, total 12. */
    elem_type_init(&et, "s4s8");
    assert(elem_type_total_n(&et) == 2);
    assert(elem_type_sizeof(&et) == 12);

    char *p_first = (char*)elem_type_kth(&et, record, 0);
    char *p_second = (char*)elem_type_kth(&et, record, 1);
    assert(p_first == record);
    assert(p_second == record + 4);

    memcpy(p_first, "abcd", 4);
    memcpy(p_second, "12345678", 8);
    assert(memcmp(record, "abcd", 4) == 0);
    assert(memcmp(record + 4, "12345678", 8) == 0);

    elem_type_destory(&et);
}

static void test_large_string_length(void) {
    elem_type_t et;
    char record[160] = {0};

    /* Multi-digit length must be parsed completely. */
    elem_type_init(&et, "is128d");
    assert(elem_type_total_n(&et) == 3);
    assert(elem_type_sizeof(&et) == sizeof(int32_t) + 128 + sizeof(double));

    int32_t *p_int = (int32_t*)elem_type_kth(&et, record, 0);
    char *p_string = (char*)elem_type_kth(&et, record, 1);
    double *p_double = (double*)elem_type_kth(&et, record, 2);

    assert((char*)p_int == record);
    assert(p_string == record + sizeof(int32_t));
    assert((char*)p_double == record + sizeof(int32_t) + 128);

    elem_type_destory(&et);
}

static void test_max_columns(void) {
    elem_type_t et;

    /* MAX_COLUMN columns of int32_t: 8 * 4 = 32 bytes. */
    elem_type_init(&et, "iiiiiiii");
    assert(elem_type_total_n(&et) == 8);
    assert(elem_type_sizeof(&et) == 8 * sizeof(int32_t));

    for(uint32_t i = 0; i < 8; ++i) {
        char *record = (char*)0x1000;
        assert((char*)elem_type_kth(&et, record, i) == record + i * sizeof(int32_t));
    }

    elem_type_destory(&et);
}

static void test_format_is_copied(void) {
    elem_type_t et;
    char fmt[] = "ids8";
    char saved[16] = {0};

    /* The format string belongs to the caller and may be reused. */
    elem_type_init(&et, fmt);
    fmt[0] = 's';
    elem_type_save(&et, saved);
    assert(strcmp(saved, "ids8") == 0);

    elem_type_destory(&et);
}

static void test_type_constants(void) {
    assert(ELEM_TYPE_D == 'd');
    assert(ELEM_TYPE_I == 'i');
    assert(ELEM_TYPE_S == 's');
}

int main(void) {
    test_type_constants();
    test_single_int();
    test_single_double();
    test_string_length();
    test_mixed_offsets();
    test_multiple_strings();
    test_large_string_length();
    test_max_columns();
    test_format_is_copied();
    return 0;
}