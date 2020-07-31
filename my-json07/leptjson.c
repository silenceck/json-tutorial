#include "leptjson.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>  /* errno, ERANGE */
#include <math.h>   /* HUGE_VAL */
#include <stdio.h>   /* sprintf() */

#ifndef LEPT_PARSE_STACK_INIT_SIZE
#define  LEPT_PARSE_STACK_INIT_SIZE 256
#endif

#ifndef LEPT_PARSE_STRINGFY_INIT_SIZE
#define LEPT_PARSE_STRINGFY_INIT_SIZE 256
#endif

typedef struct {
    const char* json;
    char* stack;
    size_t size, top;
    const lept_value* arr_stack;
}lept_context;

#define IS_DIGIT(ch)        ((ch) >= '0' && (ch) <= '9')
#define IS_DIGIT1TO9(ch)    ((ch) >= '1' && (ch) <= '9')
#define IS_HEX(ch)          (((ch) >= '1' && (ch) <= '9') || ((ch) >= 'A' && (ch) <= 'F'))
#define STRING_ERROR(ret) do { c->top = head; return ret;} while(0)
#define PUT_CH(c, ch)     do { *(char*)lept_context_push(c, sizeof(char)) = (ch);} while(0)
#define PUTS(c, s, len)   do { memcpy((char*)lept_context_push(c, len), s, len);} while(0)

void EXPECT(lept_context* c, char ch) {
    do {
        assert(*c->json == (ch));
        c->json++;
    } while (0);
}

static void lept_parse_whitespace(lept_context* c) {
    const char* p = c->json;
    while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')
        p++;
    c->json = p;
}

void lept_free(lept_value* v) {
    size_t i;
    assert(v != NULL);
    switch(v->type) {
        case LEPT_STRING:
            free(v->u.s.s);
            break;
        case LEPT_ARRAY:
            for(i = 0; i < v->u.a.size; i++) {
                lept_free(&v->u.a.e[i]);
            }
            free(v->u.a.e);
            break;
        case LEPT_OBJECT:
            for(i = 0; i < v->u.o.size; i++) {
                lept_free(&v->u.o.m[i].v);
                free(v->u.o.m[i].k);
            }
            free(v->u.o.m);
            break;
        default:
            break;
    }
    v->type = LEPT_NULL;
}

static const char* lept_parse_hex4(const char* p, unsigned* u) {
    *u = 0;
    for(int i = 0; i < 4; ++i) {
        char ch = *p++;
        *u <<= 4;
        if      (ch >= '0' && ch <= '9')  *u |= ch - '0';
        else if (ch >= 'a' && ch <= 'f')  *u |= ch - 'a' + 10;
        else if (ch >= 'A' && ch <= 'F')  *u |= ch - 'A' + 10;
        else 
            return NULL;
    }
    return p;
}

static int lept_parse_interal(lept_context* c, lept_value* v, const char* interal, lept_type type) {
    size_t i = 0;
    EXPECT(c, interal[0]);
    while(interal[i+1] != '\0') {
        if(interal[i+1] != c->json[i])
            return LEPT_PARSE_INVALID_VALUE;
        ++i;
    }
    c->json += i;
    v->type = type;
    return LEPT_PARSE_OK;
}

static int lept_parse_number(lept_context* c , lept_value* v) {
    char* end;
    const char* p = c->json;
    if(*p == '-') p++;
    if(*p == '0') p++;
    else {
        if(!IS_DIGIT1TO9(*p)) return LEPT_PARSE_INVALID_VALUE;
        for(++p; IS_DIGIT(*p); ++p);
    }
    if(*p == '.') {
        p++;
        if(!IS_DIGIT(*p)) return LEPT_PARSE_INVALID_VALUE;
        for(; IS_DIGIT(*p); ++p);
    }
    if(*p == 'e' || *p == 'E') {
        ++p;
        if(*p == '+' || *p == '-') ++p;
        if(!IS_DIGIT(*p)) return LEPT_PARSE_INVALID_VALUE;
        for(; IS_DIGIT(*p); ++p);
    }
    errno = 0;
    v->u.n = strtod(c->json, &end);
    if(errno == ERANGE && (v->u.n == HUGE_VAL || v->u.n == -HUGE_VAL))
        return LEPT_PARSE_NUMBER_TOO_BIG;
    if(c->json == end)
        return LEPT_PARSE_INVALID_VALUE;
    c->json = end;
    v->type = LEPT_NUMBER;
    return LEPT_PARSE_OK;
}

