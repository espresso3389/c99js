/*
 * c99js cJSON test driver
 *
 * This file includes a patched version of cJSON with all goto statements
 * replaced by structured control flow, since c99js does not support goto.
 * It then exercises key cJSON functionality: parse, create, print, and query.
 */

#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <limits.h>
#include <ctype.h>
#include <float.h>

/* ====== cJSON.h inlined ====== */

#define CJSON_PUBLIC(type) type

#define CJSON_VERSION_MAJOR 1
#define CJSON_VERSION_MINOR 7
#define CJSON_VERSION_PATCH 19

#define CJSON_CDECL

/* cJSON Types: */
#define cJSON_Invalid (0)
#define cJSON_False  (1 << 0)
#define cJSON_True   (1 << 1)
#define cJSON_NULL   (1 << 2)
#define cJSON_Number (1 << 3)
#define cJSON_String (1 << 4)
#define cJSON_Array  (1 << 5)
#define cJSON_Object (1 << 6)
#define cJSON_Raw    (1 << 7)

#define cJSON_IsReference 256
#define cJSON_StringIsConst 512

typedef struct cJSON
{
    struct cJSON *next;
    struct cJSON *prev;
    struct cJSON *child;
    int type;
    char *valuestring;
    int valueint;
    double valuedouble;
    char *string;
} cJSON;

typedef int cJSON_bool;

#ifndef CJSON_NESTING_LIMIT
#define CJSON_NESTING_LIMIT 1000
#endif

#ifndef CJSON_CIRCULAR_LIMIT
#define CJSON_CIRCULAR_LIMIT 10000
#endif

#define cJSON_ArrayForEach(element, array) for(element = (array != NULL) ? (array)->child : NULL; element != NULL; element = element->next)

/* ====== cJSON.c patched (no goto) ====== */

/* define our own boolean type */
#ifdef true
#undef true
#endif
#define true ((cJSON_bool)1)

#ifdef false
#undef false
#endif
#define false ((cJSON_bool)0)

#ifndef isinf
#define isinf(d) (isnan((d - d)) && !isnan(d))
#endif
#ifndef isnan
#define isnan(d) (d != d)
#endif

#ifndef NAN
#define NAN (0.0/0.0)
#endif

#ifndef DBL_EPSILON
#define DBL_EPSILON 2.2204460492503131e-16
#endif

typedef struct {
    const unsigned char *json;
    size_t position;
} error;
static error global_error = { NULL, 0 };

CJSON_PUBLIC(const char *) cJSON_GetErrorPtr(void)
{
    return (const char*) (global_error.json + global_error.position);
}

CJSON_PUBLIC(cJSON_bool) cJSON_IsInvalid(const cJSON * const item) { if (item == NULL) return false; return (item->type & 0xFF) == cJSON_Invalid; }
CJSON_PUBLIC(cJSON_bool) cJSON_IsFalse(const cJSON * const item) { if (item == NULL) return false; return (item->type & 0xFF) == cJSON_False; }
CJSON_PUBLIC(cJSON_bool) cJSON_IsTrue(const cJSON * const item) { if (item == NULL) return false; return (item->type & 0xff) == cJSON_True; }
CJSON_PUBLIC(cJSON_bool) cJSON_IsBool(const cJSON * const item) { if (item == NULL) return false; return (item->type & (cJSON_True | cJSON_False)) != 0; }
CJSON_PUBLIC(cJSON_bool) cJSON_IsNull(const cJSON * const item) { if (item == NULL) return false; return (item->type & 0xFF) == cJSON_NULL; }
CJSON_PUBLIC(cJSON_bool) cJSON_IsNumber(const cJSON * const item) { if (item == NULL) return false; return (item->type & 0xFF) == cJSON_Number; }
CJSON_PUBLIC(cJSON_bool) cJSON_IsString(const cJSON * const item) { if (item == NULL) return false; return (item->type & 0xFF) == cJSON_String; }
CJSON_PUBLIC(cJSON_bool) cJSON_IsArray(const cJSON * const item) { if (item == NULL) return false; return (item->type & 0xFF) == cJSON_Array; }
CJSON_PUBLIC(cJSON_bool) cJSON_IsObject(const cJSON * const item) { if (item == NULL) return false; return (item->type & 0xFF) == cJSON_Object; }
CJSON_PUBLIC(cJSON_bool) cJSON_IsRaw(const cJSON * const item) { if (item == NULL) return false; return (item->type & 0xFF) == cJSON_Raw; }

CJSON_PUBLIC(char *) cJSON_GetStringValue(const cJSON * const item)
{
    if (!cJSON_IsString(item)) return NULL;
    return item->valuestring;
}

CJSON_PUBLIC(double) cJSON_GetNumberValue(const cJSON * const item)
{
    if (!cJSON_IsNumber(item)) return (double) NAN;
    return item->valuedouble;
}

CJSON_PUBLIC(const char*) cJSON_Version(void)
{
    static char version[15];
    sprintf(version, "%i.%i.%i", CJSON_VERSION_MAJOR, CJSON_VERSION_MINOR, CJSON_VERSION_PATCH);
    return version;
}

static int case_insensitive_strcmp(const unsigned char *string1, const unsigned char *string2)
{
    if ((string1 == NULL) || (string2 == NULL)) return 1;
    if (string1 == string2) return 0;
    for(; tolower(*string1) == tolower(*string2); (void)string1++, string2++)
    {
        if (*string1 == '\0') return 0;
    }
    return tolower(*string1) - tolower(*string2);
}

typedef struct internal_hooks
{
    void *(CJSON_CDECL *allocate)(size_t size);
    void (CJSON_CDECL *deallocate)(void *pointer);
    void *(CJSON_CDECL *reallocate)(void *pointer, size_t size);
} internal_hooks;

static void * internal_malloc(size_t size)
{
    return malloc(size);
}
static void internal_free(void *pointer)
{
    free(pointer);
}
static void * internal_realloc(void *pointer, size_t size)
{
    return realloc(pointer, size);
}

#define static_strlen(string_literal) (sizeof(string_literal) - sizeof(""))

static internal_hooks global_hooks = { internal_malloc, internal_free, internal_realloc };

static unsigned char* cJSON_strdup(const unsigned char* string, const internal_hooks * const hooks)
{
    size_t length = 0;
    unsigned char *copy = NULL;
    if (string == NULL) return NULL;
    length = strlen((const char*)string) + sizeof("");
    copy = (unsigned char*)hooks->allocate(length);
    if (copy == NULL) return NULL;
    memcpy(copy, string, length);
    return copy;
}

CJSON_PUBLIC(void) cJSON_InitHooks(void *hooks) { (void)hooks; }

static cJSON *cJSON_New_Item(const internal_hooks * const hooks)
{
    cJSON* node = (cJSON*)hooks->allocate(sizeof(cJSON));
    if (node) memset(node, '\0', sizeof(cJSON));
    return node;
}

CJSON_PUBLIC(void) cJSON_Delete(cJSON *item)
{
    cJSON *next = NULL;
    while (item != NULL)
    {
        next = item->next;
        if (!(item->type & cJSON_IsReference) && (item->child != NULL))
            cJSON_Delete(item->child);
        if (!(item->type & cJSON_IsReference) && (item->valuestring != NULL))
        {
            global_hooks.deallocate(item->valuestring);
            item->valuestring = NULL;
        }
        if (!(item->type & cJSON_StringIsConst) && (item->string != NULL))
        {
            global_hooks.deallocate(item->string);
            item->string = NULL;
        }
        global_hooks.deallocate(item);
        item = next;
    }
}

static unsigned char get_decimal_point(void) { return '.'; }

typedef struct
{
    const unsigned char *content;
    size_t length;
    size_t offset;
    size_t depth;
    internal_hooks hooks;
} parse_buffer;

