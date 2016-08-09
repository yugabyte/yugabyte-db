%{
/*--------------------------------------------------------------------------------------------------
 * Portions Copyright (c) YugaByte, Inc.
 * Portions Copyright (c) 1996-2015, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * Lexical scanner for PostgreSQL
 *
 * The rules in this file must be kept in sync with psql's lexer!!!
 *
 * The rules are designed so that the scanner never has to backtrack,
 * in the sense that there is always a rule that can match the input
 * consumed so far (the rule action may internally throw back some input
 * with yyless(), however).  As explained in the flex manual, this makes
 * for a useful speed increase --- about a third faster than a plain -CF
 * lexer, in simple testing.  The extra complexity is mostly in the rules
 * for handling float numbers and continued string literals.  If you change
 * the lexical rules, verify that you haven't broken the no-backtrack
 * property by running flex with the "-b" option and checking that the
 * resulting "lex.backup" file says that no backing up is needed.  (As of
 * Postgres 9.2, this check is made automatically by the Makefile.)
 *--------------------------------------------------------------------------------------------------
 */
#include <ctype.h>
#include <unistd.h>

#include "yb/sql/errcodes.h"
#include "yb/sql/parser/scanner_util.h"
#include "yb/sql/parser/parser_nodes.h"
#include "yb/util/logging.h"

// Generated header file by YACC.
#include "yb/sql/parser_gram.hpp"

// Token types.
#define YYSTYPE YbSqlScan_YYSTYPE

/*
 * GUC variables.  This is a DIRECT violation of the warning given at the
 * head of gram.y, ie flex/bison code must not depend on any GUC variables;
 * as such, changing their values can induce very unintuitive behavior.
 * But we shall have to live with it as a short-term thing until the switch
 * to SQL-standard string syntax is complete.
 */
int  backslash_quote = BACKSLASH_QUOTE_SAFE_ENCODING;
bool escape_string_warning = true;
bool standard_conforming_strings = true;

/*
 * Each call to yylex must set yylloc to the location of the found token
 * (expressed as a byte offset from the start of the input text).
 * When we parse a token that requires multiple lexer rules to process,
 * this should be done in the first such rule, else yylloc will point
 * into the middle of the token.
 */
#define SET_YYLLOC()  (*(yylloc) = yytext - yyextra->scanbuf)

/*
 * Advance yylloc by the given number of bytes.
 */
#define ADVANCE_YYLLOC(delta)  ( *(yylloc) += (delta) )

#define startlit()  ( yyextra->literallen = 0 )

#define yyerror(msg)  scanner_yyerror(msg, yyscanner)

#define lexer_errposition()  scanner_errposition(*(yylloc), yyscanner)

static void addlit(char *ytext, int yleng, YbSql_yyscan_t yyscanner);
static void addlitchar(unsigned char ychar, YbSql_yyscan_t yyscanner);
static char *litbufdup(YbSql_yyscan_t yyscanner);
static char *litbuf_udeescape(unsigned char escape, YbSql_yyscan_t yyscanner);
static unsigned char unescape_single_char(unsigned char c, YbSql_yyscan_t yyscanner);
static int  process_integer_literal(const char *token, YYSTYPE *lval);
static bool is_utf16_surrogate_first(pg_wchar c);
static bool is_utf16_surrogate_second(pg_wchar c);
static pg_wchar surrogate_pair_to_codepoint(pg_wchar first, pg_wchar second);
static void addunicode(pg_wchar c, yyscan_t yyscanner);
static bool check_uescapechar(unsigned char escape);
static void check_string_escape_warning(unsigned char ychar, YbSql_yyscan_t yyscanner);
static void check_escape_warning(YbSql_yyscan_t yyscanner);
static char *downcase_truncate_identifier(const char *ident, int len, bool warn);
static void truncate_identifier(char *ident, int len, bool warn);
%}

%option reentrant
%option bison-bridge
%option bison-locations
%option 8bit
%option never-interactive

%option nodefault
%option noinput
%option nounput
%option noyywrap
%option warn
%option prefix="YbSqlScan_yy"

/*
 * OK, here is a short description of lex/flex rules behavior.
 * The longest pattern which matches an input string is always chosen.
 * For equal-length patterns, the first occurring in the rules list is chosen.
 * INITIAL is the starting state, to which all non-conditional rules apply.
 * Exclusive states change parsing rules while the state is active.  When in
 * an exclusive state, only those rules defined for that state apply.
 *
 * We use exclusive states for quoted strings, extended comments,
 * and to eliminate parsing troubles for numeric strings.
 * Exclusive states:
 *  <xb> bit string literal
 *  <xc> extended C-style comments
 *  <xd> delimited identifiers (double-quoted identifiers)
 *  <xh> hexadecimal numeric string
 *  <xq> standard quoted strings
 *  <xe> extended quoted strings (support backslash escape sequences)
 *  <xdolq> $foo$ quoted strings
 *  <xui> quoted identifier with Unicode escapes
 *  <xuiend> end of a quoted identifier with Unicode escapes, UESCAPE can follow
 *  <xus> quoted string with Unicode escapes
 *  <xusend> end of a quoted string with Unicode escapes, UESCAPE can follow
 *  <xeu> Unicode surrogate pair in extended quoted string
 *
 * Remember to add an <<EOF>> case whenever you add a new exclusive state!
 * The default one is probably not the right thing.
 */

%x xb
%x xc
%x xd
%x xh
%x xe
%x xq
%x xdolq
%x xui
%x xuiend
%x xus
%x xusend
%x xeu

/*
 * In order to make the world safe for Windows and Mac clients as well as
 * Unix ones, we accept either \n or \r as a newline.  A DOS-style \r\n
 * sequence will be seen as two successive newlines, but that doesn't cause
 * any problems.  Comments that start with -- and extend to the next
 * newline are treated as equivalent to a single whitespace character.
 *
 * NOTE a fine point: if there is no newline following --, we will absorb
 * everything to the end of the input as a comment.  This is correct.  Older
 * versions of Postgres failed to recognize -- as a comment if the input
 * did not end with a newline.
 *
 * XXX perhaps \f (formfeed) should be treated as a newline as well?
 *
 * XXX if you change the set of whitespace characters, fix scanner_isspace()
 * to agree, and see also the plpgsql lexer.
 */