static int lept_parse_string(lept_context* c, lept_value* v);

static void*  lept_context_push(lept_context* c, size_t size) {
    void* ret;
    if((c->top + size) >= c->size) {
        if(c->size == 0)
            c->size = LEPT_PARSE_STACK_INIT_SIZE;
        while ((c->top + size) >= c->size){
            c->size += c->size>>1;
        }
        c->stack = realloc(c->stack, c->size);
    }
    ret = c->stack + c->top;
    c->top += size;
    return ret;
}

static void* lept_context_pop(lept_context* c, size_t size) {
    assert(size >= 0);
    return c->stack + (c->top -= size);
}

static int lept_parse_value(lept_context* c, lept_value* v);

static int lept_parse_array(lept_context* c, lept_value* v) {
    size_t size = 0, i;
    int ret;
    EXPECT(c, '[');
    lept_parse_whitespace(c);
    if(*c->json == ']') {
        c->json++;
        v->type = LEPT_ARRAY;
        v->u.a.e = NULL;
        v->u.a.size = 0;
        return LEPT_PARSE_OK;
    }
    for(;;) {
        lept_value temp;
        lept_init(&temp);
        ret = lept_parse_value(c, &temp);
        if(ret != LEPT_PARSE_OK)
            break;
        memcpy(lept_context_push(c, sizeof(lept_value)), &temp, sizeof(lept_value));
        size++;
        lept_parse_whitespace(c);
        if(*c->json == ',') {
            c->json++;
            lept_parse_whitespace(c);
        } else if(*c->json == ']') {
            c->json++;
            v->type = LEPT_ARRAY;
            v->u.a.size = size;
            size *= sizeof(lept_value);
            memcpy(v->u.a.e = (lept_value*)malloc(size), lept_context_pop(c, size), size);
            return LEPT_PARSE_OK;
        } else 
            ret = LEPT_PARSE_MISS_COMMA_OR_SQUARE_BRACKET;
    }
    for(i = 0; i < size; ++i) {
        lept_free((lept_value*)lept_context_pop(c, sizeof(lept_value)));
    }
    return ret;
}

static void lept_encode_utf8(lept_context* c, unsigned u);