#define can_read(buffer, size) ((buffer != NULL) && (((buffer)->offset + size) <= (buffer)->length))
#define can_access_at_index(buffer, index) ((buffer != NULL) && (((buffer)->offset + index) < (buffer)->length))
#define cannot_access_at_index(buffer, index) (!can_access_at_index(buffer, index))
#define buffer_at_offset(buffer) ((buffer)->content + (buffer)->offset)

/* parse_number - patched: replaced goto loop_end with a done flag */
static cJSON_bool parse_number(cJSON * const item, parse_buffer * const input_buffer)
{
    double number = 0;
    unsigned char *after_end = NULL;
    unsigned char *number_c_string;
    unsigned char decimal_point = get_decimal_point();
    size_t i = 0;
    size_t number_string_length = 0;
    cJSON_bool has_decimal_point = false;
    cJSON_bool loop_done = false;

    if ((input_buffer == NULL) || (input_buffer->content == NULL))
        return false;

    for (i = 0; can_access_at_index(input_buffer, i) && !loop_done; i++)
    {
        switch (buffer_at_offset(input_buffer)[i])
        {
            case '0': case '1': case '2': case '3': case '4':
            case '5': case '6': case '7': case '8': case '9':
            case '+': case '-': case 'e': case 'E':
                number_string_length++;
                break;
            case '.':
                number_string_length++;
                has_decimal_point = true;
                break;
            default:
                loop_done = true;
                break;
        }
    }

    number_c_string = (unsigned char *) input_buffer->hooks.allocate(number_string_length + 1);
    if (number_c_string == NULL) return false;

    memcpy(number_c_string, buffer_at_offset(input_buffer), number_string_length);
    number_c_string[number_string_length] = '\0';

    if (has_decimal_point)
    {
        for (i = 0; i < number_string_length; i++)
        {
            if (number_c_string[i] == '.')
                number_c_string[i] = decimal_point;
        }
    }

    number = strtod((const char*)number_c_string, (char**)&after_end);
    if (number_c_string == after_end)
    {
        input_buffer->hooks.deallocate(number_c_string);
        return false;
    }

    item->valuedouble = number;
    if (number >= INT_MAX) item->valueint = INT_MAX;
    else if (number <= (double)INT_MIN) item->valueint = INT_MIN;
    else item->valueint = (int)number;

    item->type = cJSON_Number;
    input_buffer->offset += (size_t)(after_end - number_c_string);
    input_buffer->hooks.deallocate(number_c_string);
    return true;
}

CJSON_PUBLIC(double) cJSON_SetNumberHelper(cJSON *object, double number)
{
    if (number >= INT_MAX) object->valueint = INT_MAX;
    else if (number <= (double)INT_MIN) object->valueint = INT_MIN;
    else object->valueint = (int)number;
    return object->valuedouble = number;
}

typedef struct
{
    unsigned char *buffer;
    size_t length;
    size_t offset;
    size_t depth;
    cJSON_bool noalloc;
    cJSON_bool format;
    internal_hooks hooks;
} printbuffer;

static unsigned char* ensure(printbuffer * const p, size_t needed)
{
    unsigned char *newbuffer = NULL;
    size_t newsize = 0;

    if ((p == NULL) || (p->buffer == NULL)) return NULL;
    if ((p->length > 0) && (p->offset >= p->length)) return NULL;
    if (needed > INT_MAX) return NULL;

    needed += p->offset + 1;
    if (needed <= p->length) return p->buffer + p->offset;
    if (p->noalloc) return NULL;

    if (needed > (INT_MAX / 2))
    {
        if (needed <= INT_MAX) newsize = INT_MAX;
        else return NULL;
    }
    else
    {
        newsize = needed * 2;
    }

    if (p->hooks.reallocate != NULL)
    {
        newbuffer = (unsigned char*)p->hooks.reallocate(p->buffer, newsize);
        if (newbuffer == NULL)
        {
            p->hooks.deallocate(p->buffer);
            p->length = 0;
            p->buffer = NULL;
            return NULL;
        }
    }
    else
    {
        newbuffer = (unsigned char*)p->hooks.allocate(newsize);
        if (!newbuffer)
        {
            p->hooks.deallocate(p->buffer);
            p->length = 0;
            p->buffer = NULL;
            return NULL;
        }
        memcpy(newbuffer, p->buffer, p->offset + 1);
        p->hooks.deallocate(p->buffer);
    }
    p->length = newsize;
    p->buffer = newbuffer;
    return newbuffer + p->offset;
}

static void update_offset(printbuffer * const buffer)
{
    const unsigned char *buffer_pointer = NULL;
    if ((buffer == NULL) || (buffer->buffer == NULL)) return;
    buffer_pointer = buffer->buffer + buffer->offset;
    buffer->offset += strlen((const char*)buffer_pointer);
}

static cJSON_bool compare_double(double a, double b)
{
    double maxVal = fabs(a) > fabs(b) ? fabs(a) : fabs(b);
    return (fabs(a - b) <= maxVal * DBL_EPSILON);
}

static cJSON_bool print_number(const cJSON * const item, printbuffer * const output_buffer)
{
    unsigned char *output_pointer = NULL;
    double d = item->valuedouble;
    int length = 0;
    size_t i = 0;
    unsigned char number_buffer[26] = {0};
    unsigned char decimal_point = get_decimal_point();
    double test = 0.0;

    if (output_buffer == NULL) return false;

    if (isnan(d) || isinf(d))
    {
        length = sprintf((char*)number_buffer, "null");
    }
    else if(d == (double)item->valueint)
    {
        length = sprintf((char*)number_buffer, "%d", item->valueint);
    }
    else
    {
        length = sprintf((char*)number_buffer, "%1.15g", d);
        if ((sscanf((char*)number_buffer, "%lg", &test) != 1) || !compare_double((double)test, d))
        {
            length = sprintf((char*)number_buffer, "%1.17g", d);
        }
    }

    if ((length < 0) || (length > (int)(sizeof(number_buffer) - 1))) return false;

    output_pointer = ensure(output_buffer, (size_t)length + sizeof(""));
    if (output_pointer == NULL) return false;

    for (i = 0; i < ((size_t)length); i++)
    {
        if (number_buffer[i] == decimal_point) { output_pointer[i] = '.'; continue; }
        output_pointer[i] = number_buffer[i];
    }
    output_pointer[i] = '\0';
    output_buffer->offset += (size_t)length;
    return true;
}

static unsigned parse_hex4(const unsigned char * const input)
{
    unsigned int h = 0;
    size_t i = 0;
    for (i = 0; i < 4; i++)
    {
        if ((input[i] >= '0') && (input[i] <= '9')) h += (unsigned int) input[i] - '0';
        else if ((input[i] >= 'A') && (input[i] <= 'F')) h += (unsigned int) 10 + input[i] - 'A';
        else if ((input[i] >= 'a') && (input[i] <= 'f')) h += (unsigned int) 10 + input[i] - 'a';
        else return 0;
        if (i < 3) h = h << 4;
    }
    return h;
}

