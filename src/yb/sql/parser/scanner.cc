/*--------------------------------------------------------------------------------------------------
 * Portions Copyright (c) YugaByte, Inc.
 * Portions Copyright (c) 1996-2015, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * API for the core scanner (flex machine).
 *--------------------------------------------------------------------------------------------------
 */

#include <ctype.h>
#include <assert.h>

#include "yb/sql/parser/scanner_util.h"
#include "yb/sql/parser/parser_nodes.h"
#include "yb/util/logging.h"

// Include YACC-generated header file.
#include "yb/sql/parser_gram.hpp"

//--------------------------------------------------------------------------------------------------
#define PG_KEYWORD(a,b,c) {a,b,c},

const ScanKeyword ScanKeywords[] = {
#include "yb/sql/kwlist.h"
};

const ScanKeyword *ScanKeywordLookup(const char *text,
                                     const ScanKeyword *keywords,
                                     int num_keywords) {
  int     len;
  int     i;
  char    word[NAMEDATALEN];
  const   ScanKeyword *low;
  const   ScanKeyword *high;

  /* We assume all keywords are shorter than NAMEDATALEN. */
  len = strlen(text);
  if (len >= NAMEDATALEN) {
    return NULL;
  }

  /*
   * Apply an ASCII-only downcasing.  We must not use tolower() since it may
   * produce the wrong translation in some locales (eg, Turkish).
   */
  for (i = 0; i < len; i++)
  {
    char    ch = text[i];

    if (ch >= 'A' && ch <= 'Z')
      ch += 'a' - 'A';
    word[i] = ch;
  }
  word[len] = '\0';

  /*
   * Now do a binary search using plain strcmp() comparison.
   */
  low = keywords;
  high = keywords + (num_keywords - 1);
  while (low <= high)
  {
    const ScanKeyword *middle;
    int     difference;

    middle = low + (high - low) / 2;
    difference = strcmp(middle->name, word);
    if (difference == 0)
      return middle;
    else if (difference < 0)
      low = middle + 1;
    else
      high = middle - 1;
  }

  return NULL;
}

//--------------------------------------------------------------------------------------------------

const int NumScanKeywords = lengthof(ScanKeywords);
int YbSql_yylex(YbSql_YYSTYPE *lvalp, YYLTYPE *llocp, YbSql_yyscan_t yyscanner) {
  Parser_extra_type   *yyextra = pg_yyget_extra(yyscanner);
  int                  cur_token;
  int                  next_token;
  int                  cur_token_length;
  YYLTYPE              cur_yylloc;

  /* Get next token --- we might already have it */
  if (yyextra->have_lookahead) {
    cur_token = yyextra->lookahead_token;
    lvalp->ybsql_scan_yystype = yyextra->lookahead_yylval;
    *llocp = yyextra->lookahead_yylloc;
    *(yyextra->lookahead_end) = yyextra->lookahead_hold_char;
    yyextra->have_lookahead = false;
  } else {
    cur_token = YbSqlScan_yylex(&(lvalp->ybsql_scan_yystype), llocp, yyscanner);
  }

  /*
   * If this token isn't one that requires lookahead, just return it.  If it
   * does, determine the token length.  (We could get that via strlen(), but
   * since we have such a small set of possibilities, hardwiring seems
   * feasible and more efficient.)
   */
  switch (cur_token) {
    case NOT:
      cur_token_length = 3;
      break;
    case NULLS_P:
      cur_token_length = 5;
      break;
    case WITH:
      cur_token_length = 4;
      break;
    default:
      return cur_token;
  }

  /*
   * Identify end+1 of current token.  YbSqlScan_yylex() has temporarily stored a
   * '\0' here, and will undo that when we call it again.  We need to redo
   * it to fully revert the lookahead call for error reporting purposes.
   */
  yyextra->lookahead_end = yyextra->ybsql_yy_extra.scanbuf +
    *llocp + cur_token_length;
  assert(*(yyextra->lookahead_end) == '\0');

  /*
   * Save and restore *llocp around the call.  It might look like we could
   * avoid this by just passing &lookahead_yylloc to YbSqlScan_yylex(), but that
   * does not work because flex actually holds onto the last-passed pointer
   * internally, and will use that for error reporting.  We need any error
   * reports to point to the current token, not the next one.
   */
  cur_yylloc = *llocp;

  /* Get next token, saving outputs into lookahead variables */
  next_token = YbSqlScan_yylex(&(yyextra->lookahead_yylval), llocp, yyscanner);
  yyextra->lookahead_token = next_token;
  yyextra->lookahead_yylloc = *llocp;

  *llocp = cur_yylloc;

  /* Now revert the un-truncation of the current token */
  yyextra->lookahead_hold_char = *(yyextra->lookahead_end);
  *(yyextra->lookahead_end) = '\0';

  yyextra->have_lookahead = true;

  /* Replace cur_token if needed, based on lookahead */
  switch (cur_token)
  {
    case NOT:
      /* Replace NOT by NOT_LA if it's followed by BETWEEN, IN, etc */
      switch (next_token)
      {
        case BETWEEN:
        case IN_P:
        case LIKE:
        case ILIKE:
        case SIMILAR:
          cur_token = NOT_LA;
          break;
      }
      break;

    case NULLS_P:
      /* Replace NULLS_P by NULLS_LA if it's followed by FIRST or LAST */
      switch (next_token)
      {
        case FIRST_P:
        case LAST_P:
          cur_token = NULLS_LA;
          break;
      }
      break;

    case WITH:
      /* Replace WITH by WITH_LA if it's followed by TIME or ORDINALITY */
      switch (next_token)
      {
        case TIME:
        case ORDINALITY:
          cur_token = WITH_LA;
          break;
      }
      break;
  }

  return cur_token;
}