static int lept_parse_string_raw(lept_context* c, char** str, size_t* len) {
    /* 这里为什么使用char** str,而不使用char* */
    size_t head = c->top;
    const char* p;
    unsigned u, u2;
    EXPECT(c, '\"');
    p = c->json;
    while(true) {
        char ch = *p++;
        switch(ch) {
            case '\"': 
                *len = c->top - head;
                *str = lept_context_pop(c, *len);
                c->json = p;
                return LEPT_PARSE_OK;
            case '\0':
                c->top = head;
                return LEPT_PARSE_MISS_QUOTATION_MARK;
            case '\\':
                switch(*p++) {
                    case '\"': PUT_CH(c, '\"'); break;
                    case '\\': PUT_CH(c, '\\'); break;
                    case '/':  PUT_CH(c, '/' ); break;
                    case 'b':  PUT_CH(c, '\b'); break;
                    case 'f':  PUT_CH(c, '\f'); break;
                    case 'n':  PUT_CH(c, '\n'); break;
                    case 'r':  PUT_CH(c, '\r'); break;
                    case 't':  PUT_CH(c, '\t'); break;
                    case 'u':  
                        if(!(p = lept_parse_hex4(p, &u))) 
                            STRING_ERROR(LEPT_PARSE_INVALID_UNICODE_HEX);
                        if(u >= 0xD800 && u <= 0xDBFF) {
                            if(*p++ != '\\')
                                STRING_ERROR(LEPT_PARSE_INVALID_UNICODE_SURROGATE);
                            if(*p++ != 'u')
                                STRING_ERROR(LEPT_PARSE_INVALID_UNICODE_SURROGATE);
                            if(!(p = lept_parse_hex4(p, &u2))) 
                                STRING_ERROR(LEPT_PARSE_INVALID_UNICODE_HEX);
                            if (u2 < 0xDC00 || u2 > 0xDFFF)
                                STRING_ERROR(LEPT_PARSE_INVALID_UNICODE_SURROGATE);
                            u = (((u - 0xD800) << 10) | (u2 - 0xDC00)) + 0x10000;
                        }
                        lept_encode_utf8(c, u);
                        break;
                    default: 
                        c->top = head;
                        return LEPT_PARSE_INVALID_STRING_ESCAPE;   
                }
                break;
            default:
                if((unsigned char)ch < 0x22) {
                    c->top = head;
                    return LEPT_PARSE_INVALID_STRING_CHAR;
                }
                PUT_CH(c, ch);
        }
    }
}

static int lept_parse_object(lept_context* c, lept_value* v) {
    size_t i = 0;
    int ret;
    EXPECT(c, '{');
    lept_parse_whitespace(c);
    if(*c->json == '}') {
        c->json++;
        v->type = LEPT_OBJECT;
        v->u.o.m = NULL;
        v->u.o.size = 0;
        return LEPT_PARSE_OK;
    }
    lept_member m;
    m.k = NULL;
    for(;;) {
        lept_init(&m.v);
        char* s;
        size_t len;
        /* 1. parse key */
        if(*c->json == '\"') {
            if((ret = lept_parse_string_raw(c, &s, &m.klen)) != LEPT_PARSE_OK)
                break;
        } else {
            ret = LEPT_PARSE_MISS_KEY;
            break;
        }
        memcpy((m.k = (char*)malloc(m.klen+1)), s, m.klen);
        m.k[m.klen] = '\0';
        /* 2. parse ws colon ws */
        lept_parse_whitespace(c);
        if(*c->json != ':') {
            ret = LEPT_PARSE_MISS_COLON;
            break;
        }
        c->json++;
        lept_parse_whitespace(c);
        /* 3. parse value */
        if((ret = lept_parse_value(c, &m.v)) != LEPT_PARSE_OK) 
            break;
        lept_member* t = (lept_member*)lept_context_push(c, sizeof(lept_member));
        memcpy(t, &m, sizeof(lept_member));
        i++;
        m.k = NULL; /* ownership is transferred to member on stack ???? */
        /* 4. parse ws [comma | right-curly-brace] ws */
        lept_parse_whitespace(c);
        if(*c->json == ',') {
            c->json++;
            lept_parse_whitespace(c);
        }
        else if(*c->json++ == '}') {
            c->json++;
            v->type = LEPT_OBJECT;
            memcpy(v->u.o.m=(lept_member*)malloc(i*sizeof(lept_member)), lept_context_pop(c, i*sizeof(lept_member)), i*sizeof(lept_member));
            v->u.o.size = i;
            return LEPT_PARSE_OK;
        } else {
            ret = LEPT_PARSE_MISS_COMMA_OR_CURLY_BRACKET;
            break;
        }
            
    }
    free(m.k);
    size_t j;
    for(j = 0; j < i; j++) {
        lept_member* m = (lept_member*)lept_context_pop(c, sizeof(lept_member));
        lept_free(&m->v);
        free(m->k);
    }
    v->type = LEPT_NULL;
    return ret;
}