/* utf16_literal_to_utf8 - patched: replaced goto fail with early return 0 */
static unsigned char utf16_literal_to_utf8(const unsigned char * const input_pointer, const unsigned char * const input_end, unsigned char **output_pointer)
{
    long unsigned int codepoint = 0;
    unsigned int first_code = 0;
    const unsigned char *first_sequence = input_pointer;
    unsigned char utf8_length = 0;
    unsigned char utf8_position = 0;
    unsigned char sequence_length = 0;
    unsigned char first_byte_mark = 0;

    if ((input_end - first_sequence) < 6) return 0;

    first_code = parse_hex4(first_sequence + 2);
    if (((first_code >= 0xDC00) && (first_code <= 0xDFFF))) return 0;

    if ((first_code >= 0xD800) && (first_code <= 0xDBFF))
    {
        const unsigned char *second_sequence = first_sequence + 6;
        unsigned int second_code = 0;
        sequence_length = 12;
        if ((input_end - second_sequence) < 6) return 0;
        if ((second_sequence[0] != '\\') || (second_sequence[1] != 'u')) return 0;
        second_code = parse_hex4(second_sequence + 2);
        if ((second_code < 0xDC00) || (second_code > 0xDFFF)) return 0;
        codepoint = 0x10000 + (((first_code & 0x3FF) << 10) | (second_code & 0x3FF));
    }
    else
    {
        sequence_length = 6;
        codepoint = first_code;
    }

    if (codepoint < 0x80) { utf8_length = 1; }
    else if (codepoint < 0x800) { utf8_length = 2; first_byte_mark = 0xC0; }
    else if (codepoint < 0x10000) { utf8_length = 3; first_byte_mark = 0xE0; }
    else if (codepoint <= 0x10FFFF) { utf8_length = 4; first_byte_mark = 0xF0; }
    else return 0;

    for (utf8_position = (unsigned char)(utf8_length - 1); utf8_position > 0; utf8_position--)
    {
        (*output_pointer)[utf8_position] = (unsigned char)((codepoint | 0x80) & 0xBF);
        codepoint >>= 6;
    }
    if (utf8_length > 1) (*output_pointer)[0] = (unsigned char)((codepoint | first_byte_mark) & 0xFF);
    else (*output_pointer)[0] = (unsigned char)(codepoint & 0x7F);
    *output_pointer += utf8_length;
    return sequence_length;
}

/* parse_string - patched: replaced goto fail with do{...}while(0) + cleanup */
static cJSON_bool parse_string(cJSON * const item, parse_buffer * const input_buffer)
{
    const unsigned char *input_pointer = buffer_at_offset(input_buffer) + 1;
    const unsigned char *input_end = buffer_at_offset(input_buffer) + 1;
    unsigned char *output_pointer = NULL;
    unsigned char *output = NULL;
    cJSON_bool failed = false;

    if (buffer_at_offset(input_buffer)[0] != '\"')
    {
        return false;  /* not a string, no cleanup needed */
    }

    {
        size_t allocation_length = 0;
        size_t skipped_bytes = 0;
        while (((size_t)(input_end - input_buffer->content) < input_buffer->length) && (*input_end != '\"'))
        {
            if (input_end[0] == '\\')
            {
                if ((size_t)(input_end + 1 - input_buffer->content) >= input_buffer->length)
                {
                    failed = true;
                }
                if (!failed)
                {
                    skipped_bytes++;
                    input_end++;
                }
            }
            if (!failed)
            {
                input_end++;
            }
            else
            {
                break;
            }
        }
        if (!failed && (((size_t)(input_end - input_buffer->content) >= input_buffer->length) || (*input_end != '\"')))
        {
            failed = true;
        }

        if (!failed)
        {
            allocation_length = (size_t) (input_end - buffer_at_offset(input_buffer)) - skipped_bytes;
            output = (unsigned char*)input_buffer->hooks.allocate(allocation_length + sizeof(""));
            if (output == NULL) failed = true;
        }
    }

    if (!failed)
    {
        output_pointer = output;
        while (input_pointer < input_end && !failed)
        {
            if (*input_pointer != '\\')
            {
                *output_pointer++ = *input_pointer++;
            }
            else
            {
                unsigned char sequence_length = 2;
                if ((input_end - input_pointer) < 1) { failed = true; break; }
                switch (input_pointer[1])
                {
                    case 'b': *output_pointer++ = '\b'; break;
                    case 'f': *output_pointer++ = '\f'; break;
                    case 'n': *output_pointer++ = '\n'; break;
                    case 'r': *output_pointer++ = '\r'; break;
                    case 't': *output_pointer++ = '\t'; break;
                    case '\"': case '\\': case '/':
                        *output_pointer++ = input_pointer[1]; break;
                    case 'u':
                        sequence_length = utf16_literal_to_utf8(input_pointer, input_end, &output_pointer);
                        if (sequence_length == 0) { failed = true; }
                        break;
                    default:
                        failed = true; break;
                }
                if (!failed) input_pointer += sequence_length;
            }
        }
    }

    if (!failed)
    {
        *output_pointer = '\0';
        item->type = cJSON_String;
        item->valuestring = (char*)output;
        input_buffer->offset = (size_t) (input_end - input_buffer->content);
        input_buffer->offset++;
        return true;
    }

    /* fail cleanup */
    if (output != NULL)
    {
        input_buffer->hooks.deallocate(output);
    }
    if (input_pointer != NULL)
    {
        input_buffer->offset = (size_t)(input_pointer - input_buffer->content);
    }
    return false;
}

static cJSON_bool print_string_ptr(const unsigned char * const input, printbuffer * const output_buffer)
{
    const unsigned char *input_pointer = NULL;
    unsigned char *output = NULL;
    unsigned char *output_pointer = NULL;
    size_t output_length = 0;
    size_t escape_characters = 0;

    if (output_buffer == NULL) return false;

    if (input == NULL)
    {
        output = ensure(output_buffer, sizeof("\"\""));
        if (output == NULL) return false;
        strcpy((char*)output, "\"\"");
        return true;
    }

    for (input_pointer = input; *input_pointer; input_pointer++)
    {
        switch (*input_pointer)
        {
            case '\"': case '\\': case '\b': case '\f': case '\n': case '\r': case '\t':
                escape_characters++; break;
            default:
                if (*input_pointer < 32) escape_characters += 5;
                break;
        }
    }
    output_length = (size_t)(input_pointer - input) + escape_characters;

    output = ensure(output_buffer, output_length + sizeof("\"\""));
    if (output == NULL) return false;

    if (escape_characters == 0)
    {
        output[0] = '\"';
        memcpy(output + 1, input, output_length);
        output[output_length + 1] = '\"';
        output[output_length + 2] = '\0';
        return true;
    }

    output[0] = '\"';
    output_pointer = output + 1;
    for (input_pointer = input; *input_pointer != '\0'; (void)input_pointer++, output_pointer++)
    {
        if ((*input_pointer > 31) && (*input_pointer != '\"') && (*input_pointer != '\\'))
        {
            *output_pointer = *input_pointer;
        }
        else
        {
            *output_pointer++ = '\\';
            switch (*input_pointer)
            {
                case '\\': *output_pointer = '\\'; break;
                case '\"': *output_pointer = '\"'; break;
                case '\b': *output_pointer = 'b'; break;
                case '\f': *output_pointer = 'f'; break;
                case '\n': *output_pointer = 'n'; break;
                case '\r': *output_pointer = 'r'; break;
                case '\t': *output_pointer = 't'; break;
                default:
                    sprintf((char*)output_pointer, "u%04x", *input_pointer);
                    output_pointer += 4;
                    break;
            }
        }
    }
    output[output_length + 1] = '\"';
    output[output_length + 2] = '\0';
    return true;
}

static cJSON_bool print_string(const cJSON * const item, printbuffer * const p)
{
    return print_string_ptr((unsigned char*)item->valuestring, p);
}

/* Forward declarations */
static cJSON_bool parse_value(cJSON * const item, parse_buffer * const input_buffer);
static cJSON_bool print_value(const cJSON * const item, printbuffer * const output_buffer);
static cJSON_bool parse_array(cJSON * const item, parse_buffer * const input_buffer);
static cJSON_bool print_array(const cJSON * const item, printbuffer * const output_buffer);
static cJSON_bool parse_object(cJSON * const item, parse_buffer * const input_buffer);
static cJSON_bool print_object(const cJSON * const item, printbuffer * const output_buffer);

static parse_buffer *buffer_skip_whitespace(parse_buffer * const buffer)
{
    if ((buffer == NULL) || (buffer->content == NULL)) return NULL;
    if (cannot_access_at_index(buffer, 0)) return buffer;
    while (can_access_at_index(buffer, 0) && (buffer_at_offset(buffer)[0] <= 32))
    {
       buffer->offset++;
    }
    if (buffer->offset == buffer->length) buffer->offset--;
    return buffer;
}

