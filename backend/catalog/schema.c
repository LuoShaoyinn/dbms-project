/* schema.c
 *      Logical schema validation and serialization
 */

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constant.h"
#include "schema.h"

#define SCHEMA_MAGIC    0x5343484Du     /* "SCHM" */
#define SCHEMA_VERSION  1u

void* _schema_xmalloc(const size_t size) {
    void *ptr = malloc(size);
    if(ptr == NULL) {
        perror("schema: malloc");
        exit(-1);
    }
    return ptr;
}

void* _schema_xcalloc(const size_t count, const size_t size) {
    void *ptr = calloc(count, size);
    if(ptr == NULL) {
        perror("schema: calloc");
        exit(-1);
    }
    return ptr;
}

static char* _schema_strdup(const char *value) {
    size_t len = strlen(value) + 1;
    char *copy = (char*)_schema_xmalloc(len);
    memcpy(copy, value, len);
    return copy;
}

static int _schema_name_ok(const char *name) {
    if(name == NULL) return 0;

    size_t len = strlen(name);
    if(len == 0 || len >= MAX_KEY_NAME_LENGTH) return 0;

    unsigned char first = (unsigned char)name[0];
    if(!isalpha(first) && first != '_') return 0;
    for(size_t i = 1; i < len; ++i) {
        unsigned char c = (unsigned char)name[i];
        if(!isalnum(c) && c != '_') return 0;
    }
    return 1;
}

static int _schema_name_equal(const char *a, const char *b) {
    while(*a != '\0' && *b != '\0') {
        unsigned char ca = (unsigned char)tolower((unsigned char)*a);
        unsigned char cb = (unsigned char)tolower((unsigned char)*b);
        if(ca != cb) return 0;
        ++a;
        ++b;
    }
    return *a == *b;
}

static size_t _schema_column_storage_size(const schema_column_t *column) {
    switch(column->type) {
    case SCHEMA_TYPE_INT: return sizeof(int32_t);
    case SCHEMA_TYPE_FLOAT: return sizeof(double);
    case SCHEMA_TYPE_VARCHAR: return column->length;
    default: return 0;
    }
}

static int _schema_default_int_ok(const char *value) {
    char *end = NULL;
    errno = 0;
    long parsed = strtol(value, &end, 10);
    if(errno != 0 || end == value || *end != '\0') return 0;
    return parsed >= INT32_MIN && parsed <= INT32_MAX;
}

static int _schema_default_float_ok(const char *value) {
    char *end = NULL;
    errno = 0;
    double parsed = strtod(value, &end);
    if(errno != 0 || end == value || *end != '\0') return 0;
    return isfinite(parsed);
}

static int _schema_default_ok(const schema_column_t *column) {
    if(!column->has_default) return column->default_value == NULL;
    if(column->default_value == NULL) return 0;

    switch(column->type) {
    case SCHEMA_TYPE_INT:
        return _schema_default_int_ok(column->default_value);
    case SCHEMA_TYPE_FLOAT:
        return _schema_default_float_ok(column->default_value);
    case SCHEMA_TYPE_VARCHAR:
        return strlen(column->default_value) <= column->length;
    default:
        return 0;
    }
}

static int _schema_column_ok(const schema_column_t *column) {
    if(!_schema_name_ok(column->name)) return 0;

    switch(column->type) {
    case SCHEMA_TYPE_INT:
    case SCHEMA_TYPE_FLOAT:
        if(column->length != 0) return 0;
        break;
    case SCHEMA_TYPE_VARCHAR:
        if(column->length == 0 || column->length > MAX_VARCHAR_LENGTH)
            return 0;
        break;
    default:
        return 0;
    }

    return _schema_default_ok(column);
}

size_t schema_serialized_size(const schema_t *this) {
    size_t size = 3 * sizeof(uint32_t);
    for(uint32_t c = 0; c < this->column_count; ++c) {
        const schema_column_t *column = &this->columns[c];
        size += sizeof(uint32_t) + strlen(column->name) + 1;
        size += sizeof(uint8_t) + sizeof(uint32_t);
        size += sizeof(uint8_t) + sizeof(uint8_t);
        if(column->has_default)
            size += sizeof(uint32_t) + strlen(column->default_value) + 1;
    }
    return size;
}