static int lept_parse_value(lept_context* c, lept_value* v) {
    switch (*c->json) {
        case 'n':  return lept_parse_interal(c, v, "null", LEPT_NULL);
        case 't':  return lept_parse_interal(c, v, "true", LEPT_TRUE);
        case 'f':  return lept_parse_interal(c, v, "false", LEPT_FALSE);
        case '\"': return lept_parse_string(c, v);
        case '[':  return lept_parse_array(c, v);
        case '{':  return lept_parse_object(c, v);
        default:   return lept_parse_number(c, v);
        case '\0': return LEPT_PARSE_EXPECT_VALUE;

    }
}

static void lept_encode_utf8(lept_context* c, unsigned u) {
    if(u <= 0x7F)
        PUT_CH(c, u & 0xFF);
    else if(u <= 0x7FF) {
        PUT_CH(c, 0xC0 | (u >> 6 & 0xFF));
        PUT_CH(c, 0x80 | (u      & 0x3F));
    } else if(u <= 0xFFFF) {
        PUT_CH(c, 0xE0 | (u >> 12 & 0xFF));
        PUT_CH(c, 0x80 | (u >> 6  & 0x3F));
        PUT_CH(c, 0x80 | (u       & 0x3F));
    } else if(u <= 0x10FFFF) {
        PUT_CH(c, 0xF0 | ((u >> 18) & 0xFF));
        PUT_CH(c, 0x80 | ((u >> 12) & 0x3F));
        PUT_CH(c, 0x80 | ((u >>  6) & 0x3F));
        PUT_CH(c, 0x80 | ( u        & 0x3F));
    }

}

static int lept_parse_string(lept_context* c, lept_value* v) {
    int ret;
    size_t len;
    char* s;
    if((ret = lept_parse_string_raw(c, &s, &len)) == LEPT_PARSE_OK)
        lept_set_string(v, s, len);
    return ret;
}

int lept_parse(lept_value* v, const char* json) {
    lept_context c;
    int ret;
    assert(v != NULL);
    c.json = json;
    c.stack = NULL;
    c.size = c.top = 0;
    lept_init(v);
    lept_parse_whitespace(&c);
    ret = lept_parse_value(&c, v);
    if(ret == LEPT_PARSE_OK) {
        lept_parse_whitespace((&c));
        if(c.json[0] != '\0') {
            v->type = LEPT_NULL;
            ret = LEPT_PARSE_ROOT_NOT_SINGULAR;
        }
    }
    assert(c.top == 0);
    free(c.stack);
    return ret;
}

lept_type lept_get_type(const lept_value* v) {
    assert(v != NULL);
    return v->type;
}

double lept_get_number(const lept_value* v) {
    assert(v != NULL && v->type == LEPT_NUMBER);
    return v->u.n;
}

void lept_set_number(lept_value* v, double n) {
    lept_free(v);
    v->type = LEPT_NUMBER;
    v->u.n = n;

}

int lept_get_boolean(const lept_value* v) {
    assert(v != NULL && (v->type == LEPT_TRUE || v->type == LEPT_FALSE));
    return v->type == LEPT_TRUE;
}

void lept_set_boolen(lept_value* v, int b) {
    lept_free(v);
    v->type = b? LEPT_TRUE: LEPT_FALSE;
}

void lept_set_string(lept_value* v, const char* s, size_t len){
    assert(v != NULL && (s != NULL || len != 0));
    lept_free(v);
    v->u.s.s = (char*)malloc(len+1);
    memcpy(v->u.s.s, s, len);
    v->u.s.s[len] = '\0';
    v->u.s.len = len;
    v->type = LEPT_STRING;
}

char* lept_get_string(const lept_value* v) {
    assert(v != NULL && v->type == LEPT_STRING);
    return v->u.s.s;
}

