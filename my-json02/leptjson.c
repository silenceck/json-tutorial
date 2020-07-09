#include "leptjson.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

typedef struct {
    const char* json;
}lept_context;

/* #define EXPECT(c, ch) do { assert(*c->json == (ch)); c->json++; } while(0) */
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
static int lept_parse_interal(lept_context* c, lept_value* v, const char* interal, lept_type type) {
    int i = 0;
    int len = strlen(interal);
    EXPECT(c, interal[0]);
    while(interal[i+1] != '\0') {
        if(interal[i+1] != c->json[i])
            return LEPT_PARSE_INVALID_VALUE;
        i++;
    }
    c->json += len-1;
    v->type = type;
    return LEPT_PARSE_OK;
}
static bool lept_is_digit(const char* c) {
    return *c >= '0' && *c <= '9'? true: false;
}
static int lept_parse_number(lept_context* c , lept_value* v) {
    char* end;
    const char* p = c->json;
    if(*p == '-')
        p++;
    if(*p == '0') {
        p++;
        if(*p == '.') {
            p++;
            if(*p == '\0') {
                return LEPT_PARSE_INVALID_VALUE;
            }
            while(*p != '\0' && lept_is_digit(p)) {
                p++;
            }
            if(*p == 'e' || *p == 'E') {
                p++;
                if(*p == '+' || *p == '-') {
                    p++;
                }
                while(lept_is_digit(p)){
                    p++;
                }
                if(*p != '\0')
                    return LEPT_PARSE_INVALID_VALUE;
            } else if(*p != '\0')
                return LEPT_PARSE_INVALID_VALUE;
        } else if(*p != '\0')
            return LEPT_PARSE_INVALID_VALUE;
    } else {
        while(lept_is_digit(p)){
            p++;
        }
        if(*p == 'e' || *p == 'E') {
            p++;
            if(*p == '+' || *p == '-') {
                p++;
            }
            while(lept_is_digit(p)){
                p++;
            }
            if(*p != '\0')
                return LEPT_PARSE_INVALID_VALUE;
        } else
            return LEPT_PARSE_INVALID_VALUE;
    }
    v->n = strtod(c->json, &end);
    if (c->json == end)
        return LEPT_PARSE_INVALID_VALUE;
    c->json = end;
    v->type = LEPT_NUMBER;
    return LEPT_PARSE_OK;
}
static int lept_parse_value(lept_context* c, lept_value* v) {
    const char* p = c->json;
    switch (*p) {
        case 'n': return lept_parse_interal(c, v, "null", LEPT_NULL);
        case 't': return lept_parse_interal(c, v, "true", LEPT_TRUE);
        case 'f': return lept_parse_interal(c, v, "false", LEPT_FALSE);
        default: return lept_parse_number(c, v);
        case '\0': return LEPT_PARSE_EXPECT_VALUE;

    }
}
int lept_parse(lept_value* v, const char* json) {
    lept_context c;
    int ret;
    assert(v != NULL);
    c.json = json;
    v->type = LEPT_NULL;
    lept_parse_whitespace(&c);
    ret = lept_parse_value(&c, v);
    if( ret == LEPT_PARSE_OK) {
        lept_parse_whitespace((&c));
        if(c.json[0] != '\0')
            ret = LEPT_PARSE_ROOT_NOT_SINGULAR;
    }
    return ret;
}
lept_type lept_get_type(const lept_value* v) {
    assert(v != NULL);
    return v->type;
}

double lept_get_double(const lept_value* v) {
    assert(v != NULL && v->type == LEPT_NUMBER);
    return v->n;
}