static parse_buffer *skip_utf8_bom(parse_buffer * const buffer)
{
    if ((buffer == NULL) || (buffer->content == NULL) || (buffer->offset != 0)) return NULL;
    if (can_access_at_index(buffer, 4) && (strncmp((const char*)buffer_at_offset(buffer), "\xEF\xBB\xBF", 3) == 0))
    {
        buffer->offset += 3;
    }
    return buffer;
}

CJSON_PUBLIC(cJSON *) cJSON_ParseWithOpts(const char *value, const char **return_parse_end, cJSON_bool require_null_terminated);

/* cJSON_ParseWithLengthOpts - patched: replaced goto fail with do-while(0) */
CJSON_PUBLIC(cJSON *) cJSON_ParseWithLengthOpts(const char *value, size_t buffer_length, const char **return_parse_end, cJSON_bool require_null_terminated)
{
    parse_buffer buffer = { 0, 0, 0, 0, { 0, 0, 0 } };
    cJSON *item = NULL;
    cJSON_bool failed = false;

    global_error.json = NULL;
    global_error.position = 0;

    if (value == NULL || 0 == buffer_length)
    {
        failed = true;
    }

    if (!failed)
    {
        buffer.content = (const unsigned char*)value;
        buffer.length = buffer_length;
        buffer.offset = 0;
        buffer.hooks = global_hooks;

        item = cJSON_New_Item(&global_hooks);
        if (item == NULL) failed = true;
    }

    if (!failed)
    {
        if (!parse_value(item, buffer_skip_whitespace(skip_utf8_bom(&buffer))))
            failed = true;
    }

    if (!failed && require_null_terminated)
    {
        buffer_skip_whitespace(&buffer);
        if ((buffer.offset >= buffer.length) || buffer_at_offset(&buffer)[0] != '\0')
            failed = true;
    }

    if (!failed)
    {
        if (return_parse_end)
            *return_parse_end = (const char*)buffer_at_offset(&buffer);
        return item;
    }

    /* fail path */
    if (item != NULL) cJSON_Delete(item);

    if (value != NULL)
    {
        error local_error;
        local_error.json = (const unsigned char*)value;
        local_error.position = 0;
        if (buffer.offset < buffer.length) local_error.position = buffer.offset;
        else if (buffer.length > 0) local_error.position = buffer.length - 1;
        if (return_parse_end != NULL)
            *return_parse_end = (const char*)local_error.json + local_error.position;
        global_error = local_error;
    }
    return NULL;
}

CJSON_PUBLIC(cJSON *) cJSON_ParseWithOpts(const char *value, const char **return_parse_end, cJSON_bool require_null_terminated)
{
    size_t buffer_length;
    if (NULL == value) return NULL;
    buffer_length = strlen(value) + sizeof("");
    return cJSON_ParseWithLengthOpts(value, buffer_length, return_parse_end, require_null_terminated);
}

CJSON_PUBLIC(cJSON *) cJSON_Parse(const char *value)
{
    return cJSON_ParseWithOpts(value, 0, 0);
}

CJSON_PUBLIC(cJSON *) cJSON_ParseWithLength(const char *value, size_t buffer_length)
{
    return cJSON_ParseWithLengthOpts(value, buffer_length, 0, 0);
}

#define cjson_min(a, b) (((a) < (b)) ? (a) : (b))

/* print - patched: replaced goto fail; use regular struct, not array */
static unsigned char *print(const cJSON * const item, cJSON_bool format, const internal_hooks * const hooks)
{
    size_t default_buffer_size = 256;
    printbuffer buf;
    unsigned char *printed = NULL;
    cJSON_bool failed = false;

    buf.buffer = NULL;
    buf.length = 0;
    buf.offset = 0;
    buf.depth = 0;
    buf.noalloc = 0;
    buf.format = 0;
    buf.hooks.allocate = hooks->allocate;
    buf.hooks.deallocate = hooks->deallocate;
    buf.hooks.reallocate = hooks->reallocate;

    buf.buffer = (unsigned char*) hooks->allocate(default_buffer_size);
    buf.length = default_buffer_size;
    buf.format = format;
    if (buf.buffer == NULL) failed = true;

    if (!failed && !print_value(item, &buf)) failed = true;

    if (!failed)
    {
        update_offset(&buf);
        if (hooks->reallocate != NULL)
        {
            printed = (unsigned char*) hooks->reallocate(buf.buffer, buf.offset + 1);
            if (printed == NULL) failed = true;
            else buf.buffer = NULL;
        }
        else
        {
            printed = (unsigned char*) hooks->allocate(buf.offset + 1);
            if (printed == NULL) failed = true;
            else
            {
                memcpy(printed, buf.buffer, cjson_min(buf.length, buf.offset + 1));
                printed[buf.offset] = '\0';
                hooks->deallocate(buf.buffer);
                buf.buffer = NULL;
            }
        }
    }

    if (failed)
    {
        if (buf.buffer != NULL)
        {
            hooks->deallocate(buf.buffer);
            buf.buffer = NULL;
        }
        if (printed != NULL)
        {
            hooks->deallocate(printed);
            printed = NULL;
        }
        return NULL;
    }

    return printed;
}

CJSON_PUBLIC(char *) cJSON_Print(const cJSON *item)
{
    return (char*)print(item, true, &global_hooks);
}

CJSON_PUBLIC(char *) cJSON_PrintUnformatted(const cJSON *item)
{
    return (char*)print(item, false, &global_hooks);
}

/* parse_value */
static cJSON_bool parse_value(cJSON * const item, parse_buffer * const input_buffer)
{
    if ((input_buffer == NULL) || (input_buffer->content == NULL)) return false;

    /* null */
    if (can_read(input_buffer, 4) && (strncmp((const char*)buffer_at_offset(input_buffer), "null", 4) == 0))
    {
        item->type = cJSON_NULL;
        input_buffer->offset += 4;
        return true;
    }
    /* false */
    if (can_read(input_buffer, 5) && (strncmp((const char*)buffer_at_offset(input_buffer), "false", 5) == 0))
    {
        item->type = cJSON_False;
        input_buffer->offset += 5;
        return true;
    }
    /* true */
    if (can_read(input_buffer, 4) && (strncmp((const char*)buffer_at_offset(input_buffer), "true", 4) == 0))
    {
        item->type = cJSON_True;
        item->valueint = 1;
        input_buffer->offset += 4;
        return true;
    }
    /* string */
    if (can_access_at_index(input_buffer, 0) && (buffer_at_offset(input_buffer)[0] == '\"'))
        return parse_string(item, input_buffer);
    /* number */
    if (can_access_at_index(input_buffer, 0) && ((buffer_at_offset(input_buffer)[0] == '-') || ((buffer_at_offset(input_buffer)[0] >= '0') && (buffer_at_offset(input_buffer)[0] <= '9'))))
        return parse_number(item, input_buffer);
    /* array */
    if (can_access_at_index(input_buffer, 0) && (buffer_at_offset(input_buffer)[0] == '['))
        return parse_array(item, input_buffer);
    /* object */
    if (can_access_at_index(input_buffer, 0) && (buffer_at_offset(input_buffer)[0] == '{'))
        return parse_object(item, input_buffer);

    return false;
}

