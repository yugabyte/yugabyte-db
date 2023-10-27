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

#include "postgres.h"

#include "nodes/pg_list.h"
#include "parser/scansup.h"

#include "parser/ag_scanner.h"
#include "parser/cypher_gram.h"
#include "parser/cypher_keywords.h"
#include "parser/cypher_parser.h"

int cypher_yylex(YYSTYPE *lvalp, YYLTYPE *llocp, ag_scanner_t scanner)
{
    /*
     * This list must match ag_token_type.
     * 0 means end-of-input.
     */
    const int type_map[] = {
        0,
        INTEGER,
        DECIMAL,
        STRING,
        IDENTIFIER,
        PARAMETER,
        NOT_EQ,
        LT_EQ,
        GT_EQ,
        DOT_DOT,
        TYPECAST,
        PLUS_EQ,
        EQ_TILDE,
        LEFT_CONTAINS,
        RIGHT_CONTAINS,
        ACCESS_PATH,
        ANY_EXISTS,
        ALL_EXISTS,
        CONCAT
    };

    ag_token token;

    token = ag_scanner_next_token(scanner);

    switch (token.type)
    {
    case AG_TOKEN_NULL:
        break;
    case AG_TOKEN_INTEGER:
        lvalp->integer = token.value.i;
        break;
    case AG_TOKEN_DECIMAL:
    case AG_TOKEN_STRING:
        lvalp->string = pstrdup(token.value.s);
        break;
    case AG_TOKEN_IDENTIFIER:
    {
        int kwnum;
        char *ident;

        kwnum = ScanKeywordLookup(token.value.s, &CypherKeyword);
        if (kwnum >= 0)
        {
            /*
             * use token.value.s instead of keyword->name to preserve
             * case sensitivity
             */
            lvalp->keyword = GetScanKeyword(kwnum, &CypherKeyword);
            ident = pstrdup(token.value.s);
            truncate_identifier(ident, strlen(ident), true);
            lvalp->string = ident;
            *llocp = token.location;
            return CypherKeywordTokens[kwnum];
        }

        ident = pstrdup(token.value.s);
        truncate_identifier(ident, strlen(ident), true);
        lvalp->string = ident;
        break;
    }
    case AG_TOKEN_PARAMETER:
        lvalp->string = pstrdup(token.value.s);
        break;
    case AG_TOKEN_LT_GT:
    case AG_TOKEN_LT_EQ:
    case AG_TOKEN_GT_EQ:
    case AG_TOKEN_DOT_DOT:
    case AG_TOKEN_PLUS_EQ:
    case AG_TOKEN_EQ_TILDE:
    case AG_TOKEN_ACCESS_PATH:
    case AG_TOKEN_ALL_EXISTS:
    case AG_TOKEN_ANY_EXISTS:
    case AG_TOKEN_LEFT_CONTAINS:
    case AG_TOKEN_RIGHT_CONTAINS:
    case AG_TOKEN_CONCAT:
        break;
    case AG_TOKEN_TYPECAST:
        break;
    case AG_TOKEN_CHAR:
        *llocp = token.location;
        return token.value.c;
    default:
        ereport(ERROR, (errmsg("unexpected ag_token_type: %d", token.type)));
        break;
    }

    *llocp = token.location;
    return type_map[token.type];
}

void cypher_yyerror(YYLTYPE *llocp, ag_scanner_t scanner,
                    cypher_yy_extra *extra, const char *msg)
{
    ereport(ERROR, (errcode(ERRCODE_SYNTAX_ERROR),
                    ag_scanner_errmsg(msg, scanner),
                    ag_scanner_errposition(*llocp, scanner)));
}

/* declaration to make mac os x compiler happy */
int cypher_yyparse(ag_scanner_t scanner, cypher_yy_extra *extra);

List *parse_cypher(const char *s)
{
    ag_scanner_t scanner;
    cypher_yy_extra extra;
    int yyresult;

    scanner = ag_scanner_create(s);
    extra.result = NIL;
    extra.extra = NULL;

    yyresult = cypher_yyparse(scanner, &extra);

    ag_scanner_destroy(scanner);

    /*
     * cypher_yyparse() returns 0 if parsing was successful.
     * Otherwise, it returns 1 (invalid input) or 2 (memory exhaustion).
     */
    if (yyresult)
        return NIL;

    /*
     * Append the extra node regardless of its value. Currently the extra
     * node is only used by EXPLAIN
    */
    return lappend(extra.result, extra.extra);
}
