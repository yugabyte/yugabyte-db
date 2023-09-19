/*
 * For PostgreSQL Database Management System:
 * (formerly known as Postgres, then as Postgres95)
 *
 * Portions Copyright (c) 1996-2010, The PostgreSQL Global Development Group
 *
 * Portions Copyright (c) 1994, The Regents of the University of California
 *
 * Permission to use, copy, modify, and distribute this software and its documentation for any purpose,
 * without fee, and without a written agreement is hereby granted, provided that the above copyright notice
 * and this paragraph and the following two paragraphs appear in all copies.
 *
 * IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO ANY PARTY FOR DIRECT,
 * INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING LOST PROFITS,
 * ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY
 * OF CALIFORNIA HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING,
 * BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 * THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS" BASIS, AND THE UNIVERSITY OF CALIFORNIA
 * HAS NO OBLIGATIONS TO PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 */

#ifndef AG_AG_SCANNER_H
#define AG_AG_SCANNER_H

/*
 * AG_TOKEN_NULL indicates the end of a scan. The name came from YY_NULL.
 *
 * AG_TOKEN_DECIMAL can be a decimal integer literal that does not fit in "int"
 * type.
 */
typedef enum ag_token_type
{
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
    AG_TOKEN_TYPECAST,
    AG_TOKEN_PLUS_EQ,
    AG_TOKEN_EQ_TILDE,
    AG_TOKEN_ACCESS_PATH,
    AG_TOKEN_ANY_EXISTS,
    AG_TOKEN_ALL_EXISTS,
    AG_TOKEN_CONCAT,
    AG_TOKEN_CHAR,
} ag_token_type;

/*
 * Fields in value field are used with the following types.
 *
 *     * c: AG_TOKEN_CHAR
 *     * i: AG_TOKEN_INTEGER
 *     * s: all other types except the types for c and i, and AG_TOKEN_NULL
 *
 * "int" type is chosen for value.i to line it up with Value in PostgreSQL.
 *
 * value.s is read-only because it points at an internal buffer and it changes
 * for every ag_scanner_next_token() call. So, users who want to keep or modify
 * the value need to copy it first.
 */
typedef struct ag_token
{
    ag_token_type type;
    union
    {
        char c;
        int i;
        const char *s;
    } value;
    int location;
} ag_token;

// an opaque data structure encapsulating the current state of the scanner
typedef void *ag_scanner_t;

ag_scanner_t ag_scanner_create(const char *s);
void ag_scanner_destroy(ag_scanner_t scanner);
ag_token ag_scanner_next_token(ag_scanner_t scanner);

int ag_scanner_errmsg(const char *msg, ag_scanner_t *scanner);
int ag_scanner_errposition(const int location, ag_scanner_t *scanner);

#endif