/* print_value */
static cJSON_bool print_value(const cJSON * const item, printbuffer * const output_buffer)
{
    unsigned char *output = NULL;
    if ((item == NULL) || (output_buffer == NULL)) return false;

    switch ((item->type) & 0xFF)
    {
        case cJSON_NULL:
            output = ensure(output_buffer, 5);
            if (output == NULL) return false;
            strcpy((char*)output, "null");
            return true;
        case cJSON_False:
            output = ensure(output_buffer, 6);
            if (output == NULL) return false;
            strcpy((char*)output, "false");
            return true;
        case cJSON_True:
            output = ensure(output_buffer, 5);
            if (output == NULL) return false;
            strcpy((char*)output, "true");
            return true;
        case cJSON_Number:
            return print_number(item, output_buffer);
        case cJSON_Raw:
        {
            size_t raw_length = 0;
            if (item->valuestring == NULL) return false;
            raw_length = strlen(item->valuestring) + sizeof("");
            output = ensure(output_buffer, raw_length);
            if (output == NULL) return false;
            memcpy(output, item->valuestring, raw_length);
            return true;
        }
        case cJSON_String:
            return print_string(item, output_buffer);
        case cJSON_Array:
            return print_array(item, output_buffer);
        case cJSON_Object:
            return print_object(item, output_buffer);
        default:
            return false;
    }
}

/* parse_array - patched: replaced goto fail/success with flags */
static cJSON_bool parse_array(cJSON * const item, parse_buffer * const input_buffer)
{
    cJSON *head = NULL;
    cJSON *current_item = NULL;
    cJSON_bool is_empty = false;
    cJSON_bool failed = false;

    if (input_buffer->depth >= CJSON_NESTING_LIMIT) return false;
    input_buffer->depth++;

    if (buffer_at_offset(input_buffer)[0] != '[') { failed = true; }

    if (!failed)
    {
        input_buffer->offset++;
        buffer_skip_whitespace(input_buffer);
        if (can_access_at_index(input_buffer, 0) && (buffer_at_offset(input_buffer)[0] == ']'))
        {
            is_empty = true;
        }
    }

    if (!failed && !is_empty)
    {
        if (cannot_access_at_index(input_buffer, 0))
        {
            input_buffer->offset--;
            failed = true;
        }
    }

    if (!failed && !is_empty)
    {
        input_buffer->offset--;
        do
        {
            cJSON *new_item = cJSON_New_Item(&(input_buffer->hooks));
            if (new_item == NULL) { failed = true; break; }

            if (head == NULL) { current_item = head = new_item; }
            else { current_item->next = new_item; new_item->prev = current_item; current_item = new_item; }

            input_buffer->offset++;
            buffer_skip_whitespace(input_buffer);
            if (!parse_value(current_item, input_buffer)) { failed = true; break; }
            buffer_skip_whitespace(input_buffer);
        }
        while (can_access_at_index(input_buffer, 0) && (buffer_at_offset(input_buffer)[0] == ','));

        if (!failed)
        {
            if (cannot_access_at_index(input_buffer, 0) || buffer_at_offset(input_buffer)[0] != ']')
                failed = true;
        }
    }

    if (failed)
    {
        if (head != NULL) cJSON_Delete(head);
        input_buffer->depth--;
        return false;
    }

    /* success */
    input_buffer->depth--;
    if (head != NULL) head->prev = current_item;
    item->type = cJSON_Array;
    item->child = head;
    input_buffer->offset++;
    return true;
}

/* print_array */
static cJSON_bool print_array(const cJSON * const item, printbuffer * const output_buffer)
{
    unsigned char *output_pointer = NULL;
    size_t length = 0;
    cJSON *current_element = item->child;

    if (output_buffer == NULL) return false;

    length = (size_t) (output_buffer->format ? 2 : 1);
    output_pointer = ensure(output_buffer, length + 1);
    if (output_pointer == NULL) return false;
    *output_pointer++ = '[';
    output_buffer->depth++;
    if (output_buffer->format) *output_pointer++ = '\n';
    output_buffer->offset += length;

    while (current_element != NULL)
    {
        if (output_buffer->format)
        {
            size_t i;
            output_pointer = ensure(output_buffer, output_buffer->depth);
            if (output_pointer == NULL) return false;
            for (i = 0; i < output_buffer->depth; i++) *output_pointer++ = '\t';
            output_buffer->offset += output_buffer->depth;
        }
        if (!print_value(current_element, output_buffer)) return false;
        update_offset(output_buffer);
        length = (size_t)(output_buffer->format ? 1 : 0) + (size_t)(current_element->next ? 1 : 0);
        output_pointer = ensure(output_buffer, length + 1);
        if (output_pointer == NULL) return false;
        if (current_element->next) *output_pointer++ = ',';
        if (output_buffer->format) *output_pointer++ = '\n';
        *output_pointer = '\0';
        output_buffer->offset += length;
        current_element = current_element->next;
    }

    output_pointer = ensure(output_buffer, output_buffer->format ? (output_buffer->depth + 1) : 2);
    if (output_pointer == NULL) return false;
    if (output_buffer->format)
    {
        size_t i;
        for (i = 0; i < (output_buffer->depth - 1); i++) *output_pointer++ = '\t';
    }
    *output_pointer++ = ']';
    *output_pointer = '\0';
    output_buffer->depth--;
    return true;
}

/* parse_object - patched: replaced goto fail/success with flags */
static cJSON_bool parse_object(cJSON * const item, parse_buffer * const input_buffer)
{
    cJSON *head = NULL;
    cJSON *current_item = NULL;
    cJSON_bool is_empty = false;
    cJSON_bool failed = false;

    if (input_buffer->depth >= CJSON_NESTING_LIMIT) return false;
    input_buffer->depth++;

    if (cannot_access_at_index(input_buffer, 0) || (buffer_at_offset(input_buffer)[0] != '{'))
    {
        failed = true;
    }

    if (!failed)
    {
        input_buffer->offset++;
        buffer_skip_whitespace(input_buffer);
        if (can_access_at_index(input_buffer, 0) && (buffer_at_offset(input_buffer)[0] == '}'))
        {
            is_empty = true;
        }
    }

    if (!failed && !is_empty)
    {
        if (cannot_access_at_index(input_buffer, 0))
        {
            input_buffer->offset--;
            failed = true;
        }
    }

    if (!failed && !is_empty)
    {
        input_buffer->offset--;
        do
        {
            cJSON *new_item = cJSON_New_Item(&(input_buffer->hooks));
            if (new_item == NULL) { failed = true; break; }

            if (head == NULL) { current_item = head = new_item; }
            else { current_item->next = new_item; new_item->prev = current_item; current_item = new_item; }

            if (cannot_access_at_index(input_buffer, 1)) { failed = true; break; }

            input_buffer->offset++;
            buffer_skip_whitespace(input_buffer);
            if (!parse_string(current_item, input_buffer)) { failed = true; break; }
            buffer_skip_whitespace(input_buffer);

            current_item->string = current_item->valuestring;
            current_item->valuestring = NULL;

            if (cannot_access_at_index(input_buffer, 0) || (buffer_at_offset(input_buffer)[0] != ':'))
            { failed = true; break; }

            input_buffer->offset++;
            buffer_skip_whitespace(input_buffer);
            if (!parse_value(current_item, input_buffer)) { failed = true; break; }
            buffer_skip_whitespace(input_buffer);
        }
        while (can_access_at_index(input_buffer, 0) && (buffer_at_offset(input_buffer)[0] == ','));

        if (!failed)
        {
            if (cannot_access_at_index(input_buffer, 0) || (buffer_at_offset(input_buffer)[0] != '}'))
                failed = true;
        }
    }

    if (failed)
    {
        if (head != NULL) cJSON_Delete(head);
        input_buffer->depth--;
        return false;
    }

    /* success */
    input_buffer->depth--;
    if (head != NULL) head->prev = current_item;
    item->type = cJSON_Object;
    item->child = head;
    input_buffer->offset++;
    return true;
}

