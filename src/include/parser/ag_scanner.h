/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
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
    AG_TOKEN_CHAR
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
