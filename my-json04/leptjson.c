#include "leptjson.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>  /* errno, ERANGE */
#include <math.h>   /* HUGE_VAL */

#ifndef LEPT_PARSE_STACK_INIT_SIZE
#define  LEPT_PARSE_STACK_INIT_SIZE 256
#endif

typedef struct {
    const char* json;
    char* stack;
    size_t size, top;
}lept_context;

/* #define EXPECT(c, ch) do { assert(*c->json == (ch)); c->json++; } while(0) */
#define IS_DIGIT(ch)        ((ch) >= '0' && (ch) <= '9')
#define IS_DIGIT1TO9(ch)    ((ch) >= '1' && (ch) <= '9')
#define IS_HEX(ch)        (((ch) >= '1' && (ch) <= '9') || ((ch) >= 'A' && (ch) <= 'F'))
#define PUT_CH(c, ch) do { *(char*)lept_context_push(c, sizeof(char)) = (ch);} while(0)

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
    assert(v != NULL);
    if (v->type == LEPT_STRING) {
        free(v->u.s.s);
    } 
    v->type = LEPT_NULL;
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
    if(*p != '\0') return LEPT_PARSE_INVALID_VALUE;
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

static int lept_parse_value(lept_context* c, lept_value* v) {
    const char* p = c->json;
    switch (*p) {
        case 'n': return lept_parse_interal(c, v, "null", LEPT_NULL);
        case 't': return lept_parse_interal(c, v, "true", LEPT_TRUE);
        case 'f': return lept_parse_interal(c, v, "false", LEPT_FALSE);
        case '\"': return lept_parse_string(c, v);
        default:  return lept_parse_number(c, v);
        case '\0': return LEPT_PARSE_EXPECT_VALUE;

    }
}

static void* lept_context_push(lept_context* c, size_t size) {
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
static int lept_parse_string(lept_context* c, lept_value* v) {
    size_t head = c->top, len;
    const char* p;
    EXPECT(c, '\"');
    p = c->json;
    while(true) {
        char ch = *p++;
        switch(ch) {
            case '\"': 
                len = c->top - head;
                lept_set_string(v, (const char*)lept_context_pop(c, len), len);
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
                        PUT_CH(c, '\\');
                        PUT_CH(c, 'u');
                        int i = 4;
                        while(i--) {
                            if(IS_HEX(*p)) {
                                PUT_CH(c, *p);
                                p++;
                            } else {
                                c->top = head;
                                return LEPT_PARSE_INVALID_STRING_ESCAPE;
                            }
                        }
                        if(*(p-4) == 'D' && ((*(p-3) >= '8' && *(p-3) <= '9') || (*(p-3) >= 'A' && *(p-3) <= 'B')) && \
                        (IS_DIGIT(*(p-2)) || (*(p-2) >= 'A' && *(p-2) <= 'F')) && (IS_DIGIT(*(p-1)) || (*(p-1) >= 'A' && *(p-1) <= 'F'))) {
                            if(*p == '\\') {
                                p++;
                                PUT_CH(c, '\\');
                                if(*p == 'u') {
                                    PUT_CH(c, 'u');
                                    p++;
                                    if(*p == 'D' && (*(p+1) >= 'C' && *(p+1) <= 'F') && (IS_DIGIT(*(p+2)) || (*(p+2) >= 'A' && *(p+2) <= 'F'))\
                                    && (IS_DIGIT(*(p+3)) || (*(p+3) >= 'A' && *(p+3) <= 'F'))) {
                                        PUT_CH(c, *p++);
                                        PUT_CH(c, *p++);
                                        PUT_CH(c, *p++);
                                        PUT_CH(c, *p++);
                                        break;
                                    } else {
                                        c->top = head;
                                        return LEPT_PARSE_INVALID_STRING_ESCAPE;
                                    }
                                } else {
                                    c->top = head;
                                    return LEPT_PARSE_INVALID_STRING_ESCAPE;
                                }
                            } else {
                                c->top = head;
                                return LEPT_PARSE_INVALID_STRING_ESCAPE;
                            }

                        }
                        

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
    if( ret == LEPT_PARSE_OK) {
        lept_parse_whitespace((&c));
        if(c.json[0] != '\0')
            ret = LEPT_PARSE_ROOT_NOT_SINGULAR;
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


