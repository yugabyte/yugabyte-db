/*-------------------------------------------------------------------------
 * Portions Copyright (c) YugaByte, Inc.
 * Portions Copyright (c) 1996-2015, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * Main entry point/driver for PostgreSQL grammar
 *
 * Note that the grammar is not allowed to perform any table access
 * (since we need to be able to do basic parsing even while inside an
 * aborted transaction).  Therefore, the data structures returned by
 * the grammar are "raw" parsetrees that still need to be analyzed by
 * analyze.c and related files.
 *-------------------------------------------------------------------------
 */
#include "yb/sql/parser/scanner_util.h"
#include "yb/sql/parser/parser_nodes.h"
#include "yb/sql/parser/parser.h"

// Include YACC-generated header for parsing.
#include "yb/sql/parser_gram.hpp"

// Initialize to parse one query string.
extern void parser_init(Parser_extra_type *yyext);

/*
 * SqlParse
 *    Given a query in string form, do lexical and grammatical analysis.
 *
 * Returns a list of raw (un-analyzed) parse trees.
 */
List *SqlParse(const char *str)
{
  YbSql_yyscan_t      yyscanner;
  Parser_extra_type   yyextra;
  int                 yyresult;

  /* initialize the flex scanner */
  yyscanner = scanner_init(str, &yyextra.ybsql_yy_extra, ScanKeywords, NumScanKeywords);

  /* base_yylex() only needs this much initialization */
  yyextra.have_lookahead = false;

  /* initialize the bison parser */
  parser_init(&yyextra);

  /* Parse! */
  yyresult = YbSql_yyparse(yyscanner);

  /* Clean up (release memory) */
  scanner_finish(yyscanner);

  if (yyresult)       /* error */
    return NULL;

  return yyextra.parsetree;
}