space                     [ \t\n\r\f]
horiz_space               [ \t\f]
newline                   [\n\r]
non_newline               [^\n\r]

comment                   ("--"{non_newline}*)

whitespace                ({space}+|{comment})

/*
 * SQL requires at least one newline in the whitespace separating
 * string literals that are to be concatenated.  Silly, but who are we
 * to argue?  Note that {whitespace_with_newline} should not have * after
 * it, whereas {whitespace} should generally have a * after it...
 */

special_whitespace        ({space}+|{comment}{newline})
horiz_whitespace          ({horiz_space}|{comment})
whitespace_with_newline   ({horiz_whitespace}*{newline}{special_whitespace}*)

/*
 * To ensure that {quotecontinue} can be scanned without having to back up
 * if the full pattern isn't matched, we include trailing whitespace in
 * {quotestop}.  This matches all cases where {quotecontinue} fails to match,
 * except for {quote} followed by whitespace and just one "-" (not two,
 * which would start a {comment}).  To cover that we have {quotefail}.
 * The actions for {quotestop} and {quotefail} must throw back characters
 * beyond the quote (') proper.
 */
/* quote (') */
quote                     '
quotestop                 {quote}{whitespace}*
quotecontinue             {quote}{whitespace_with_newline}{quote}
quotefail                 {quote}{whitespace}*"-"

/* Bit string
 * It is tempting to scan the string for only those characters
 * which are allowed. However, this leads to silently swallowed
 * characters if illegal characters are included in the string.
 * For example, if xbinside is [01] then B'ABCD' is interpreted
 * as a zero-length string, and the ABCD' is lost!
 * Better to pass the string forward and let the input routines
 * validate the contents.
 */