/* print_object */
static cJSON_bool print_object(const cJSON * const item, printbuffer * const output_buffer)
{
    unsigned char *output_pointer = NULL;
    size_t length = 0;
    cJSON *current_item = item->child;

    if (output_buffer == NULL) return false;

    length = (size_t) (output_buffer->format ? 2 : 1);
    output_pointer = ensure(output_buffer, length + 1);
    if (output_pointer == NULL) return false;
    *output_pointer++ = '{';
    output_buffer->depth++;
    if (output_buffer->format) *output_pointer++ = '\n';
    output_buffer->offset += length;

    while (current_item)
    {
        if (output_buffer->format)
        {
            size_t i;
            output_pointer = ensure(output_buffer, output_buffer->depth);
            if (output_pointer == NULL) return false;
            for (i = 0; i < output_buffer->depth; i++) *output_pointer++ = '\t';
            output_buffer->offset += output_buffer->depth;
        }
        if (!print_string_ptr((unsigned char*)current_item->string, output_buffer)) return false;
        update_offset(output_buffer);
        length = (size_t) (output_buffer->format ? 2 : 1);
        output_pointer = ensure(output_buffer, length);
        if (output_pointer == NULL) return false;
        *output_pointer++ = ':';
        if (output_buffer->format) *output_pointer++ = '\t';
        output_buffer->offset += length;
        if (!print_value(current_item, output_buffer)) return false;
        update_offset(output_buffer);
        length = ((size_t)(output_buffer->format ? 1 : 0) + (size_t)(current_item->next ? 1 : 0));
        output_pointer = ensure(output_buffer, length + 1);
        if (output_pointer == NULL) return false;
        if (current_item->next) *output_pointer++ = ',';
        if (output_buffer->format) *output_pointer++ = '\n';
        *output_pointer = '\0';
        output_buffer->offset += length;
        current_item = current_item->next;
    }

    output_pointer = ensure(output_buffer, output_buffer->format ? (output_buffer->depth + 1) : 2);
    if (output_pointer == NULL) return false;
    if (output_buffer->format)
    {
        size_t i;
        for (i = 0; i < (output_buffer->depth - 1); i++) *output_pointer++ = '\t';
    }
    *output_pointer++ = '}';
    *output_pointer = '\0';
    output_buffer->depth--;
    return true;
}

/* Array/Object access functions */
CJSON_PUBLIC(int) cJSON_GetArraySize(const cJSON *array)
{
    cJSON *child = NULL;
    size_t size = 0;
    if (array == NULL) return 0;
    child = array->child;
    while(child != NULL) { size++; child = child->next; }
    return (int)size;
}

static cJSON* get_array_item(const cJSON *array, size_t index)
{
    cJSON *current_child = NULL;
    if (array == NULL) return NULL;
    current_child = array->child;
    while ((current_child != NULL) && (index > 0)) { index--; current_child = current_child->next; }
    return current_child;
}

CJSON_PUBLIC(cJSON *) cJSON_GetArrayItem(const cJSON *array, int index)
{
    if (index < 0) return NULL;
    return get_array_item(array, (size_t)index);
}

static cJSON *get_object_item(const cJSON * const object, const char * const name, const cJSON_bool case_sensitive)
{
    cJSON *current_element = NULL;
    if ((object == NULL) || (name == NULL)) return NULL;
    current_element = object->child;
    if (case_sensitive)
    {
        while ((current_element != NULL) && (current_element->string != NULL) && (strcmp(name, current_element->string) != 0))
            current_element = current_element->next;
    }
    else
    {
        while ((current_element != NULL) && (case_insensitive_strcmp((const unsigned char*)name, (const unsigned char*)(current_element->string)) != 0))
            current_element = current_element->next;
    }
    if ((current_element == NULL) || (current_element->string == NULL)) return NULL;
    return current_element;
}

CJSON_PUBLIC(cJSON *) cJSON_GetObjectItem(const cJSON * const object, const char * const string)
{
    return get_object_item(object, string, false);
}

CJSON_PUBLIC(cJSON *) cJSON_GetObjectItemCaseSensitive(const cJSON * const object, const char * const string)
{
    return get_object_item(object, string, true);
}

CJSON_PUBLIC(cJSON_bool) cJSON_HasObjectItem(const cJSON *object, const char *string)
{
    return cJSON_GetObjectItem(object, string) ? 1 : 0;
}

/* Utility functions */
static void suffix_object(cJSON *prev, cJSON *item)
{
    prev->next = item;
    item->prev = prev;
}

static void* cast_away_const(const void* string)
{
    return (void*)string;
}

static cJSON_bool add_item_to_array(cJSON *array, cJSON *item)
{
    cJSON *child = NULL;
    if ((item == NULL) || (array == NULL) || (array == item)) return false;
    child = array->child;
    if (child == NULL) { array->child = item; item->prev = item; item->next = NULL; }
    else { if (child->prev) { suffix_object(child->prev, item); array->child->prev = item; } }
    return true;
}

static cJSON_bool add_item_to_object(cJSON * const object, const char * const string, cJSON * const item, const internal_hooks * const hooks, const cJSON_bool constant_key)
{
    char *new_key = NULL;
    int new_type = cJSON_Invalid;
    if ((object == NULL) || (string == NULL) || (item == NULL) || (object == item)) return false;
    if (constant_key) { new_key = (char*)cast_away_const(string); new_type = item->type | cJSON_StringIsConst; }
    else
    {
        new_key = (char*)cJSON_strdup((const unsigned char*)string, hooks);
        if (new_key == NULL) return false;
        new_type = item->type & ~cJSON_StringIsConst;
    }
    if (!(item->type & cJSON_StringIsConst) && (item->string != NULL)) hooks->deallocate(item->string);
    item->string = new_key;
    item->type = new_type;
    return add_item_to_array(object, item);
}

CJSON_PUBLIC(cJSON_bool) cJSON_AddItemToArray(cJSON *array, cJSON *item)
{
    return add_item_to_array(array, item);
}

CJSON_PUBLIC(cJSON_bool) cJSON_AddItemToObject(cJSON *object, const char *string, cJSON *item)
{
    return add_item_to_object(object, string, item, &global_hooks, false);
}

CJSON_PUBLIC(cJSON_bool) cJSON_AddItemToObjectCS(cJSON *object, const char *string, cJSON *item)
{
    return add_item_to_object(object, string, item, &global_hooks, true);
}

/* Create basic types */
CJSON_PUBLIC(cJSON *) cJSON_CreateNull(void)
{
    cJSON *item = cJSON_New_Item(&global_hooks);
    if(item) item->type = cJSON_NULL;
    return item;
}

CJSON_PUBLIC(cJSON *) cJSON_CreateTrue(void)
{
    cJSON *item = cJSON_New_Item(&global_hooks);
    if(item) item->type = cJSON_True;
    return item;
}

CJSON_PUBLIC(cJSON *) cJSON_CreateFalse(void)
{
    cJSON *item = cJSON_New_Item(&global_hooks);
    if(item) item->type = cJSON_False;
    return item;
}

CJSON_PUBLIC(cJSON *) cJSON_CreateBool(cJSON_bool boolean)
{
    cJSON *item = cJSON_New_Item(&global_hooks);
    if(item) item->type = boolean ? cJSON_True : cJSON_False;
    return item;
}

CJSON_PUBLIC(cJSON *) cJSON_CreateNumber(double num)
{
    cJSON *item = cJSON_New_Item(&global_hooks);
    if(item)
    {
        item->type = cJSON_Number;
        item->valuedouble = num;
        if (num >= INT_MAX) item->valueint = INT_MAX;
        else if (num <= (double)INT_MIN) item->valueint = INT_MIN;
        else item->valueint = (int)num;
    }
    return item;
}

CJSON_PUBLIC(cJSON *) cJSON_CreateString(const char *string)
{
    cJSON *item = cJSON_New_Item(&global_hooks);
    if(item)
    {
        item->type = cJSON_String;
        item->valuestring = (char*)cJSON_strdup((const unsigned char*)string, &global_hooks);
        if(!item->valuestring) { cJSON_Delete(item); return NULL; }
    }
    return item;
}

