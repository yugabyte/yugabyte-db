%{
/* 
** A scanner for EMP-style numeric ranges 
*/

#include "postgres.h"

#include "parser/gramparse.h"
/* Not needed now that this file is compiled as part of gram.y */
/* #include "parser/parse.h" */
#include "parser/scansup.h"
#include "mb/pg_wchar.h"

#include "parse_keyword.h"

/* Avoid exit() on fatal scanner errors (a bit ugly -- see yy_fatal_error) */
#undef fprintf
#define fprintf(file, fmt, msg)  fprintf_to_ereport(fmt, msg)

static void
fprintf_to_ereport(const char *fmt, const char *msg)
{
	ereport(ERROR, (errmsg_internal("%s", msg)));
}

static int		xcdepth = 0;	/* depth of nesting in slash-star comments */
static char    *dolqstart;      /* current $foo$ quote start string */
static bool extended_string = false;


/* No reason to constrain amount of data slurped */
#define YY_READ_BUF_SIZE 16777216

/* Handles to the buffer that the lexer uses internally */


static YY_BUFFER_STATE scanbufhandle;

#define SET_YYLLOC()  (orafce_sql_yylval.val.lloc = yytext - scanbuf)                                                                                              
                                                                                                                                               
/* Handles to the buffer that the lexer uses internally */                                                                                     
static char *scanbuf;       

/* flex 2.5.4 doesn't bother with a decl for this */

int orafce_sql_yylex(void);

void orafce_sql_scanner_init(const char *str);
void orafce_sql_scanner_finish(void);

/*
 * literalbuf is used to accumulate literal values when multiple rules
 * are needed to parse a single literal.  Call startlit to reset buffer
 * to empty, addlit to add text.  Note that the buffer is palloc'd and
 * starts life afresh on every parse cycle.
 */
static char	   *literalbuf;		/* expandable buffer */
static int		literallen;		/* actual current length */
static int		literalalloc;	/* current allocated buffer size */

#define startlit()  (literalbuf[0] = '\0', literallen = 0)
static void addlit(char *ytext, int yleng);
static void addlitchar(unsigned char ychar);
static char *litbufdup(void);

static int	lexer_errposition(void);

/*
 * Each call to yylex must set yylloc to the location of the found token
 * (expressed as a byte offset from the start of the input text).
 * When we parse a token that requires multiple lexer rules to process,
 * this should be done in the first such rule, else yylloc will point
 * into the middle of the token.
 */

/* Handles to the buffer that the lexer uses internally */
static char *scanbuf;

static unsigned char unescape_single_char(unsigned char c);

#ifndef _pg_mbstrlen_with_len
#define _pg_mbstrlen_with_len(buf,loc) 	pg_mbstrlen_with_len(buf,loc)
#endif

%}

%option 8bit
%option never-interactive
%option nodefault
%option noinput
%option nounput
%option noyywrap
%option prefix="orafce_sql_yy"

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
 */

%x xb
%x xc
%x xd
%x xh
%x xe
%x xq
%x xdolq


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

space			[ \t\n\r\f]
horiz_space		[ \t\f]
newline			[\n\r]
non_newline		[^\n\r]

comment			("--"{non_newline}*)

whitespace		{space}+

/*
 * SQL requires at least one newline in the whitespace separating
 * string literals that are to be concatenated.  Silly, but who are we
 * to argue?  Note that {whitespace_with_newline} should not have * after
 * it, whereas {whitespace} should generally have a * after it...
 */

special_whitespace		({space}+|{comment}{newline})
horiz_whitespace		({horiz_space}|{comment})
whitespace_with_newline	({horiz_whitespace}*{newline}{special_whitespace}*)

/*
 * To ensure that {quotecontinue} can be scanned without having to back up
 * if the full pattern isn't matched, we include trailing whitespace in
 * {quotestop}.  This matches all cases where {quotecontinue} fails to match,
 * except for {quote} followed by whitespace and just one "-" (not two,
 * which would start a {comment}).  To cover that we have {quotefail}.
 * The actions for {quotestop} and {quotefail} must throw back characters
 * beyond the quote proper.
 */
quote			'
quotestop		{quote}{whitespace}*
quotecontinue	{quote}{whitespace_with_newline}{quote}
quotefail		{quote}{whitespace}*"-"