xbstart                   [bB]{quote}
xbinside                  [^']*

/* Hexadecimal number */
xhstart                   [xX]{quote}
xhinside                  [^']*

/* National character */
xnstart                   [nN]{quote}

/* Quoted string that allows backslash escapes */
xestart                   [eE]{quote}
xeinside                  [^\\']+
xeescape                  [\\][^0-7]
xeoctesc                  [\\][0-7]{1,3}
xehexesc                  [\\]x[0-9A-Fa-f]{1,2}
xeunicode                 [\\](u[0-9A-Fa-f]{4}|U[0-9A-Fa-f]{8})
xeunicodefail             [\\](u[0-9A-Fa-f]{0,3}|U[0-9A-Fa-f]{0,7})

/* Extended quote
 * xqdouble implements embedded quote, ''''
 */
xqstart                   {quote}
xqdouble                  {quote}{quote}
xqinside                  [^']+

/* $foo$ style quotes ("dollar quoting")
 * The quoted string starts with $foo$ where "foo" is an optional string
 * in the form of an identifier, except that it may not contain "$",
 * and extends to the first occurrence of an identical string.
 * There is *no* processing of the quoted text.
 *
 * {dolqfailed} is an error rule to avoid scanner backup when {dolqdelim}
 * fails to match its trailing "$".
 */
dolq_start                [A-Za-z\200-\377_]
dolq_cont                 [A-Za-z\200-\377_0-9]
dolqdelim                 \$({dolq_start}{dolq_cont}*)?\$
dolqfailed                \${dolq_start}{dolq_cont}*
dolqinside                [^$]+

/* Double quote
 * Allows embedded spaces and other special characters into identifiers.
 */
dquote                    \"
xdstart                   {dquote}
xdstop                    {dquote}
xddouble                  {dquote}{dquote}
xdinside                  [^"]+

/* Unicode escapes */
uescape                   [uU][eE][sS][cC][aA][pP][eE]{whitespace}*{quote}[^']{quote}
/* error rule to avoid backup */
uescapefail               [uU][eE][sS][cC][aA][pP][eE]{whitespace}*"-"|[uU][eE][sS][cC][aA][pP][eE]{whitespace}*{quote}[^']|[uU][eE][sS][cC][aA][pP][eE]{whitespace}*{quote}|[uU][eE][sS][cC][aA][pP][eE]{whitespace}*|[uU][eE][sS][cC][aA][pP]|[uU][eE][sS][cC][aA]|[uU][eE][sS][cC]|[uU][eE][sS]|[uU][eE]|[uU]

/* Quoted identifier with Unicode escapes */
xuistart                  [uU]&{dquote}

/* Quoted string with Unicode escapes */
xusstart                  [uU]&{quote}

/* Optional UESCAPE after a quoted string or identifier with Unicode escapes. */
xustop1                   {uescapefail}?
xustop2                   {uescape}

/* error rule to avoid backup */
xufailed                  [uU]&


/* C-style comments
 *
 * The "extended comment" syntax closely resembles allowable operator syntax.
 * The tricky part here is to get lex to recognize a string starting with
 * slash-star as a comment, when interpreting it as an operator would produce
 * a longer match --- remember lex will prefer a longer match!  Also, if we
 * have something like plus-slash-star, lex will think this is a 3-character
 * operator whereas we want to see it as a + operator and a comment start.
 * The solution is two-fold:
 * 1. append {op_chars}* to xcstart so that it matches as much text as
 *    {operator} would. Then the tie-breaker (first matching rule of same
 *    length) ensures xcstart wins.  We put back the extra stuff with yyless()
 *    in case it contains a star-slash that should terminate the comment.
 * 2. In the operator rule, check for slash-star within the operator, and
 *    if found throw it back with yyless().  This handles the plus-slash-star
 *    problem.
 * Dash-dash comments have similar interactions with the operator rule.
 *
 * Add " to negate quote.
 */
xcstart                   \/\*{op_chars}*
xcstop                    \*+\/
xcinside                  [^*/]+

digit                     [0-9]
ident_start               [A-Za-z\200-\377_]
ident_cont                [A-Za-z\200-\377_0-9\$]

identifier                {ident_start}{ident_cont}*

/* Assorted special-case operators and operator-like tokens */
typecast                  "::"
dot_dot                   \.\.
colon_equals              ":="
equals_greater            "=>"
less_equals               "<="
greater_equals            ">="
less_greater              "<>"
not_equals                "!="

/*
 * "self" is the set of chars that should be returned as single-character
 * tokens.  "op_chars" is the set of chars that can make up "Op" tokens,
 * which can be one or more characters long (but if a single-char token
 * appears in the "self" set, it is not to be returned as an Op).  Note
 * that the sets overlap, but each has some chars that are not in the other.
 *
 * If you change either set, adjust the character lists appearing in the
 * rule for "operator"!
 */
self                      [,()\[\].;\:\+\-\*\/\%\^\<\>\=]
op_chars                  [\~\!\@\#\^\&\|\`\?\+\-\*\/\%\<\>\=]
operator                  {op_chars}+

/* we no longer allow unary minus in numbers.
 * instead we pass it separately to parser. there it gets
 * coerced via doNegate() -- Leon aug 20 1999
 *
 * {decimalfail} is used because we would like "1..10" to lex as 1, dot_dot, 10.
 *
 * {realfail1} and {realfail2} are added to prevent the need for scanner
 * backup when the {real} rule fails to match completely.
 */

integer                   {digit}+
decimal                   (({digit}*\.{digit}+)|({digit}+\.{digit}*))
decimalfail               {digit}+\.\.
real                      ({integer}|{decimal})[Ee][-+]?{digit}+
realfail1                 ({integer}|{decimal})[Ee]
realfail2                 ({integer}|{decimal})[Ee][-+]

param                     \${integer}

other                     .

/*
 * Dollar quoted strings are totally opaque, and no escaping is done on them.
 * Other quoted strings must allow some special characters such as single-quote
 *  and newline.
 * Embedded single-quotes are implemented both in the SQL standard
 *  style of two adjacent single quotes "''" and in the Postgres/Java style
 *  of escaped-quote "\'".
 * Other embedded escaped characters are matched explicitly and the leading
 *  backslash is dropped from the string.
 * Note that xcstart must appear before operator, as explained above!
 *  Also whitespace (comment) must appear before operator.
 */

%%
{whitespace} {
  /* ignore */
}

{xcstart} {
  /* Set location in case of syntax error in comment */
  SET_YYLLOC();
  yyextra->xcdepth = 0;
  BEGIN(xc);
  /* Put back any characters past slash-star; see above */
  yyless(2);
}

<xc>{xcstart} {
  (yyextra->xcdepth)++;
  /* Put back any characters past slash-star; see above */
  yyless(2);
}

<xc>{xcstop} {
  if (yyextra->xcdepth <= 0)
    BEGIN(INITIAL);
  else
    (yyextra->xcdepth)--;
}

<xc>{xcinside} {
  /* ignore */
}

<xc>{op_chars} {
  /* ignore */
}

<xc>\*+ {
  /* ignore */
}

<xc><<EOF>> {
  yyerror("unterminated /* comment");
}

{xbstart} {
  /* Binary bit type.
   * At some point we should simply pass the string
   * forward to the parser and label it there.
   * In the meantime, place a leading "b" on the string
   * to mark it for the input routine as a binary string.
   */
  SET_YYLLOC();
  BEGIN(xb);
  startlit();
  addlitchar('b', yyscanner);
}

<xb>{quotestop} |
<xb>{quotefail} {
  yyless(1);
  BEGIN(INITIAL);
  yylval->str = litbufdup(yyscanner);
  return BCONST;
}

<xh>{xhinside} |
<xb>{xbinside}  {
  addlit(yytext, yyleng, yyscanner);
}

<xh>{quotecontinue} |
<xb>{quotecontinue} {
  /* ignore */
}

<xb><<EOF>> {
  yyerror("unterminated bit string literal");
}

{xhstart} {
  /* Hexadecimal bit type.
   * At some point we should simply pass the string
   * forward to the parser and label it there.
   * In the meantime, place a leading "x" on the string
   * to mark it for the input routine as a hex string.
   */
  SET_YYLLOC();
  BEGIN(xh);
  startlit();
  addlitchar('x', yyscanner);
}

<xh>{quotestop} |
<xh>{quotefail} {
  yyless(1);
  BEGIN(INITIAL);
  yylval->str = litbufdup(yyscanner);
  return XCONST;
}

<xh><<EOF>> {
  yyerror("unterminated hexadecimal string literal");
}

{xnstart} {
  /* National character.
   * We will pass this along as a normal character string,
   * but preceded with an internally-generated "NCHAR".
   */
  const ScanKeyword *keyword;

  SET_YYLLOC();
  yyless(1);        /* eat only 'n' this time */

  keyword = ScanKeywordLookup("nchar",
                              yyextra->keywords,
                              yyextra->num_keywords);
  if (keyword != NULL) {
    yylval->keyword = keyword->name;
    return keyword->value;
  } else {
    /* If NCHAR isn't a keyword, just return "n" */
    yylval->str = pstrdup("n");
    return IDENT;
  }
}

{xqstart} {
  yyextra->warn_on_first_escape = true;
  yyextra->saw_non_ascii = false;
  SET_YYLLOC();
  if (yyextra->standard_conforming_strings)
    BEGIN(xq);
  else
    BEGIN(xe);
  startlit();
}

{xestart} {
  yyextra->warn_on_first_escape = false;
  yyextra->saw_non_ascii = false;
  SET_YYLLOC();
  BEGIN(xe);
  startlit();
}

{xusstart} {
  SET_YYLLOC();
  if (!yyextra->standard_conforming_strings)
    LOG(ERROR)
      << "SQL ERROR " << ERRCODE_FEATURE_NOT_SUPPORTED
      << "(Unsafe use of string constant with Unicode escapes): "
      << "String constants with Unicode escapes cannot be used when standard_conforming_strings "
      << "is off";
  BEGIN(xus);
  startlit();
}

<xq,xe>{quotestop} |
<xq,xe>{quotefail} {
  yyless(1);
  BEGIN(INITIAL);
  /*
   * check that the data remains valid if it might have been
   * made invalid by unescaping any chars.
   */
  if (yyextra->saw_non_ascii)
    pg_verify_mbstr_len(yyextra->literalbuf,
                        yyextra->literallen,
                        false);
  yylval->str = litbufdup(yyscanner);
  return SCONST;
}

<xus>{quotestop} |
<xus>{quotefail} {
  /* throw back all but the quote */
  yyless(1);
  /* xusend state looks for possible UESCAPE */
  BEGIN(xusend);
}

<xusend>{whitespace} {
  /* stay in xusend state over whitespace */
}

<xusend>{other} |
<xusend>{xustop1} |
<xusend><<EOF>> {
  /* no UESCAPE after the quote, throw back everything */
  yyless(0);
  BEGIN(INITIAL);
  yylval->str = litbuf_udeescape('\\', yyscanner);
  return SCONST;
}

<xusend>{xustop2} {
  /* found UESCAPE after the end quote */
  BEGIN(INITIAL);
  if (!check_uescapechar(yytext[yyleng-2])) {
    SET_YYLLOC();
    ADVANCE_YYLLOC(yyleng-2);
    yyerror("invalid Unicode escape character");
  }
  yylval->str = litbuf_udeescape(yytext[yyleng-2], yyscanner);
  return SCONST;
}

<xq,xe,xus>{xqdouble} {
  addlitchar('\'', yyscanner);
}

<xq,xus>{xqinside} {
  addlit(yytext, yyleng, yyscanner);
}

<xe>{xeinside} {
  addlit(yytext, yyleng, yyscanner);
}

<xe>{xeunicode} {
  pg_wchar c = strtoul(yytext+2, NULL, 16);

  check_escape_warning(yyscanner);

  if (is_utf16_surrogate_first(c)) {
    yyextra->utf16_first_part = c;
    BEGIN(xeu);
  } else if (is_utf16_surrogate_second(c)) {
    yyerror("invalid Unicode surrogate pair");
  } else {
    addunicode(c, yyscanner);
  }
}

<xeu>{xeunicode} {
  pg_wchar c = strtoul(yytext+2, NULL, 16);

  if (!is_utf16_surrogate_second(c)) {
    yyerror("invalid Unicode surrogate pair");
  }

  c = surrogate_pair_to_codepoint(yyextra->utf16_first_part, c);
  addunicode(c, yyscanner);
  BEGIN(xe);
}

<xeu>.      { yyerror("invalid Unicode surrogate pair"); }
<xeu>\n     { yyerror("invalid Unicode surrogate pair"); }
<xeu><<EOF>>  { yyerror("invalid Unicode surrogate pair"); }

<xe,xeu>{xeunicodefail} {
  LOG(ERROR)
    << "SQL ERROR " << ERRCODE_INVALID_ESCAPE_SEQUENCE
    << "(Invalid Unicode escape): "
    << "Unicode escapes must be \\uXXXX or \\UXXXXXXXX.";
}

<xe>{xeescape}  {
  if (yytext[1] == '\'') {
    if (yyextra->backslash_quote == BACKSLASH_QUOTE_OFF ||
        (yyextra->backslash_quote == BACKSLASH_QUOTE_SAFE_ENCODING)) {
      LOG(ERROR)
        << "SQL ERROR " << ERRCODE_NONSTANDARD_USE_OF_ESCAPE_CHARACTER
        << "(Unsafe use of \\' in a string literal): "
        << "Use '' to write quotes in strings. \\' is insecure in client-only encodings";
    }
  }
  check_string_escape_warning(yytext[1], yyscanner);
  addlitchar(unescape_single_char(yytext[1], yyscanner),
             yyscanner);
}
<xe>{xeoctesc}  {
          unsigned char c = strtoul(yytext+1, NULL, 8);

          check_escape_warning(yyscanner);
          addlitchar(c, yyscanner);
          if (c == '\0' || IS_HIGHBIT_SET(c))
            yyextra->saw_non_ascii = true;
        }
<xe>{xehexesc}  {
          unsigned char c = strtoul(yytext+2, NULL, 16);

          check_escape_warning(yyscanner);
          addlitchar(c, yyscanner);
          if (c == '\0' || IS_HIGHBIT_SET(c))
            yyextra->saw_non_ascii = true;
        }
<xq,xe,xus>{quotecontinue} {
          /* ignore */
        }
<xe>.     {
          /* This is only needed for \ just before EOF */
          addlitchar(yytext[0], yyscanner);
        }
<xq,xe,xus><<EOF>>    { yyerror("unterminated quoted string"); }

{dolqdelim}   {
          SET_YYLLOC();
          yyextra->dolqstart = pstrdup(yytext);
          BEGIN(xdolq);
          startlit();
        }
{dolqfailed}  {
          SET_YYLLOC();
          /* throw back all but the initial "$" */
          yyless(1);
          /* and treat it as {other} */
          return yytext[0];
        }
<xdolq>{dolqdelim} {
          if (strcmp(yytext, yyextra->dolqstart) == 0)
          {
            free(yyextra->dolqstart);
            yyextra->dolqstart = NULL;
            BEGIN(INITIAL);
            yylval->str = litbufdup(yyscanner);
            return SCONST;
          }
          else
          {
            /*
             * When we fail to match $...$ to dolqstart, transfer
             * the $... part to the output, but put back the final
             * $ for rescanning.  Consider $delim$...$junk$delim$
             */
            addlit(yytext, yyleng-1, yyscanner);
            yyless(yyleng-1);
          }
        }
<xdolq>{dolqinside} {
          addlit(yytext, yyleng, yyscanner);
        }
<xdolq>{dolqfailed} {
          addlit(yytext, yyleng, yyscanner);
        }
<xdolq>.    {
          /* This is only needed for $ inside the quoted text */
          addlitchar(yytext[0], yyscanner);
        }
<xdolq><<EOF>>  { yyerror("unterminated dollar-quoted string"); }

{xdstart}   {
          SET_YYLLOC();
          BEGIN(xd);
          startlit();
        }
{xuistart}    {
          SET_YYLLOC();
          BEGIN(xui);
          startlit();
        }
<xd>{xdstop}  {
          char       *ident;

          BEGIN(INITIAL);
          if (yyextra->literallen == 0)
            yyerror("zero-length delimited identifier");
          ident = litbufdup(yyscanner);
          if (yyextra->literallen >= NAMEDATALEN)
            truncate_identifier(ident, yyextra->literallen, true);
          yylval->str = ident;
          return IDENT;
        }
<xui>{dquote} {
          yyless(1);
          /* xuiend state looks for possible UESCAPE */
          BEGIN(xuiend);
        }
<xuiend>{whitespace} { /* stay in xuiend state over whitespace */ }
<xuiend>{other} |
<xuiend>{xustop1} |
<xuiend><<EOF>> {
          /* no UESCAPE after the quote, throw back everything */
          char     *ident;
          int     identlen;

          yyless(0);

          BEGIN(INITIAL);
          if (yyextra->literallen == 0)
            yyerror("zero-length delimited identifier");
          ident = litbuf_udeescape('\\', yyscanner);
          identlen = strlen(ident);
          if (identlen >= NAMEDATALEN)
            truncate_identifier(ident, identlen, true);
          yylval->str = ident;
          return IDENT;
        }
<xuiend>{xustop2} {
          /* found UESCAPE after the end quote */
          char     *ident;
          int     identlen;

          BEGIN(INITIAL);
          if (yyextra->literallen == 0)
            yyerror("zero-length delimited identifier");
          if (!check_uescapechar(yytext[yyleng-2]))
          {
            SET_YYLLOC();
            ADVANCE_YYLLOC(yyleng-2);
            yyerror("invalid Unicode escape character");
          }
          ident = litbuf_udeescape(yytext[yyleng - 2], yyscanner);
          identlen = strlen(ident);
          if (identlen >= NAMEDATALEN)
            truncate_identifier(ident, identlen, true);
          yylval->str = ident;
          return IDENT;
        }
<xd,xui>{xddouble}  {
          addlitchar('"', yyscanner);
        }
<xd,xui>{xdinside}  {
          addlit(yytext, yyleng, yyscanner);
        }
<xd,xui><<EOF>>   { yyerror("unterminated quoted identifier"); }

{xufailed}  {
          char       *ident;

          SET_YYLLOC();
          /* throw back all but the initial u/U */
          yyless(1);
          /* and treat it as {identifier} */
          ident = downcase_truncate_identifier(yytext, yyleng, true);
          yylval->str = ident;
          return IDENT;
        }

{typecast}    {
          SET_YYLLOC();
          return TYPECAST;
        }

{dot_dot}   {
          SET_YYLLOC();
          return DOT_DOT;
        }

{colon_equals}  {
          SET_YYLLOC();
          return COLON_EQUALS;
        }

{equals_greater}  {
          SET_YYLLOC();
          return EQUALS_GREATER;
        }

{less_equals} {
          SET_YYLLOC();
          return LESS_EQUALS;
        }

{greater_equals} {
          SET_YYLLOC();
          return GREATER_EQUALS;
        }

{less_greater}  {
          /* We accept both "<>" and "!=" as meaning NOT_EQUALS */
          SET_YYLLOC();
          return NOT_EQUALS;
        }

{not_equals}  {
          /* We accept both "<>" and "!=" as meaning NOT_EQUALS */
          SET_YYLLOC();
          return NOT_EQUALS;
        }

{self}      {
          SET_YYLLOC();
          return yytext[0];
        }

{operator}    {
          /*
           * Check for embedded slash-star or dash-dash; those
           * are comment starts, so operator must stop there.
           * Note that slash-star or dash-dash at the first
           * character will match a prior rule, not this one.
           */
          int   nchars = yyleng;
          char   *slashstar = strstr(yytext, "/*");
          char   *dashdash = strstr(yytext, "--");

          if (slashstar && dashdash)
          {
            /* if both appear, take the first one */
            if (slashstar > dashdash)
              slashstar = dashdash;
          }
          else if (!slashstar)
            slashstar = dashdash;
          if (slashstar)
            nchars = slashstar - yytext;

          /*
           * For SQL compatibility, '+' and '-' cannot be the
           * last char of a multi-char operator unless the operator
           * contains chars that are not in SQL operators.
           * The idea is to lex '=-' as two operators, but not
           * to forbid operator names like '?-' that could not be
           * sequences of SQL operators.
           */
          while (nchars > 1 &&
               (yytext[nchars-1] == '+' ||
              yytext[nchars-1] == '-'))
          {
            int   ic;

            for (ic = nchars-2; ic >= 0; ic--)
            {
              if (strchr("~!@#^&|`?%", yytext[ic]))
                break;
            }
            if (ic >= 0)
              break; /* found a char that makes it OK */
            nchars--; /* else remove the +/-, and check again */
          }

          SET_YYLLOC();

          if (nchars < yyleng)
          {
            /* Strip the unwanted chars from the token */
            yyless(nchars);
            /*
             * If what we have left is only one char, and it's
             * one of the characters matching "self", then
             * return it as a character token the same way
             * that the "self" rule would have.
             */
            if (nchars == 1 &&
              strchr(",()[].;:+-*/%^<>=", yytext[0]))
              return yytext[0];
          }

          /*
           * Complain if operator is too long.  Unlike the case
           * for identifiers, we make this an error not a notice-
           * and-truncate, because the odds are we are looking at
           * a syntactic mistake anyway.
           */
          if (nchars >= NAMEDATALEN)
            yyerror("operator too long");

          yylval->str = pstrdup(yytext);
          return Op;
        }

{param}     {
          SET_YYLLOC();
          yylval->ival = atol(yytext + 1);
          return PARAM;
        }

{integer}   {
          SET_YYLLOC();
          return process_integer_literal(yytext, yylval);
        }
{decimal}   {
          SET_YYLLOC();
          yylval->str = pstrdup(yytext);
          return FCONST;
        }
{decimalfail} {
          /* throw back the .., and treat as integer */
          yyless(yyleng-2);
          SET_YYLLOC();
          return process_integer_literal(yytext, yylval);
        }
{real}      {
          SET_YYLLOC();
          yylval->str = pstrdup(yytext);
          return FCONST;
        }
{realfail1}   {
          /*
           * throw back the [Ee], and treat as {decimal}.  Note
           * that it is possible the input is actually {integer},
           * but since this case will almost certainly lead to a
           * syntax error anyway, we don't bother to distinguish.
           */
          yyless(yyleng-1);
          SET_YYLLOC();
          yylval->str = pstrdup(yytext);
          return FCONST;
        }
{realfail2}   {
          /* throw back the [Ee][+-], and proceed as above */
          yyless(yyleng-2);
          SET_YYLLOC();
          yylval->str = pstrdup(yytext);
          return FCONST;
        }


{identifier}  {
          const ScanKeyword *keyword;
          char       *ident;

          SET_YYLLOC();

          /* Is it a keyword? */
          keyword = ScanKeywordLookup(yytext,
                        yyextra->keywords,
                        yyextra->num_keywords);
          if (keyword != NULL)
          {
            yylval->keyword = keyword->name;
            return keyword->value;
          }

          /*
           * No.  Convert the identifier to lower case, and truncate
           * if necessary.
           */
          ident = downcase_truncate_identifier(yytext, yyleng, true);
          yylval->str = ident;
          return IDENT;
        }

{other}     {
          SET_YYLLOC();
          return yytext[0];
        }

<<EOF>>     {
          SET_YYLLOC();
          yyterminate();
        }
%%

/*
 * Arrange access to yyextra for subroutines of the main yylex() function.
 * We expect each subroutine to have a yyscanner parameter.  Rather than
 * use the yyget_xxx functions, which might or might not get inlined by the
 * compiler, we cheat just a bit and cast yyscanner to the right type.
 */
#undef yyextra
#define yyextra  (((struct yyguts_t *) yyscanner)->yyextra_r)

/* Likewise for a couple of other things we need. */
#undef yylloc
#define yylloc  (((struct yyguts_t *) yyscanner)->yylloc_r)
#undef yyleng
#define yyleng  (((struct yyguts_t *) yyscanner)->yyleng_r)

/*
 * scanner_errposition
 *    Report a lexer or grammar error cursor position, if possible.
 *
 * This is expected to be used within an ereport() call.  The return value
 * is a dummy (always 0, in fact).
 *
 * Note that this can only be used for messages emitted during raw parsing
 * (essentially, scan.l and gram.y), since it requires the yyscanner struct
 * to still be available.
 */
int
scanner_errposition(int location, YbSql_yyscan_t yyscanner)
{
  int   pos;

  if (location < 0)
    return 0;       /* no-op if location is unknown */

  /* Convert byte offset to character number */
  pos = pg_mbstrlen_with_len(yyextra->scanbuf, location) + 1;

  /* And pass it to the ereport mechanism */
  // errposition(pos);

  return pos;
}

/*
 * scanner_yyerror
 *    Report a lexer or grammar error.
 *
 * The message's cursor position is whatever YYLLOC was last set to,
 * ie, the start of the current token if called within yylex(), or the
 * most recently lexed token if called from the grammar.
 * This is OK for syntax error messages from the Bison parser, because Bison
 * parsers report error as soon as the first unparsable token is reached.
 * Beware of using yyerror for other purposes, as the cursor position might
 * be misleading!
 */
void
scanner_yyerror(const char *message, YbSql_yyscan_t yyscanner)
{
  const char *loc = yyextra->scanbuf + *yylloc;

  if (*loc == YY_END_OF_BUFFER_CHAR)
  {
    LOG(ERROR)
      << "SQL ERROR " << ERRCODE_SYNTAX_ERROR
      << message << "at end of input";
  } else {
    LOG(ERROR)
      << "SQL ERROR " << ERRCODE_SYNTAX_ERROR
      << " at or near " << message;
  }
}

/*
 * Called before any actual parsing is done
 */
YbSql_yyscan_t scanner_init(const char *str,
                            YbSql_yy_extra_type *yyext,
                            const ScanKeyword *keywords,
                            int num_keywords) {
  size_t         slen = strlen(str);
  YbSql_yyscan_t scanner;

  if (yylex_init(&scanner) != 0) {
    LOG(FATAL) << "yylex_init() failed: %m";
  }

  YbSqlScan_yyset_extra(yyext, scanner);

  yyext->keywords = keywords;
  yyext->num_keywords = num_keywords;

  yyext->backslash_quote = backslash_quote;
  yyext->escape_string_warning = escape_string_warning;
  yyext->standard_conforming_strings = standard_conforming_strings;

  /*
   * Make a scan buffer with special termination needed by flex.
   */
  yyext->scanbuf = (char *) malloc(slen + 2);
  yyext->scanbuflen = slen;
  memcpy(yyext->scanbuf, str, slen);
  yyext->scanbuf[slen] = yyext->scanbuf[slen + 1] = YY_END_OF_BUFFER_CHAR;
  yy_scan_buffer(yyext->scanbuf, slen + 2, scanner);

  /* initialize literal buffer to a reasonable but expansible size */
  yyext->literalalloc = 1024;
  yyext->literalbuf = (char *) malloc(yyext->literalalloc);
  yyext->literallen = 0;

  return scanner;
}


/*
 * Called after parsing is done to clean up after scanner_init()
 */
void
scanner_finish(YbSql_yyscan_t yyscanner)
{
  /*
   * We don't bother to call yylex_destroy(), because all it would do
   * is pfree a small amount of control storage.  It's cheaper to leak
   * the storage until the parsing context is destroyed.  The amount of
   * space involved is usually negligible compared to the output parse
   * tree anyway.
   *
   * We do bother to pfree the scanbuf and literal buffer, but only if they
   * represent a nontrivial amount of space.  The 8K cutoff is arbitrary.
   */
  if (yyextra->scanbuflen >= 8192)
    free(yyextra->scanbuf);
  if (yyextra->literalalloc >= 8192)
    free(yyextra->literalbuf);
}

using namespace std;

void
addlit(char *ytext, int yleng, YbSql_yyscan_t yyscanner)
{
  /* enlarge buffer if needed */
  if ((yyextra->literallen + yleng) >= yyextra->literalalloc)
  {
    do {
      yyextra->literalalloc *= 2;
    } while ((yyextra->literallen + yleng) >= yyextra->literalalloc);
    yyextra->literalbuf = (char *)realloc(yyextra->literalbuf, yyextra->literalalloc);
  }
  /* append new data */
  memcpy(yyextra->literalbuf + yyextra->literallen, ytext, yleng);
  yyextra->literallen += yleng;
}


void
addlitchar(unsigned char ychar, YbSql_yyscan_t yyscanner)
{
  /* enlarge buffer if needed */
  if ((yyextra->literallen + 1) >= yyextra->literalalloc)
  {
    yyextra->literalalloc *= 2;
    yyextra->literalbuf = (char *)realloc(yyextra->literalbuf, yyextra->literalalloc);
  }
  /* append new data */
  yyextra->literalbuf[yyextra->literallen] = ychar;
  yyextra->literallen += 1;
}


/*
 * Create a palloc'd copy of literalbuf, adding a trailing null.
 */
char *
litbufdup(YbSql_yyscan_t yyscanner)
{
  int   llen = yyextra->literallen;
  char *lbuf;

  lbuf = (char*)malloc(llen + 1);
  memcpy(lbuf, yyextra->literalbuf, llen);
  lbuf[llen] = '\0';
  return lbuf;
}

int
process_integer_literal(const char *token, YbSqlScan_YYSTYPE *lval)
{
  int64_t  val;
  char    *endptr;

  errno = 0;
  val = strtol(token, &endptr, 10);
  if (*endptr != '\0' || errno == ERANGE || val != (long)((int32) val)) {
    /* if long > 32 bits, check for overflow of int4 */
    /* integer too large, treat it as a float */
    lval->str = pstrdup(token);
    return FCONST;
  }
  lval->ival = val;
  return ICONST;
}

unsigned int
hexval(unsigned char c)
{
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 0xA;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 0xA;
  LOG(FATAL) << "invalid hexadecimal digit";
  return 0; /* not reached */
}

void
check_unicode_value(pg_wchar c, char *loc, YbSql_yyscan_t yyscanner) {
}

bool
is_utf16_surrogate_first(pg_wchar c)
{
  return (c >= 0xD800 && c <= 0xDBFF);
}

bool
is_utf16_surrogate_second(pg_wchar c)
{
  return (c >= 0xDC00 && c <= 0xDFFF);
}

pg_wchar
surrogate_pair_to_codepoint(pg_wchar first, pg_wchar second)
{
  return ((first & 0x3FF) << 10) + 0x10000 + (second & 0x3FF);
}

void
addunicode(pg_wchar c, YbSql_yyscan_t yyscanner)
{
  char buf[8];

  if (c == 0 || c > 0x10FFFF)
    yyerror("invalid Unicode escape value");
  if (c > 0x7F) {
    yyextra->saw_non_ascii = true;
  }
  unicode_to_utf8(c, (unsigned char *) buf);
  addlit(buf, pg_utf_mblen((unsigned char *)buf), yyscanner);
}

/*
 * downcase_truncate_identifier() --- do appropriate downcasing and
 * truncation of an unquoted identifier.  Optionally warn of truncation.
 *
 * Returns a palloc'd string containing the adjusted identifier.
 *
 * Note: in some usages the passed string is not null-terminated.
 *
 * Note: the API of this function is designed to allow for downcasing
 * transformations that increase the string length, but we don't yet
 * support that.  If you want to implement it, you'll need to fix
 * SplitIdentifierString() in utils/adt/varlena.c.
 */
char *
downcase_truncate_identifier(const char *ident, int len, bool warn)
{
  char     *result;
  int     i;

  result = (char *)malloc(len + 1);

  /*
   * SQL99 specifies Unicode-aware case normalization, which we don't yet
   * have the infrastructure for.  Instead we use tolower() to provide a
   * locale-aware translation.  However, there are some locales where this
   * is not right either (eg, Turkish may do strange things with 'i' and
   * 'I').  Our current compromise is to use tolower() for characters with
   * the high bit set, as long as they aren't part of a multi-byte
   * character, and use an ASCII-only downcasing for 7-bit characters.
   */
  for (i = 0; i < len; i++)
  {
    unsigned char ch = (unsigned char) ident[i];

    if (ch >= 'A' && ch <= 'Z')
      ch += 'a' - 'A';
    result[i] = (char) ch;
  }
  result[i] = '\0';

  if (i >= NAMEDATALEN)
    truncate_identifier(result, i, warn);

  return result;
}

/*
 * truncate_identifier() --- truncate an identifier to NAMEDATALEN-1 bytes.
 *
 * The given string is modified in-place, if necessary.  A warning is
 * issued if requested.
 *
 * We require the caller to pass in the string length since this saves a
 * strlen() call in some common usages.
 */
void
truncate_identifier(char *ident, int len, bool warn)
{
  if (len >= NAMEDATALEN)
  {
    len = pg_encoding_mbcliplen(ident, len, NAMEDATALEN - 1);
    if (warn)
    {
      /*
       * We avoid using %.*s here because it can misbehave if the data
       * is not valid in what libc thinks is the prevailing encoding.
       */
      char    buf[NAMEDATALEN];

      memcpy(buf, ident, len);
      buf[len] = '\0';
      LOG(WARNING)
        << "SQL WARNING " << ERRCODE_NAME_TOO_LONG
        << ": Identifier " << ident << " will be truncated to " << buf;
    }
    ident[len] = '\0';
  }
}

/* is 'escape' acceptable as Unicode escape character (UESCAPE syntax) ? */
/*
 * scanner_isspace() --- return TRUE if flex scanner considers char whitespace
 *
 * This should be used instead of the potentially locale-dependent isspace()
 * function when it's important to match the lexer's behavior.
 *
 * In principle we might need similar functions for isalnum etc, but for the
 * moment only isspace seems needed.
 */
bool
scanner_isspace(char ch)
{
  /* This must match scan.l's list of {space} characters */
  if (ch == ' ' ||
    ch == '\t' ||
    ch == '\n' ||
    ch == '\r' ||
    ch == '\f')
    return true;
  return false;
}

bool
check_uescapechar(unsigned char escape)
{
  if (isxdigit(escape)
    || escape == '+'
    || escape == '\''
    || escape == '"'
    || scanner_isspace(escape))
  {
    return false;
  }
  else
    return true;
}

/* like litbufdup, but handle unicode escapes */
char *
litbuf_udeescape(unsigned char escape, YbSql_yyscan_t yyscanner)
{
  char *new_litbuf;
  char *litbuf, *in, *out;
  pg_wchar pair_first = 0;

  /* Make literalbuf null-terminated to simplify the scanning loop */
  litbuf = yyextra->literalbuf;
  litbuf[yyextra->literallen] = '\0';

  /*
   * This relies on the subtle assumption that a UTF-8 expansion
   * cannot be longer than its escaped representation.
   */
  new_litbuf = (char*)malloc(yyextra->literallen + 1);

  in = litbuf;
  out = new_litbuf;
  while (*in)
  {
    if (in[0] == escape)
    {
      if (in[1] == escape)
      {
        if (pair_first)
        {
          ADVANCE_YYLLOC(in - litbuf + 3);   /* 3 for U&" */
          yyerror("invalid Unicode surrogate pair");
        }
        *out++ = escape;
        in += 2;
      }
      else if (isxdigit((unsigned char) in[1]) &&
           isxdigit((unsigned char) in[2]) &&
           isxdigit((unsigned char) in[3]) &&
           isxdigit((unsigned char) in[4]))
      {
        pg_wchar unicode;

        unicode = (hexval(in[1]) << 12) +
          (hexval(in[2]) << 8) +
          (hexval(in[3]) << 4) +
          hexval(in[4]);
        check_unicode_value(unicode, in, yyscanner);
        if (pair_first)
        {
          if (is_utf16_surrogate_second(unicode))
          {
            unicode = surrogate_pair_to_codepoint(pair_first, unicode);
            pair_first = 0;
          }
          else
          {
            ADVANCE_YYLLOC(in - litbuf + 3);   /* 3 for U&" */
            yyerror("invalid Unicode surrogate pair");
          }
        }
        else if (is_utf16_surrogate_second(unicode))
          yyerror("invalid Unicode surrogate pair");

        if (is_utf16_surrogate_first(unicode))
          pair_first = unicode;
        else
        {
          unicode_to_utf8(unicode, (unsigned char *) out);
          out += pg_utf_mblen((unsigned char *)out);
        }
        in += 5;
      }
      else if (in[1] == '+' &&
           isxdigit((unsigned char) in[2]) &&
           isxdigit((unsigned char) in[3]) &&
           isxdigit((unsigned char) in[4]) &&
           isxdigit((unsigned char) in[5]) &&
           isxdigit((unsigned char) in[6]) &&
           isxdigit((unsigned char) in[7]))
      {
        pg_wchar unicode;

        unicode = (hexval(in[2]) << 20) +
          (hexval(in[3]) << 16) +
          (hexval(in[4]) << 12) +
          (hexval(in[5]) << 8) +
          (hexval(in[6]) << 4) +
          hexval(in[7]);
        check_unicode_value(unicode, in, yyscanner);
        if (pair_first)
        {
          if (is_utf16_surrogate_second(unicode))
          {
            unicode = surrogate_pair_to_codepoint(pair_first, unicode);
            pair_first = 0;
          }
          else
          {
            ADVANCE_YYLLOC(in - litbuf + 3);   /* 3 for U&" */
            yyerror("invalid Unicode surrogate pair");
          }
        }
        else if (is_utf16_surrogate_second(unicode))
          yyerror("invalid Unicode surrogate pair");

        if (is_utf16_surrogate_first(unicode))
          pair_first = unicode;
        else
        {
          unicode_to_utf8(unicode, (unsigned char *) out);
          out += pg_utf_mblen((unsigned char *)out);
        }
        in += 8;
      }
      else
      {
        ADVANCE_YYLLOC(in - litbuf + 3);   /* 3 for U&" */
        yyerror("invalid Unicode escape value");
      }
    }
    else
    {
      if (pair_first)
      {
        ADVANCE_YYLLOC(in - litbuf + 3);   /* 3 for U&" */
        yyerror("invalid Unicode surrogate pair");
      }
      *out++ = *in++;
    }
  }

  *out = '\0';
  /*
   * We could skip pg_verifymbstr if we didn't process any non-7-bit-ASCII
   * codes; but it's probably not worth the trouble, since this isn't
   * likely to be a performance-critical path.
   */
  pg_verify_mbstr_len(new_litbuf, out - new_litbuf, false);
  return new_litbuf;
}

unsigned char
unescape_single_char(unsigned char c, YbSql_yyscan_t yyscanner)
{
  switch (c)
  {
    case 'b':
      return '\b';
    case 'f':
      return '\f';
    case 'n':
      return '\n';
    case 'r':
      return '\r';
    case 't':
      return '\t';
    default:
      /* check for backslash followed by non-7-bit-ASCII */
      if (c == '\0' || IS_HIGHBIT_SET(c))
        yyextra->saw_non_ascii = true;

      return c;
  }
}

void
check_string_escape_warning(unsigned char ychar, YbSql_yyscan_t yyscanner)
{
  if (ychar == '\'')
  {
    if (yyextra->warn_on_first_escape && yyextra->escape_string_warning)
      LOG(WARNING)
        << "SQL ERROR " << ERRCODE_NONSTANDARD_USE_OF_ESCAPE_CHARACTER
        << "(Nonstandard use of \\' in a string literal): "
        << "Use '' to write quotes in strings, or use the escape string syntax (E'...').";
    yyextra->warn_on_first_escape = false;  /* warn only once per string */
  } else if (ychar == '\\') {
    if (yyextra->warn_on_first_escape && yyextra->escape_string_warning)
      LOG(WARNING)
        << "SQL ERROR " << ERRCODE_NONSTANDARD_USE_OF_ESCAPE_CHARACTER
        << "(Nonstandard use of \\\\ in a string literal): "
        << "Use the escape string syntax for backslashes, e.g., E'\\\\'.";
    yyextra->warn_on_first_escape = false;  /* warn only once per string */
  }
  else
    check_escape_warning(yyscanner);
}

void
check_escape_warning(YbSql_yyscan_t yyscanner)
{
  if (yyextra->warn_on_first_escape && yyextra->escape_string_warning)
    LOG(WARNING)
      << "SQL ERROR " << ERRCODE_NONSTANDARD_USE_OF_ESCAPE_CHARACTER
      << "(Nonstandard use of escape in a string literal): "
      << "Use the escape string syntax for escapes, e.g., E'\\r\\n'.";
  yyextra->warn_on_first_escape = false;  /* warn only once per string */
}

// We will define a memory allocator for parsing, so PostgreSql's version is undef'ed.
#if 0
/*
 * Interface functions to make flex use palloc() instead of malloc().
 * It'd be better to make these static, but flex insists otherwise.
 */
void *
YbSql_yyalloc(yy_size_t bytes, YbSql_yyscan_t yyscanner)
{
  return malloc(bytes);
}

void *
YbSql_yyrealloc(void *ptr, yy_size_t bytes, YbSql_yyscan_t yyscanner)
{
  if (ptr)
    return realloc(ptr, bytes);
  else
    return alloc(bytes);
}

void
YbSql_yyfree(void *ptr, YbSql_yyscan_t yyscanner)
{
  if (ptr)
    free(ptr);
}
#endif