CJSON_PUBLIC(cJSON *) cJSON_CreateArray(void)
{
    cJSON *item = cJSON_New_Item(&global_hooks);
    if(item) item->type = cJSON_Array;
    return item;
}

CJSON_PUBLIC(cJSON *) cJSON_CreateObject(void)
{
    cJSON *item = cJSON_New_Item(&global_hooks);
    if (item) item->type = cJSON_Object;
    return item;
}

CJSON_PUBLIC(cJSON*) cJSON_AddNullToObject(cJSON * const object, const char * const name)
{
    cJSON *null_item = cJSON_CreateNull();
    if (add_item_to_object(object, name, null_item, &global_hooks, false)) return null_item;
    cJSON_Delete(null_item);
    return NULL;
}

CJSON_PUBLIC(cJSON*) cJSON_AddTrueToObject(cJSON * const object, const char * const name)
{
    cJSON *true_item = cJSON_CreateTrue();
    if (add_item_to_object(object, name, true_item, &global_hooks, false)) return true_item;
    cJSON_Delete(true_item);
    return NULL;
}

CJSON_PUBLIC(cJSON*) cJSON_AddFalseToObject(cJSON * const object, const char * const name)
{
    cJSON *false_item = cJSON_CreateFalse();
    if (add_item_to_object(object, name, false_item, &global_hooks, false)) return false_item;
    cJSON_Delete(false_item);
    return NULL;
}

CJSON_PUBLIC(cJSON*) cJSON_AddBoolToObject(cJSON * const object, const char * const name, const cJSON_bool boolean)
{
    cJSON *bool_item = cJSON_CreateBool(boolean);
    if (add_item_to_object(object, name, bool_item, &global_hooks, false)) return bool_item;
    cJSON_Delete(bool_item);
    return NULL;
}

CJSON_PUBLIC(cJSON*) cJSON_AddNumberToObject(cJSON * const object, const char * const name, const double number)
{
    cJSON *number_item = cJSON_CreateNumber(number);
    if (add_item_to_object(object, name, number_item, &global_hooks, false)) return number_item;
    cJSON_Delete(number_item);
    return NULL;
}

CJSON_PUBLIC(cJSON*) cJSON_AddStringToObject(cJSON * const object, const char * const name, const char * const string)
{
    cJSON *string_item = cJSON_CreateString(string);
    if (add_item_to_object(object, name, string_item, &global_hooks, false)) return string_item;
    cJSON_Delete(string_item);
    return NULL;
}

CJSON_PUBLIC(cJSON*) cJSON_AddObjectToObject(cJSON * const object, const char * const name)
{
    cJSON *object_item = cJSON_CreateObject();
    if (add_item_to_object(object, name, object_item, &global_hooks, false)) return object_item;
    cJSON_Delete(object_item);
    return NULL;
}

CJSON_PUBLIC(cJSON*) cJSON_AddArrayToObject(cJSON * const object, const char * const name)
{
    cJSON *array = cJSON_CreateArray();
    if (add_item_to_object(object, name, array, &global_hooks, false)) return array;
    cJSON_Delete(array);
    return NULL;
}

CJSON_PUBLIC(void *) cJSON_malloc(size_t size)
{
    return global_hooks.allocate(size);
}

CJSON_PUBLIC(void) cJSON_free(void *object)
{
    global_hooks.deallocate(object);
}

/* ================================================================
 *  TEST DRIVER
 * ================================================================ */

static int tests_passed = 0;
static int tests_failed = 0;

static void test_assert(const char *name, int condition)
{
    if (condition)
    {
        printf("  PASS: %s\n", name);
        tests_passed++;
    }
    else
    {
        printf("  FAIL: %s\n", name);
        tests_failed++;
    }
}

static void test_parse_simple_object(void)
{
    const char *json_str = "{\"name\":\"cJSON\",\"version\":1.7,\"active\":true,\"license\":null}";
    cJSON *root = cJSON_Parse(json_str);
    cJSON *name_item;
    cJSON *version_item;
    cJSON *active_item;
    cJSON *license_item;

    printf("\n[Test 1] Parse simple JSON object\n");
    test_assert("cJSON_Parse returns non-NULL", root != NULL);
    if (root == NULL) return;

    test_assert("Root is object", cJSON_IsObject(root));

    name_item = cJSON_GetObjectItem(root, "name");
    test_assert("'name' field exists", name_item != NULL);
    test_assert("'name' is string", cJSON_IsString(name_item));
    test_assert("'name' value is 'cJSON'", name_item != NULL && name_item->valuestring != NULL && strcmp(name_item->valuestring, "cJSON") == 0);

    version_item = cJSON_GetObjectItem(root, "version");
    test_assert("'version' field exists", version_item != NULL);
    test_assert("'version' is number", cJSON_IsNumber(version_item));
    test_assert("'version' value is 1.7", version_item != NULL && (version_item->valuedouble > 1.69 && version_item->valuedouble < 1.71));

    active_item = cJSON_GetObjectItem(root, "active");
    test_assert("'active' field exists", active_item != NULL);
    test_assert("'active' is true", cJSON_IsTrue(active_item));

    license_item = cJSON_GetObjectItem(root, "license");
    test_assert("'license' field exists", license_item != NULL);
    test_assert("'license' is null", cJSON_IsNull(license_item));

    cJSON_Delete(root);
}

static void test_parse_array(void)
{
    const char *json_str = "[1, 2, 3, \"hello\", false]";
    cJSON *root = cJSON_Parse(json_str);
    cJSON *item;

    printf("\n[Test 2] Parse JSON array\n");
    test_assert("cJSON_Parse returns non-NULL", root != NULL);
    if (root == NULL) return;

    test_assert("Root is array", cJSON_IsArray(root));
    test_assert("Array size is 5", cJSON_GetArraySize(root) == 5);

    item = cJSON_GetArrayItem(root, 0);
    test_assert("Item 0 is number 1", item != NULL && cJSON_IsNumber(item) && item->valueint == 1);

    item = cJSON_GetArrayItem(root, 1);
    test_assert("Item 1 is number 2", item != NULL && cJSON_IsNumber(item) && item->valueint == 2);

    item = cJSON_GetArrayItem(root, 2);
    test_assert("Item 2 is number 3", item != NULL && cJSON_IsNumber(item) && item->valueint == 3);

    item = cJSON_GetArrayItem(root, 3);
    test_assert("Item 3 is string 'hello'", item != NULL && cJSON_IsString(item) && strcmp(item->valuestring, "hello") == 0);

    item = cJSON_GetArrayItem(root, 4);
    test_assert("Item 4 is false", item != NULL && cJSON_IsFalse(item));

    cJSON_Delete(root);
}

static void test_parse_nested(void)
{
    const char *json_str = "{\"person\":{\"name\":\"Alice\",\"age\":30},\"scores\":[95,87,92]}";
    cJSON *root = cJSON_Parse(json_str);
    cJSON *person;
    cJSON *scores;
    cJSON *name_item;
    cJSON *age_item;
    cJSON *score0;

    printf("\n[Test 3] Parse nested JSON\n");
    test_assert("cJSON_Parse returns non-NULL", root != NULL);
    if (root == NULL) return;

    person = cJSON_GetObjectItem(root, "person");
    test_assert("'person' is object", person != NULL && cJSON_IsObject(person));

    name_item = cJSON_GetObjectItem(person, "name");
    test_assert("person.name is 'Alice'", name_item != NULL && strcmp(name_item->valuestring, "Alice") == 0);

    age_item = cJSON_GetObjectItem(person, "age");
    test_assert("person.age is 30", age_item != NULL && age_item->valueint == 30);

    scores = cJSON_GetObjectItem(root, "scores");
    test_assert("'scores' is array", scores != NULL && cJSON_IsArray(scores));
    test_assert("scores has 3 items", cJSON_GetArraySize(scores) == 3);

    score0 = cJSON_GetArrayItem(scores, 0);
    test_assert("scores[0] is 95", score0 != NULL && score0->valueint == 95);

    cJSON_Delete(root);
}