int schema_validate(const schema_t *this) {
    if(this == NULL || this->columns == NULL) return -1;
    if(this->column_count == 0 || this->column_count > MAX_COLUMN) return -1;

    size_t payload = 0;
    for(uint32_t c = 0; c < this->column_count; ++c) {
        const schema_column_t *column = &this->columns[c];
        if(!_schema_column_ok(column)) return -1;

        for(uint32_t other = 0; other < c; ++other)
            if(_schema_name_equal(column->name, this->columns[other].name))
                return -1;

        payload += _schema_column_storage_size(column);
    }

    /* Physical tables need a NULL bitmap plus one full record per page. */
    size_t record_size = ((size_t)this->column_count + 7) / 8 + payload;
    if(record_size > PAGE_SIZE - sizeof(uint64_t)) return -1;

    if(schema_serialized_size(this) > PAGE_SIZE) return -1;
    return 0;
}

schema_status_t schema_create(schema_t **this, const uint32_t column_count,
        const schema_column_t *columns) {
    if(this == NULL) return SCHEMA_ERR_INVALID;
    *this = NULL;

    schema_t view;
    view.column_count = column_count;
    view.columns = (schema_column_t*)columns;
    if(schema_validate(&view) != 0) return SCHEMA_ERR_INVALID;

    schema_t *schema = (schema_t*)_schema_xmalloc(sizeof(schema_t));
    schema->column_count = column_count;
    schema->columns = (schema_column_t*)_schema_xcalloc(column_count,
            sizeof(schema_column_t));

    for(uint32_t c = 0; c < column_count; ++c) {
        const schema_column_t *src = &columns[c];
        schema_column_t *dst = &schema->columns[c];

        dst->name = _schema_strdup(src->name);
        dst->type = src->type;
        dst->length = src->length;
        dst->not_null = src->not_null;
        dst->has_default = src->has_default;
        dst->default_value = src->has_default
                ? _schema_strdup(src->default_value) : NULL;
    }

    *this = schema;
    return SCHEMA_OK;
}

void schema_destroy(schema_t *this) {
    if(this == NULL) return;

    for(uint32_t c = 0; c < this->column_count; ++c) {
        free(this->columns[c].name);
        free(this->columns[c].default_value);
    }
    free(this->columns);
    free(this);
}

int schema_format(const schema_column_t *column, char *out, const size_t size) {
    if(column == NULL || out == NULL || size == 0) return -1;

    int n;
    switch(column->type) {
    case SCHEMA_TYPE_INT:
        n = snprintf(out, size, "i");
        break;
    case SCHEMA_TYPE_FLOAT:
        n = snprintf(out, size, "d");
        break;
    case SCHEMA_TYPE_VARCHAR:
        n = snprintf(out, size, "s%u", column->length);
        break;
    default:
        return -1;
    }

    return (n < 0 || (size_t)n >= size) ? -1 : 0;
}

const char* schema_type_name(const schema_type_t type) {
    switch(type) {
    case SCHEMA_TYPE_INT: return "INT";
    case SCHEMA_TYPE_FLOAT: return "FLOAT";
    case SCHEMA_TYPE_VARCHAR: return "VARCHAR";
    default: return "UNKNOWN";
    }
}

schema_status_t schema_save(const schema_t *this, void *buffer,
        const size_t size) {
    if(this == NULL || buffer == NULL) return SCHEMA_ERR_INVALID;
    if(schema_validate(this) != 0) return SCHEMA_ERR_INVALID;
    if(size < schema_serialized_size(this)) return SCHEMA_ERR_INVALID;

    char *p = (char*)buffer;
    uint32_t magic = SCHEMA_MAGIC;
    uint32_t version = SCHEMA_VERSION;
    uint32_t count = this->column_count;
    memcpy(p, &magic, sizeof(magic)); p += sizeof(magic);
    memcpy(p, &version, sizeof(version)); p += sizeof(version);
    memcpy(p, &count, sizeof(count)); p += sizeof(count);

    for(uint32_t c = 0; c < this->column_count; ++c) {
        const schema_column_t *column = &this->columns[c];

        uint32_t name_len = (uint32_t)strlen(column->name) + 1;
        memcpy(p, &name_len, sizeof(name_len)); p += sizeof(name_len);
        memcpy(p, column->name, name_len); p += name_len;

        uint8_t type = (uint8_t)column->type;
        uint32_t length = column->length;
        uint8_t not_null = column->not_null ? 1 : 0;
        uint8_t has_default = column->has_default ? 1 : 0;
        memcpy(p, &type, sizeof(type)); p += sizeof(type);
        memcpy(p, &length, sizeof(length)); p += sizeof(length);
        memcpy(p, &not_null, sizeof(not_null)); p += sizeof(not_null);
        memcpy(p, &has_default, sizeof(has_default)); p += sizeof(has_default);

        if(column->has_default) {
            uint32_t default_len =
                    (uint32_t)strlen(column->default_value) + 1;
            memcpy(p, &default_len, sizeof(default_len));
            p += sizeof(default_len);
            memcpy(p, column->default_value, default_len); p += default_len;
        }
    }

    return SCHEMA_OK;
}

