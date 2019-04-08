#ifndef AG_SCAN_H
#define AG_SCAN_H

// AG_TOKEN_NULL indicates the end of a scan. The name came from YY_NULL.
//
// AG_TOKEN_DECIMAL can be a decimal, an octal, or a hexadecimal integer
// literal that does not fit in "int" type.
enum ag_token_type {
    AG_TOKEN_NULL,
    AG_TOKEN_INTEGER,
    AG_TOKEN_DECIMAL,
    AG_TOKEN_STRING,
    AG_TOKEN_IDENTIFIER,
    AG_TOKEN_PARAMETER,
    AG_TOKEN_LT_GT,
    AG_TOKEN_LT_EQ,
    AG_TOKEN_GT_EQ,
    AG_TOKEN_DOT_DOT,
    AG_TOKEN_PLUS_EQ,
    AG_TOKEN_EQ_TILDE,
    AG_TOKEN_CHAR
};

// Fields in value field are used with the following types.
//
//     * c: AG_TOKEN_CHAR
//     * i: AG_TOKEN_INTEGER
//     * s: all other types except the types for c and i, and AG_TOKEN_NULL
//
// "int" type is chosen for value.i to line it up with Value in PostgreSQL.
//
// value.s is read-only because it points at an internal buffer and it changes
// for every ag_scanner_next_token() call. So, users who want to keep or modify
// the value need to copy it first.
struct ag_token {
    enum ag_token_type type;
    union {
        char c;
        int i;
        const char *s;
    } value;
    int location;
};

// an opaque data structure encapsulating the current state of the scanner
typedef void *ag_scanner_t;

ag_scanner_t ag_scanner_create(const char *s);
void ag_scanner_destroy(ag_scanner_t scanner);
struct ag_token ag_scanner_next_token(ag_scanner_t scanner);

#endif