static void test_create_object(void)
{
    cJSON *root;
    cJSON *hobbies;
    char *printed;

    printf("\n[Test 4] Create JSON object programmatically\n");

    root = cJSON_CreateObject();
    test_assert("CreateObject returns non-NULL", root != NULL);
    if (root == NULL) return;

    cJSON_AddStringToObject(root, "name", "Bob");
    cJSON_AddNumberToObject(root, "age", 25.0);
    cJSON_AddBoolToObject(root, "student", 1);
    cJSON_AddNullToObject(root, "address");

    hobbies = cJSON_AddArrayToObject(root, "hobbies");
    cJSON_AddItemToArray(hobbies, cJSON_CreateString("reading"));
    cJSON_AddItemToArray(hobbies, cJSON_CreateString("coding"));

    test_assert("Object has 5 fields", cJSON_GetArraySize(root) == 5);
    test_assert("name is 'Bob'", strcmp(cJSON_GetObjectItem(root, "name")->valuestring, "Bob") == 0);
    test_assert("age is 25", cJSON_GetObjectItem(root, "age")->valueint == 25);
    test_assert("student is true", cJSON_IsTrue(cJSON_GetObjectItem(root, "student")));
    test_assert("address is null", cJSON_IsNull(cJSON_GetObjectItem(root, "address")));
    test_assert("hobbies has 2 items", cJSON_GetArraySize(hobbies) == 2);

    printed = cJSON_PrintUnformatted(root);
    test_assert("PrintUnformatted returns non-NULL", printed != NULL);
    if (printed != NULL)
    {
        printf("  Created JSON: %s\n", printed);
        free(printed);
    }

    cJSON_Delete(root);
}

static void test_print_and_reparse(void)
{
    const char *original = "{\"x\":42,\"y\":\"test\",\"z\":[1,2,3]}";
    cJSON *root;
    char *printed;
    cJSON *reparsed;
    cJSON *x_item;
    cJSON *y_item;
    cJSON *z_item;

    printf("\n[Test 5] Print and re-parse round-trip\n");

    root = cJSON_Parse(original);
    test_assert("Parse original", root != NULL);
    if (root == NULL) return;

    printed = cJSON_PrintUnformatted(root);
    test_assert("Print returns non-NULL", printed != NULL);
    if (printed == NULL) { cJSON_Delete(root); return; }

    printf("  Original:  %s\n", original);
    printf("  Printed:   %s\n", printed);

    reparsed = cJSON_Parse(printed);
    test_assert("Re-parse succeeds", reparsed != NULL);

    if (reparsed != NULL)
    {
        x_item = cJSON_GetObjectItem(reparsed, "x");
        test_assert("Re-parsed x is 42", x_item != NULL && x_item->valueint == 42);

        y_item = cJSON_GetObjectItem(reparsed, "y");
        test_assert("Re-parsed y is 'test'", y_item != NULL && strcmp(y_item->valuestring, "test") == 0);

        z_item = cJSON_GetObjectItem(reparsed, "z");
        test_assert("Re-parsed z has 3 items", z_item != NULL && cJSON_GetArraySize(z_item) == 3);

        cJSON_Delete(reparsed);
    }

    free(printed);
    cJSON_Delete(root);
}

static void test_escape_strings(void)
{
    const char *json_str = "{\"msg\":\"hello\\nworld\\ttab\"}";
    cJSON *root = cJSON_Parse(json_str);
    cJSON *msg;

    printf("\n[Test 6] Parse escaped strings\n");
    test_assert("Parse escaped string JSON", root != NULL);
    if (root == NULL) return;

    msg = cJSON_GetObjectItem(root, "msg");
    test_assert("msg exists", msg != NULL);
    test_assert("msg contains newline", msg != NULL && msg->valuestring != NULL && strchr(msg->valuestring, '\n') != NULL);
    test_assert("msg contains tab", msg != NULL && msg->valuestring != NULL && strchr(msg->valuestring, '\t') != NULL);

    cJSON_Delete(root);
}

static void test_type_checks(void)
{
    cJSON *root;
    cJSON *item;
    const char *json_str = "{\"n\":42,\"s\":\"hi\",\"b\":true,\"f\":false,\"nil\":null,\"a\":[],\"o\":{}}";

    printf("\n[Test 7] Type checking functions\n");

    root = cJSON_Parse(json_str);
    test_assert("Parse type-check JSON", root != NULL);
    if (root == NULL) return;

    item = cJSON_GetObjectItem(root, "n");
    test_assert("IsNumber for number", cJSON_IsNumber(item));
    test_assert("!IsString for number", !cJSON_IsString(item));

    item = cJSON_GetObjectItem(root, "s");
    test_assert("IsString for string", cJSON_IsString(item));
    test_assert("!IsNumber for string", !cJSON_IsNumber(item));

    item = cJSON_GetObjectItem(root, "b");
    test_assert("IsTrue for true", cJSON_IsTrue(item));
    test_assert("IsBool for true", cJSON_IsBool(item));

    item = cJSON_GetObjectItem(root, "f");
    test_assert("IsFalse for false", cJSON_IsFalse(item));
    test_assert("IsBool for false", cJSON_IsBool(item));

    item = cJSON_GetObjectItem(root, "nil");
    test_assert("IsNull for null", cJSON_IsNull(item));

    item = cJSON_GetObjectItem(root, "a");
    test_assert("IsArray for array", cJSON_IsArray(item));

    item = cJSON_GetObjectItem(root, "o");
    test_assert("IsObject for object", cJSON_IsObject(item));

    cJSON_Delete(root);
}

static void test_version(void)
{
    const char *ver;
    printf("\n[Test 8] cJSON version\n");
    ver = cJSON_Version();
    test_assert("Version is not NULL", ver != NULL);
    printf("  cJSON version: %s\n", ver);
    test_assert("Version starts with '1.'", ver != NULL && ver[0] == '1' && ver[1] == '.');
}

static void test_invalid_json(void)
{
    cJSON *root;
    printf("\n[Test 9] Invalid JSON handling\n");

    root = cJSON_Parse("");
    test_assert("Empty string returns NULL", root == NULL);

    root = cJSON_Parse(NULL);
    test_assert("NULL input returns NULL", root == NULL);

    root = cJSON_Parse("{invalid}");
    test_assert("Invalid JSON returns NULL", root == NULL);

    root = cJSON_Parse("[1, 2,]");
    test_assert("Trailing comma returns NULL", root == NULL);
}

static void test_formatted_print(void)
{
    cJSON *root;
    char *printed;
    printf("\n[Test 10] Formatted print\n");

    root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "key", "value");
    cJSON_AddNumberToObject(root, "num", 123.0);

    printed = cJSON_Print(root);
    test_assert("Formatted print returns non-NULL", printed != NULL);
    if (printed != NULL)
    {
        test_assert("Formatted output contains newline", strchr(printed, '\n') != NULL);
        test_assert("Formatted output contains tab", strchr(printed, '\t') != NULL);
        printf("  Formatted output:\n%s\n", printed);
        free(printed);
    }

    cJSON_Delete(root);
}

int main(void)
{
    printf("=== cJSON c99js Test Suite ===\n");

    test_parse_simple_object();
    test_parse_array();
    test_parse_nested();
    test_create_object();
    test_print_and_reparse();
    test_escape_strings();
    test_type_checks();
    test_version();
    test_invalid_json();
    test_formatted_print();

    printf("\n=== Results: %d passed, %d failed ===\n", tests_passed, tests_failed);
    if (tests_failed == 0)
        printf("ALL TESTS PASSED\n");
    else
        printf("SOME TESTS FAILED\n");

    return tests_failed;
}
