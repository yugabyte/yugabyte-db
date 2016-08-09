/*--------------------------------------------------------------------------------------------------
 * Portions Copyright (c) YugaByte, Inc.
 * Portions Copyright (c) 1996-2015, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * Definitions for the "raw" parser (flex and bison phases only)
 * This is the external API for the raw lexing/parsing functions.
 *-------------------------------------------------------------------------
 */
#ifndef YB_SQL_PARSER_PARSER_H
#define YB_SQL_PARSER_PARSER_H

#include <stdlib.h>
#include <ctype.h>

// Fake the list datatype to compile. We'll replace Postgres List with standard list.
typedef int64_t List;

// The YY_EXTRA data that a flex scanner allows us to pass around.  Private
// state needed for raw parsing/lexing goes here.
typedef struct Parser_extra_type {
  // Fields used by the core scanner.
  YbSql_yy_extra_type     ybsql_yy_extra;

  // State variables for YbSql_yylex().
  bool                    have_lookahead;                            /* is lookahead info valid? */
  int                     lookahead_token;                                /* one-token lookahead */
  YbSqlScan_YYSTYPE       lookahead_yylval;                        /* yylval for lookahead token */
  YYLTYPE                 lookahead_yylloc;                        /* yylloc for lookahead token */
  char                   *lookahead_end;                                 /* end of current token */
  char                    lookahead_hold_char;               /* to be put back at *lookahead_end */

  // State variables that belong to the grammar.
  List                   *parsetree;                     /* final parse result is delivered here */
} Parser_extra_type;

// Primary entry point for the raw parsing functions.
extern List *SqlParse(const char *str);

#endif // YB_SQL_PARSER_PARSER_H