typedef struct _s_schema_reader {
    const unsigned char *p;
    size_t left;
} schema_reader_t;

static int _schema_read(schema_reader_t *reader, void *dst, const size_t size) {
    if(size > reader->left) return -1;
    memcpy(dst, reader->p, size);
    reader->p += size;
    reader->left -= size;
    return 0;
}

schema_status_t schema_load(schema_t **this, const void *buffer,
        const size_t size) {
    if(this == NULL || buffer == NULL) return SCHEMA_ERR_INVALID;
    *this = NULL;

    schema_reader_t reader;
    reader.p = (const unsigned char*)buffer;
    reader.left = size;

    uint32_t magic;
    uint32_t version;
    uint32_t count;
    if(_schema_read(&reader, &magic, sizeof(magic)) != 0
            || _schema_read(&reader, &version, sizeof(version)) != 0
            || _schema_read(&reader, &count, sizeof(count)) != 0)
        return SCHEMA_ERR_INVALID;
    if(magic != SCHEMA_MAGIC || version != SCHEMA_VERSION) {
        return SCHEMA_ERR_INVALID;
    }
    if(count == 0 || count > MAX_COLUMN) return SCHEMA_ERR_INVALID;

    schema_t *schema = (schema_t*)_schema_xmalloc(sizeof(schema_t));
    schema->column_count = count;
    schema->columns = (schema_column_t*)_schema_xcalloc(count,
            sizeof(schema_column_t));

    for(uint32_t c = 0; c < count; ++c) {
        schema_column_t *column = &schema->columns[c];

        uint32_t name_len;
        if(_schema_read(&reader, &name_len, sizeof(name_len)) != 0)
            goto invalid;
        if(name_len == 0 || name_len > MAX_KEY_NAME_LENGTH) goto invalid;
        column->name = (char*)_schema_xmalloc(name_len);
        if(_schema_read(&reader, column->name, name_len) != 0) goto invalid;
        if(column->name[name_len - 1] != '\0') goto invalid;
        if(strlen(column->name) + 1 != name_len) goto invalid;

        uint8_t type;
        uint32_t length;
        uint8_t not_null;
        uint8_t has_default;
        if(_schema_read(&reader, &type, sizeof(type)) != 0
                || _schema_read(&reader, &length, sizeof(length)) != 0
                || _schema_read(&reader, &not_null, sizeof(not_null)) != 0
                || _schema_read(&reader, &has_default,
                        sizeof(has_default)) != 0)
            goto invalid;
        if(type > SCHEMA_TYPE_VARCHAR) goto invalid;
        if(not_null > 1 || has_default > 1) goto invalid;

        column->type = (schema_type_t)type;
        column->length = length;
        column->not_null = not_null != 0;
        column->has_default = has_default != 0;

        if(column->has_default) {
            uint32_t default_len;
            if(_schema_read(&reader, &default_len,
                    sizeof(default_len)) != 0)
                goto invalid;
            if(default_len == 0
                    || default_len > MAX_VARCHAR_LENGTH + 1)
                goto invalid;
            column->default_value = (char*)_schema_xmalloc(default_len);
            if(_schema_read(&reader, column->default_value,
                    default_len) != 0)
                goto invalid;
            if(column->default_value[default_len - 1] != '\0') goto invalid;
            if(strlen(column->default_value) + 1 != default_len) goto invalid;
        }
    }

    if(schema_validate(schema) != 0) goto invalid;

    *this = schema;
    return SCHEMA_OK;

invalid:
    schema_destroy(schema);
    return SCHEMA_ERR_INVALID;
}