/* Bit string
 * It is tempting to scan the string for only those characters
 * which are allowed. However, this leads to silently swallowed
 * characters if illegal characters are included in the string.
 * For example, if xbinside is [01] then B'ABCD' is interpreted
 * as a zero-length string, and the ABCD' is lost!
 * Better to pass the string forward and let the input routines
 * validate the contents.
 */
xbstart			[bB]{quote}
xbinside		[^']*

/* Hexadecimal number */
xhstart			[xX]{quote}
xhinside		[^']*

/* National character */
xnstart			[nN]{quote}

/* Quoted string that allows backslash escapes */
xestart			[eE]{quote}
xeinside		[^\\']+
xeescape		[\\][^0-7]
xeoctesc		[\\][0-7]{1,3}
xehexesc		[\\]x[0-9A-Fa-f]{1,2}

/* Extended quote
 * xqdouble implements embedded quote, ''''
 */
xqstart			{quote}
xqdouble		{quote}{quote}
xqinside		[^']+

/* $foo$ style quotes ("dollar quoting")
 * The quoted string starts with $foo$ where "foo" is an optional string
 * in the form of an identifier, except that it may not contain "$", 
 * and extends to the first occurrence of an identical string.  
 * There is *no* processing of the quoted text.
 *
 * {dolqfailed} is an error rule to avoid scanner backup when {dolqdelim}
 * fails to match its trailing "$".
 */
dolq_start		[A-Za-z\200-\377_]
dolq_cont		[A-Za-z\200-\377_0-9]
dolqdelim		\$({dolq_start}{dolq_cont}*)?\$
dolqfailed		\${dolq_start}{dolq_cont}*
dolqinside		[^$]+

/* Double quote
 * Allows embedded spaces and other special characters into identifiers.
 */
dquote			\"
xdstart			{dquote}
xdstop			{dquote}
xddouble		{dquote}{dquote}
xdinside		[^"]+

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
 */
xcstart			\/\*{op_chars}*
xcstop			\*+\/
xcinside		[^*/]+

digit			[0-9]
ident_start		[A-Za-z\200-\377_]
ident_cont		[A-Za-z\200-\377_0-9\$]

identifier		{ident_start}{ident_cont}*

typecast		"::"

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
self			[,()\[\].;\:\+\-\*\/\%\^\<\>\=]
op_chars		[\~\!\@\#\^\&\|\`\?\+\-\*\/\%\<\>\=]
operator		{op_chars}+

/* we no longer allow unary minus in numbers. 
 * instead we pass it separately to parser. there it gets
 * coerced via doNegate() -- Leon aug 20 1999
 *
 * {realfail1} and {realfail2} are added to prevent the need for scanner
 * backup when the {real} rule fails to match completely.
 */

integer			{digit}+
decimal			(({digit}*\.{digit}+)|({digit}+\.{digit}*))
real			({integer}|{decimal})[Ee][-+]?{digit}+
realfail1		({integer}|{decimal})[Ee]
realfail2		({integer}|{decimal})[Ee][-+]

param			\${integer}

other			.

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

{whitespace}	{
					SET_YYLLOC();
					yylval.val.str = yytext;
					yylval.val.modificator = NULL;
					yylval.val.keycode = -1;
					yylval.val.sep = NULL;
					return X_WHITESPACE;
				}

{comment}	{
					SET_YYLLOC();
					yylval.val.str = yytext;
					yylval.val.modificator = "sc";
					yylval.val.keycode = -1;
					yylval.val.sep = NULL;
					return X_COMMENT;
				}


{xcstart}		{
					/* Set location in case of syntax error in comment */
					SET_YYLLOC();
					xcdepth = 0;
					BEGIN(xc);
					/* Put back any characters past slash-star; see above */
					startlit();
					addlitchar('/');
					addlitchar('*');
				
					yyless(2);
				}

<xc>{xcstart}	{
					xcdepth++;
					/* Put back any characters past slash-star; see above */
					addlitchar('/');
					addlitchar('*');

					yyless(2);
				}

<xc>{xcstop}	{
					if (xcdepth <= 0)
					{
						BEGIN(INITIAL);
						addlitchar('*');
						addlitchar('/');

						yylval.val.str = litbufdup();
						yylval.val.modificator = "ec";
						yylval.val.keycode = -1;
						yylval.val.sep = NULL;
						return X_COMMENT;
					}
					else
					{
						xcdepth--;
						addlitchar('*');
						addlitchar('/');
					}

				}

<xc>{xcinside}	{
					addlit(yytext, yyleng);
				}

<xc>{op_chars}	{
					addlit(yytext, yyleng);
				}

<xc>\*+			{
					addlit(yytext, yyleng);
				}

<xc><<EOF>>		{ 
					yylval.val.str = litbufdup();
					yylval.val.modificator = "ecu";
					yylval.val.keycode = -1;
					yylval.val.sep = NULL;
					return X_COMMENT;

				}

{xbstart}		{
					/* Binary bit type.
					 * At some point we should simply pass the string
					 * forward to the parser and label it there.
					 * In the meantime, place a leading "b" on the string
					 * to mark it for the input routine as a binary string.
					 */
					SET_YYLLOC();
					BEGIN(xb);
					startlit();
					addlitchar('b');
				}
<xb>{quotestop}	|
<xb>{quotefail} {
					yyless(1);
					BEGIN(INITIAL);
					yylval.val.str = litbufdup();
					yylval.val.modificator = "b";
					yylval.val.keycode = -1;
					yylval.val.sep = NULL;
					return X_NCONST;
				}
<xh>{xhinside}	|
<xb>{xbinside}	{
					addlit(yytext, yyleng);
				}
<xh>{quotecontinue}	|
<xb>{quotecontinue}	{
					/* ignore */
				}
<xb><<EOF>>		{ 
					yylval.val.str = litbufdup();
					yylval.val.modificator = "bu";
					yylval.val.keycode = -1;
					yylval.val.sep = NULL;
					return X_NCONST;
				}

{xhstart}		{
					/* Hexadecimal bit type.
					 * At some point we should simply pass the string
					 * forward to the parser and label it there.
					 * In the meantime, place a leading "x" on the string
					 * to mark it for the input routine as a hex string.
					 */
					SET_YYLLOC();
					BEGIN(xh);
					startlit();
					addlitchar('x');
				}
<xh>{quotestop}	|
<xh>{quotefail} {
					yyless(1);
					BEGIN(INITIAL);
					yylval.val.str = litbufdup();
					yylval.val.modificator = "x";
					yylval.val.keycode = -1;
					yylval.val.sep = NULL;
					return X_NCONST;
				}
<xh><<EOF>>		{ 
					yylval.val.str = litbufdup();
					yylval.val.modificator = "xu";
					yylval.val.keycode = -1;
					yylval.val.sep = NULL;
					return X_NCONST;
				}

{xnstart}		{
					/* National character.
					 * We will pass this along as a normal character string,
					 * but preceded with an internally-generated "NCHAR".
					 */
					const char *keyword;
					int		keycode;

					SET_YYLLOC();
					yyless(1);				/* eat only 'n' this time */
					/* nchar had better be a keyword! */
					keyword = orafce_scan_keyword("nchar", &keycode);
					Assert(keyword != NULL);
					yylval.val.str = (char*) keyword;
					yylval.val.keycode = keycode;
					yylval.val.modificator = NULL;
					yylval.val.sep = NULL;
					return X_KEYWORD;
				}

{xqstart}		{
					SET_YYLLOC();
					BEGIN(xq);
					extended_string = false;
					startlit();
				}
{xestart}		{
					SET_YYLLOC();
					BEGIN(xe);
					extended_string = true;
					startlit();
				}
<xq,xe>{quotestop}	|
<xq,xe>{quotefail} {
					yyless(1);
					BEGIN(INITIAL);
					yylval.val.str = litbufdup();
					yylval.val.modificator = extended_string ? "es" : "qs";
					yylval.val.keycode = -1;
					yylval.val.sep = NULL;
					return X_SCONST;
				}
<xq,xe>{xqdouble} {
					addlitchar('\'');
				}
<xq>{xqinside}  {
					addlit(yytext, yyleng);
				}
<xe>{xeinside}  {
					addlit(yytext, yyleng);
				}
<xe>{xeescape}  {
					addlitchar(unescape_single_char(yytext[1]));
				}
<xe>{xeoctesc}  {
					unsigned char c = strtoul(yytext+1, NULL, 8);

					addlitchar(c);
				}
<xe>{xehexesc}  {
					unsigned char c = strtoul(yytext+2, NULL, 16);

					addlitchar(c);
				}
<xq,xe>{quotecontinue} {
					/* ignore */
				}
<xe>.			{
					/* This is only needed for \ just before EOF */
					addlitchar(yytext[0]);
				}
<xq,xe><<EOF>>		{ 
					yylval.val.str = litbufdup();
					yylval.val.modificator = extended_string ? "esu" : "qsu";
					yylval.val.keycode = -1;
					yylval.val.sep = NULL;
					return X_SCONST;
				}    

{dolqdelim}		{
					SET_YYLLOC();
					dolqstart = pstrdup(yytext);
					BEGIN(xdolq);
					startlit();
				}
{dolqfailed}	{
					/* throw back all but the initial "$" */
					yyless(1);
					/* and treat it as {other} */
					yylval.val.str = yytext;
					yylval.val.modificator = "dolqf";
					yylval.val.keycode = -1;
					yylval.val.sep = NULL;
					return X_OTHERS;
				}
<xdolq>{dolqdelim} {
					if (strcmp(yytext, dolqstart) == 0)
					{
						yylval.val.sep = dolqstart;
						yylval.val.modificator = "dolq";
						BEGIN(INITIAL);
						yylval.val.str = litbufdup();
						yylval.val.keycode = -1;
						return X_SCONST;
					}
					else
					{
						/*
						 * When we fail to match $...$ to dolqstart, transfer
						 * the $... part to the output, but put back the final
						 * $ for rescanning.  Consider $delim$...$junk$delim$
						 */
						addlit(yytext, yyleng-1);
						yyless(yyleng-1);
					}
				}
<xdolq>{dolqinside} {
					addlit(yytext, yyleng);
				}
<xdolq>{dolqfailed} {
					addlit(yytext, yyleng);
				}
<xdolq>.		{
					/* This is only needed for inside the quoted text */
					addlitchar(yytext[0]);
				}
<xdolq><<EOF>>	{ 
					yylval.val.sep = dolqstart;
					yylval.val.modificator = "dolqu";
					yylval.val.str = litbufdup();
					yylval.val.keycode = -1;
					yylval.val.sep = NULL;
					return X_SCONST;
				}

{xdstart}		{
					SET_YYLLOC();
					BEGIN(xd);
					startlit();
				}
<xd>{xdstop}	{
					char		   *ident;

					BEGIN(INITIAL);
					if (literallen == 0)
						yyerror(NULL, "zero-length delimited identifier");
					ident = litbufdup();
					if (literallen >= NAMEDATALEN)
						truncate_identifier(ident, literallen, true);
					yylval.val.modificator = "dq";
					yylval.val.str = ident;
					yylval.val.keycode = -1;
					yylval.val.sep = NULL;
					return X_IDENT;
				}
<xd>{xddouble}	{
					addlitchar('"');
				}
<xd>{xdinside}	{
					addlit(yytext, yyleng);
				}
<xd><<EOF>>		{ 
					yylval.val.modificator = "dqu";
					yylval.val.str = litbufdup();
					yylval.val.keycode = -1;
					yylval.val.sep = NULL;
					return X_IDENT;
				}
{typecast}		{
					SET_YYLLOC();
					yylval.val.modificator = "typecast";
					yylval.val.keycode = X_TYPECAST;
					yylval.val.sep = NULL;
					return X_OTHERS;
				}

{self}			{
					SET_YYLLOC();
					yylval.val.str = yytext;
					yylval.val.modificator = "self";
					yylval.val.keycode = yytext[0];
					yylval.val.sep = NULL;
					return X_OTHERS;
				}

{operator}		{
					/*
					 * Check for embedded slash-star or dash-dash; those
					 * are comment starts, so operator must stop there.
					 * Note that slash-star or dash-dash at the first
					 * character will match a prior rule, not this one.
					 */
					int		nchars = yyleng;
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
						int		ic;

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
						{
							yylval.val.str = yytext;
							yylval.val.modificator = NULL;
							yylval.val.keycode = yytext[0];
							yylval.val.sep = NULL;
							return X_OTHERS;
						}
					}

					/*
					 * Complain if operator is too long.  Unlike the case
					 * for identifiers, we make this an error not a notice-
					 * and-truncate, because the odds are we are looking at
					 * a syntactic mistake anyway.
					 */
					if (nchars >= NAMEDATALEN)
						yyerror(NULL, "operator too long");

					/* Convert "!=" operator to "<>" for compatibility */
					yylval.val.modificator = NULL;
					if (strcmp(yytext, "!=") == 0)
						yylval.val.str = pstrdup("<>");
					else
						yylval.val.str = pstrdup(yytext);
					yylval.val.keycode = -1;
					yylval.val.sep = NULL;
					return X_OP;
				}

{param}			{
					SET_YYLLOC();
					yylval.val.modificator = NULL;
					yylval.val.str = yytext;
					yylval.val.keycode = -1;
					yylval.val.sep = NULL;
					return X_PARAM;
				}

{integer}		{
					long val;
					char* endptr;

					SET_YYLLOC();
					errno = 0;
					val = strtol(yytext, &endptr, 10);
					if (*endptr != '\0' || errno == ERANGE
#ifdef HAVE_LONG_INT_64
						/* if long > 32 bits, check for overflow of int4 */
						|| val != (long) ((int32) val)
#endif
						)
					{
						/* integer too large, treat it as a float */
						yylval.val.str = pstrdup(yytext);
						yylval.val.modificator = "f";
						yylval.val.keycode = -1;
						yylval.val.sep = NULL;
						return X_NCONST;
					}
					yylval.val.str = yytext;
					yylval.val.modificator = "i";
					yylval.val.keycode = -1;
					yylval.val.sep = NULL;
					return X_NCONST;
				}
{decimal}		{
					SET_YYLLOC();
					yylval.val.str = pstrdup(yytext);
					yylval.val.modificator = "f";
					yylval.val.keycode = -1;
					yylval.val.sep = NULL;
					return X_NCONST;
				}
{real}			{
					SET_YYLLOC();
					yylval.val.str = pstrdup(yytext);
					yylval.val.modificator = "f";
					yylval.val.keycode = -1;
					yylval.val.sep = NULL;
					return X_NCONST;
				}
{realfail1}		{
					/*
					 * throw back the [Ee], and treat as {decimal}.  Note
					 * that it is possible the input is actually {integer},
					 * but since this case will almost certainly lead to a
					 * syntax error anyway, we don't bother to distinguish.
					 */
					yyless(yyleng-1);
					SET_YYLLOC();
					yylval.val.str = pstrdup(yytext);
					yylval.val.modificator = "f";
					yylval.val.keycode = -1;
					yylval.val.sep = NULL;
					return X_NCONST;
				}
{realfail2}		{
					/* throw back the [Ee][+-], and proceed as above */
					yyless(yyleng-2);
					SET_YYLLOC();
					yylval.val.str = pstrdup(yytext);
					yylval.val.modificator = "f";
					yylval.val.keycode = -1;
					yylval.val.sep = NULL;
					return X_NCONST;
				}


{identifier}	{
					char		   *ident;
					const char *keyword;
					int		keycode;

					SET_YYLLOC();

					/* nchar had better be a keyword! */
					keyword = orafce_scan_keyword("nchar", &keycode);

					/* Is it a keyword? */
					keyword = orafce_scan_keyword(yytext, &keycode);
					if (keyword != NULL)
					{
						yylval.val.str = (char*) keyword;
						yylval.val.keycode = keycode;
						yylval.val.modificator = NULL;
						yylval.val.sep = NULL;
						return X_KEYWORD;
					}

					/*
					 * No.  Convert the identifier to lower case, and truncate
					 * if necessary.
					 */
					ident = downcase_truncate_identifier(yytext, yyleng, true);
					yylval.val.str = ident;
					yylval.val.modificator = NULL;
					yylval.val.keycode = -1;
					yylval.val.sep = NULL;
					return X_IDENT;
				}

{other}			{
					SET_YYLLOC();
					yylval.val.str = yytext;
					yylval.val.modificator = NULL;
					yylval.val.keycode = yytext[0];
					yylval.val.sep = NULL;
					return X_OTHERS;
				}

<<EOF>>			{
					SET_YYLLOC();
					yyterminate();
				}

%%

/*
 * lexer_errposition
 *		Report a lexical-analysis-time cursor position, if possible.
 *
 * This is expected to be used within an ereport() call.  The return value
 * is a dummy (always 0, in fact).
 *
 * Note that this can only be used for messages from the lexer itself,
 * since it depends on scanbuf to still be valid.
 */
static int
lexer_errposition(void)
{
	int		pos;

	/* Convert byte offset to character number */
	pos = _pg_mbstrlen_with_len(scanbuf, orafce_sql_yylval.val.lloc) + 1;
	/* And pass it to the ereport mechanism */

#if PG_VERSION_NUM >= 130000

	errposition(pos);

	return pos;

#else

	return errposition(pos);

#endif

}

/*
 * yyerror
 *		Report a lexer or grammar error.
 *
 * The message's cursor position identifies the most recently lexed token.
 * This is OK for syntax error messages from the Bison parser, because Bison
 * parsers report error as soon as the first unparsable token is reached.
 * Beware of using yyerror for other purposes, as the cursor position might
 * be misleading!
 */
void
orafce_sql_yyerror(List **result, const char *message)
{
	const char *loc = scanbuf + orafce_sql_yylval.val.lloc;

	if (*loc == YY_END_OF_BUFFER_CHAR)
	{
		ereport(ERROR,
				(errcode(ERRCODE_SYNTAX_ERROR),
				 errmsg("%s at end of input", message),
				 lexer_errposition()));
	}
	else
	{
		ereport(ERROR,
				(errcode(ERRCODE_SYNTAX_ERROR),
				 errmsg("%s at or near \"%s\"", message, loc),
				 lexer_errposition()));
	}
}


/*
 * Called before any actual parsing is done
 */
void
orafce_sql_scanner_init(const char *str)
{
	Size	slen = strlen(str);

	/*
	 * Might be left over after ereport()
	 */
	if (YY_CURRENT_BUFFER)
		yy_delete_buffer(YY_CURRENT_BUFFER);

	/*
	 * Make a scan buffer with special termination needed by flex.
	 */
	scanbuflen = slen;
	scanbuf = palloc(slen + 2);
	memcpy(scanbuf, str, slen);
	scanbuf[slen] = scanbuf[slen + 1] = YY_END_OF_BUFFER_CHAR;
	scanbufhandle = yy_scan_buffer(scanbuf, slen + 2);

	/* initialize literal buffer to a reasonable but expansible size */
	literalalloc = 128;
	literalbuf = (char *) palloc(literalalloc);
	startlit();

	BEGIN(INITIAL);
}


/*
 * Called after parsing is done to clean up after fdate_scanner_init()
 */
void
orafce_sql_scanner_finish(void)
{
	yy_delete_buffer(scanbufhandle);
	pfree(scanbuf);
}

static void
addlit(char *ytext, int yleng)
{
	/* enlarge buffer if needed */
	if ((literallen+yleng) >= literalalloc)
	{
		do {
			literalalloc *= 2;
		} while ((literallen+yleng) >= literalalloc);
		literalbuf = (char *) repalloc(literalbuf, literalalloc);
	}
	/* append new data, add trailing null */
	memcpy(literalbuf+literallen, ytext, yleng);
	literallen += yleng;
	literalbuf[literallen] = '\0';
}


static void
addlitchar(unsigned char ychar)
{
	/* enlarge buffer if needed */
	if ((literallen+1) >= literalalloc)
	{
		literalalloc *= 2;
		literalbuf = (char *) repalloc(literalbuf, literalalloc);
	}
	/* append new data, add trailing null */
	literalbuf[literallen] = ychar;
	literallen += 1;
	literalbuf[literallen] = '\0';
}


/*
 * One might be tempted to write pstrdup(literalbuf) instead of this,
 * but for long literals this is much faster because the length is
 * already known.
 */
static char *
litbufdup(void)
{
	char *new;

	new = palloc(literallen + 1);
	memcpy(new, literalbuf, literallen+1);
	return new;
}


static unsigned char
unescape_single_char(unsigned char c)
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
			return c;
	}
}