size_t lept_get_string_length(const lept_value* v) {
    assert(v != NULL && v->type == LEPT_STRING);
    return v->u.s.len;
}

size_t lept_get_array_size(const lept_value* v) {
    assert(v != NULL && v->type == LEPT_ARRAY);
    return v->u.a.size;
}

lept_value* lept_get_array_element(const lept_value* v, size_t index) {
    assert(v != NULL && v->type == LEPT_ARRAY && (index >= 0 && index < v->u.a.size));
    return &(v->u.a.e[index]);
}

static void lept_stringfy_string(lept_context* c, const char* s, size_t len) {
    static const char hex_digits[] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
    assert(s != NULL);
    size_t i, size;
    char *p, *head;
    p = head = lept_context_push(c, size=len*6+2);
    *p++ = '"';
    for (i = 0; i < size; i++)
    {
        unsigned char ch = (unsigned char)s[i];
        switch(ch) {
            case '\"': *p++ = '\\'; *p++ = '\"'; break;
            case '\\': *p++ = '\\'; *p++ = '\\'; break;
            case '\b': *p++ = '\\'; *p++ = 'b';  break;
            case '\f': *p++ = '\\'; *p++ = 'f';  break;
            case '\n': *p++ = '\\'; *p++ = 'n';  break;
            case '\r': *p++ = '\\'; *p++ = 'r';  break;
            case '\t': *p++ = '\\'; *p++ = 't';  break;
            default:
                if(ch < 0x20) {
                    *p++ = '\\'; *p++ = 'u'; *p++ = '0'; *p++ = '0';
                    *p++ = hex_digits[ch >> 4];
                    *p++ = hex_digits[ch & 15];
                } else 
                    *p++ = s[i];
        }
    }
    *p++ = '"';
    c->top -= size - (p - head);
} 

static int lept_stringfy_value(lept_context* c, const lept_value* v) {
    size_t i;
    int ret;
    switch(v->type) {
        case LEPT_NULL:
            PUTS(c, "null", 4);
            break;
        case LEPT_TRUE:
            PUTS(c, "true", 4);
            break;
        case LEPT_FALSE:
            PUTS(c, "false", 5);
            break;
        case LEPT_NUMBER: {
            char* buffer = lept_context_push(c, 32);
            int length = sprintf(buffer, "%.17g", v->u.n);
            c->top -= 32 - length;
            break;
        }
        case LEPT_STRING:
            lept_stringfy_string(c, v->u.s.s, v->u.s.len);
            break;
        case LEPT_ARRAY:
            PUT_CH(c, '[');
            for(i = 0; i < v->u.a.size; i++) {
                if(i > 0)
                    PUT_CH(c, ',');
                lept_stringfy_value(c, &v->u.a.e[i]);
            }
            PUT_CH(c, ']');
            break;
        case LEPT_OBJECT:
            PUT_CH(c, '{');
            for(i = 0; i < v->u.o.size; i++) {
                if(i > 0)
                    PUT_CH(c, ',');
                lept_stringfy_string(c, v->u.o.m[i].k, v->u.o.m[i].klen);
                PUT_CH(c, ':');
                lept_stringfy_value(c, &v->u.o.m[i].v);
            }
            PUT_CH(c, '}');
            break;
        default:
            break;
    }
    return LEPT_STRINGIFY_OK;
}

int lept_stringfy(const lept_value* v, char** json, size_t* length) {
    lept_context c;
    int ret;
    assert(v != NULL);
    assert(json != NULL);
    c.stack = (char*)malloc(c.size = LEPT_PARSE_STRINGFY_INIT_SIZE);
    c.top = 0;
    if((ret=lept_stringfy_value(&c, v) != LEPT_STRINGIFY_OK)) {
        free(c.stack);
        *json = NULL;
        return ret;
    }
    if(length) 
        *length = c.top;
    PUT_CH(&c, '\0');
    *json = c.stack;
    return LEPT_STRINGIFY_OK;
}