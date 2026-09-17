%top{
/*-------------------------------------------------------------------------
 *
 * query_scan.l
 *	  lexical scanner for SQL commands
 *
 * This code is mainly concerned with determining where query hints are
 * located.  The most reliable way to handle this is to borrow the backend's
 * flex lexer rules, lock, stock, and barrel.  The rules below are (except
 * for a few) the same as the backend's, but their actions are just ECHO
 * whereas the backend's actions generally do other things.
 *
 * XXX The rules in this file must be kept in sync with the backend lexer!!!
 *
 * XXX Avoid creating backtracking cases --- see the backend lexer for info.
 *
 * See query_scan_int.h for additional details.
 *
 * Portions Copyright (c) 1996-2026, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * IDENTIFICATION
 *	  query_scan.l
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "query_scan.h"
#include "mb/pg_wchar.h"

#include "query_scan_int.h"

}

%{
/* Avoid exit() on fatal scanner errors (a bit ugly -- see yy_fatal_error) */
#undef fprintf
#define fprintf(file, fmt, msg)  fprintf_to_ereport(fmt, msg)

static void
fprintf_to_ereport(const char *fmt, const char *msg)
{
	ereport(ERROR, (errmsg_internal("%s", msg)));
}

/*
 * We must have a typedef YYSTYPE for yylex's first argument, but this lexer
 * doesn't presently make use of that argument, so just declare it as int.
 */
typedef int YYSTYPE;

/*
 * Set the type of yyextra; we use it as a pointer back to the containing
 * QueryScanState.
 */
#define YY_EXTRA_TYPE QueryScanState

/* Return values from yylex() */
#define LEXRES_EOL			0	/* end of input */

#define ECHO query_scan_emit(cur_state, yytext, yyleng)

%}

%option reentrant
%option bison-bridge
%option 8bit
%option never-interactive
%option nodefault
%option noinput
%option nounput
%option noyywrap
%option warn
%option prefix="query_yy"

/*
 * All of the following definitions and rules should exactly match with
 * upstream PostgreSQL's src/backend/parser/scan.l so far as the flex
 * patterns are concerned.  The rule bodies are just ECHO as opposed to what
 * the backend does, however.  (But be sure to duplicate code that affects
 * the lexing process, such as BEGIN() and yyless().)
 */

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
 *  <xh> hexadecimal byte string
 *  <xhint> Query hints as C-style comments
 *  <xq> standard quoted strings
 *  <xqs> quote stop (detect continued strings)
 *  <xe> extended quoted strings (support backslash escape sequences)
 *  <xdolq> $foo$ quoted strings
 *  <xui> quoted identifier with Unicode escapes
 *  <xus> quoted string with Unicode escapes
 *
 * Note: we intentionally don't mimic the backend's <xeu> state; we have
 * no need to distinguish it from <xe> state, and no good way to get out
 * of it in error cases.  The backend just throws yyerror() in those
 * cases, but that's not an option here.
 */

%x xb
%x xc
%x xd
%x xh
%x xhint
%x xq
%x xqs
%x xe
%x xdolq
%x xui
%x xus

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
 * non_newline_space tracks all the other space characters except newlines.
 *
 * XXX if you change the set of whitespace characters, fix scanner_isspace()
 * to agree.
 */

space			[ \t\n\r\f\v]
non_newline_space	[ \t\f\v]
newline			[\n\r]
non_newline		[^\n\r]

comment			("--"{non_newline}*)

whitespace		({space}+|{comment})

/*
 * SQL requires at least one newline in the whitespace separating
 * string literals that are to be concatenated.  Silly, but who are we
 * to argue?  Note that {whitespace_with_newline} should not have * after
 * it, whereas {whitespace} should generally have a * after it...
 */

special_whitespace		({space}+|{comment}{newline})
non_newline_whitespace		({non_newline_space}|{comment})
whitespace_with_newline	({non_newline_whitespace}*{newline}{special_whitespace}*)

quote			'
/* If we see {quote} then {quotecontinue}, the quoted string continues */
quotecontinue	{whitespace_with_newline}{quote}

/*
 * {quotecontinuefail} is needed to avoid lexer backup when we fail to match
 * {quotecontinue}.  It might seem that this could just be {whitespace}*,
 * but if there's a dash after {whitespace_with_newline}, it must be consumed
 * to see if there's another dash --- which would start a {comment} and thus
 * allow continuation of the {quotecontinue} token.
 */
quotecontinuefail	{whitespace}*"-"?

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

/* Hexadecimal byte string */
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
xeunicode		[\\](u[0-9A-Fa-f]{4}|U[0-9A-Fa-f]{8})
xeunicodefail	[\\](u[0-9A-Fa-f]{0,3}|U[0-9A-Fa-f]{0,7})

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

/* Quoted identifier with Unicode escapes */
xuistart		[uU]&{dquote}

/* Quoted string with Unicode escapes */
xusstart		[uU]&{quote}

/* error rule to avoid backup */
xufailed		[uU]&

/*
 * Query hints as C-style comments
 *
 * This should take priority to C-style comments, while the inside and end
 * can match the rules cited below.
 */
xhintstart \/\*\+

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

ident_start		[A-Za-z\200-\377_]
ident_cont		[A-Za-z\200-\377_0-9\$]

identifier		{ident_start}{ident_cont}*

/* Assorted special-case operators and operator-like tokens */
typecast		"::"
dot_dot			\.\.
colon_equals	":="

/*
 * These operator-like tokens (unlike the above ones) also match the {operator}
 * rule, which means that they might be overridden by a longer match if they
 * are followed by a comment start or a + or - character. Accordingly, if you
 * add to this list, you must also add corresponding code to the {operator}
 * block to return the correct token in such cases. (This is not needed in
 * query_scan.l since the token value is ignored there.)
 */
equals_greater	"=>"
less_equals		"<="
greater_equals	">="
less_greater	"<>"
not_equals		"!="

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

/*
 * Numbers
 *
 * Unary minus is not part of a number here.  Instead we pass it separately to
 * the parser, and there it gets coerced via doNegate().
 *
 * {numericfail} is used because we would like "1..10" to lex as 1, dot_dot, 10.
 *
 * {realfail} is added to prevent the need for scanner
 * backup when the {real} rule fails to match completely.
 */
decdigit		[0-9]
hexdigit		[0-9A-Fa-f]
octdigit		[0-7]
bindigit		[0-1]

decinteger		{decdigit}(_?{decdigit})*
hexinteger		0[xX](_?{hexdigit})+
octinteger		0[oO](_?{octdigit})+
bininteger		0[bB](_?{bindigit})+

hexfail			0[xX]_?
octfail			0[oO]_?
binfail			0[bB]_?

numeric			(({decinteger}\.{decinteger}?)|(\.{decinteger}))
numericfail		{decinteger}+\.\.

real			({decinteger}|{numeric})[Ee][-+]?{decinteger}
realfail		({decinteger}|{numeric})[Ee][-+]

/* Positional parameters don't accept underscores. */
param			\${decdigit}+

/*
 * An identifier immediately following an integer literal is disallowed because
 * in some cases it's ambiguous what is meant: for example, 0x1234 could be
 * either a hexinteger or a decinteger "0" and an identifier "x1234".  We can
 * detect such problems by seeing if integer_junk matches a longer substring
 * than any of the XXXinteger patterns (decinteger, hexinteger, octinteger,
 * bininteger).  One "junk" pattern is sufficient because
 * {decinteger}{identifier} will match all the same strings we'd match with
 * {hexinteger}{identifier} etc.
 *
 * Note that the rule for integer_junk must appear after the ones for
 * XXXinteger to make this work correctly: 0x1234 will match both hexinteger
 * and integer_junk, and we need hexinteger to be chosen in that case.
 *
 * Also disallow strings matched by numeric_junk, real_junk and param_junk
 * for consistency.
 */
integer_junk	{decinteger}{identifier}
numeric_junk	{numeric}{identifier}
real_junk		{real}{identifier}
param_junk		\${decdigit}+{identifier}

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

%{
		/* Declare some local variables inside yylex(), for convenience */
		QueryScanState cur_state = yyextra;

		/*
		 * Force flex into the state indicated by start_state.  This has a
		 * couple of purposes: it lets some of the functions below set a new
		 * starting state without ugly direct access to flex variables, and it
		 * allows us to transition from one flex lexer to another so that we
		 * can lex different parts of the source string using separate lexers.
		 */
		BEGIN(cur_state->start_state);
%}

{whitespace}			{
					/*
					 * Note that the whitespace rule includes both true
					 * whitespace and single-line ("--" style) comments.
					 * We suppress whitespace until we have collected some
					 * non-whitespace data.  (This interacts with some
					 * decisions in MainLoop(); see there for details.)
					 */
				}

{xhintstart}			{
					/* Fail hard if there are more than one hint */
					if (cur_state->xhintnum > 0)
						query_yyerror(ERROR,
							      yytext,
							      "Multiple hints are not supported.");\

					/*
					 * Increment the hint counter as well as the comment
					 * to be able to correctly ignore the contents in
					 * nested contents.
					 */
					(cur_state->xhintnum)++;
					(cur_state->xcdepth)++;
					/* Put back any characters past slash-star-plus; see above */
					yyless(3);
					BEGIN(xhint);
}

<xhint>{
{xcstart}			{
					(cur_state->xcdepth)++;
					query_yyerror(cur_state->elevel,
						      yytext,
						      "Nested block comments are not supported.");
					/* Put back any characters past slash-star; see above */
					yyless(2);
				}
{xcinside}			{
					/*
					 * Print the contents of the hint into the output buffer.
					 * Ignore if we are in a comment.
					 */
					if (cur_state->xcdepth == 1)
						ECHO;
				}
{xcstop}			{
					if (cur_state->xcdepth > 0)
						(cur_state->xcdepth)--;

					if (cur_state->xcdepth <= 0)
						BEGIN(INITIAL);
				}
{op_chars}			{
					/* Special set of characters that can be authorized in hints */
					if (cur_state->xcdepth == 1)
						ECHO;
				}

\*+				{
					/* Special character that can be authorized in hints */
					if (cur_state->xcdepth == 1)
						ECHO;
				}
} /* <xhint> */

{xcstart}			{
					cur_state->xcdepth = 0;
					BEGIN(xc);
					/* Put back any characters past slash-star; see above */
					yyless(2);
					/* ignore */
				}

<xc>{
{xcstart}			{
					(cur_state->xcdepth)++;
					BEGIN(xc);
					/* Put back any characters past slash-star; see above */
					yyless(2);
					/* ignore */
				}

{xcstop}			{
					if (cur_state->xcdepth <= 0)
						BEGIN(INITIAL);
					else
						(cur_state->xcdepth)--;
					/* ignore */
				}

{xcinside}			{
					/* ignore */
				}

{op_chars}			{
					/* ignore */
				}

\*+				{
					/* ignore */
				}
} /* <xc> */

{xbstart}		{
					BEGIN(xb);
					/* ignore */
				}
<xh>{xhinside}	|
<xb>{xbinside}	{
					/* ignore */
				}

{xhstart}		{
					/* Hexadecimal bit type.
					 * At some point we should simply pass the string
					 * forward to the parser and label it there.
					 * In the meantime, place a leading "x" on the string
					 * to mark it for the input routine as a hex string.
					 */
					BEGIN(xh);
					/* ignore */
				}

{xnstart}		{
					yyless(1);	/* eat only 'n' this time */
					/* ignore */
				}

{xqstart}		{
					BEGIN(xq);
					/* ignore */
				}
{xestart}		{
					BEGIN(xe);
					/* ignore */
				}
{xusstart}		{
					BEGIN(xus);
					/* ignore */
				}

<xb,xh,xq,xe,xus>{quote} {
					/*
					 * When we are scanning a quoted string and see an end
					 * quote, we must look ahead for a possible continuation.
					 * If we don't see one, we know the end quote was in fact
					 * the end of the string.  To reduce the lexer table size,
					 * we use a single "xqs" state to do the lookahead for all
					 * types of strings.
					 */
					cur_state->state_before_str_stop = YYSTATE;
					BEGIN(xqs);
					/* ignore */
				}
<xqs>{quotecontinue} {
					/*
					 * Found a quote continuation, so return to the in-quote
					 * state and continue scanning the literal.  Nothing is
					 * added to the literal's contents.
					 */
					BEGIN(cur_state->state_before_str_stop);
					/* ignore */
				}
<xqs>{quotecontinuefail} |
<xqs>{other}	{
					/*
					 * Failed to see a quote continuation.  Throw back
					 * everything after the end quote, and handle the string
					 * according to the state we were in previously.
					 */
					yyless(0);
					BEGIN(INITIAL);
					/* There's nothing to echo ... */
				}

<xq,xe,xus>{xqdouble} {
					/* ignore */
				}
<xq,xus>{xqinside}  {
					/* ignore */
				}
<xe>{xeinside}  {
					/* ignore */
				}
<xe>{xeunicode} {
					/* ignore */
				}
<xe>{xeunicodefail}	{
					/* ignore */
				}
<xe>{xeescape}  {
					/* ignore */
				}
<xe>{xeoctesc}  {
					/* ignore */
				}
<xe>{xehexesc}  {
					/* ignore */
				}
<xe>.			{
					/* This is only needed for \ just before EOF */
					/* ignore */
				}

{dolqdelim}		{
					cur_state->dolqstart = pstrdup(yytext);
					BEGIN(xdolq);
					/* ignore */
				}
{dolqfailed}	{
					/* throw back all but the initial "$" */
					yyless(1);
					/* ignore */
				}
<xdolq>{dolqdelim} {
					if (strcmp(yytext, cur_state->dolqstart) == 0)
					{
						pfree(cur_state->dolqstart);
						cur_state->dolqstart = NULL;
						BEGIN(INITIAL);
					}
					else
					{
						/*
						 * When we fail to match $...$ to dolqstart, transfer
						 * the $... part to the output, but put back the final
						 * $ for rescanning.  Consider $delim$...$junk$delim$
						 */
						yyless(yyleng - 1);
					}
					/* ignore */
				}
<xdolq>{dolqinside} {
					/* ignore */
				}
<xdolq>{dolqfailed} {
					/* ignore */
				}
<xdolq>.		{
					/* This is only needed for $ inside the quoted text */
					/* ignore */
				}

{xdstart}		{
					BEGIN(xd);
					/* ignore */
				}
{xuistart}		{
					BEGIN(xui);
					/* ignore */
				}
<xd>{xdstop}	{
					BEGIN(INITIAL);
					/* ignore */
				}
<xui>{dquote}	{
					BEGIN(INITIAL);
					/* ignore */
				}
<xd,xui>{xddouble}	{
					/* ignore */
				}
<xd,xui>{xdinside}	{
					/* ignore */
				}

{xufailed}	{
					/* throw back all but the initial u/U */
					yyless(1);
					/* ignore */
				}

{typecast}		{
					/* ignore */
				}

{dot_dot}		{
					/* ignore */
				}

{colon_equals}	{
					/* ignore */
				}

{equals_greater} {
					/* ignore */
				}

{less_equals}	{
					/* ignore */
				}

{greater_equals} {
					/* ignore */
				}

{less_greater}	{
					/* ignore */
				}

{not_equals}	{
					/* ignore */
				}

{self}			{
					/* ignore */
				}

{operator}		{
					/*
					 * Check for embedded slash-star or dash-dash; those
					 * are comment starts, so operator must stop there.
					 * Note that slash-star or dash-dash at the first
					 * character will match a prior rule, not this one.
					 */
					int			nchars = yyleng;
					char	   *slashstar = strstr(yytext, "/*");
					char	   *dashdash = strstr(yytext, "--");

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
					if (nchars > 1 &&
						(yytext[nchars - 1] == '+' ||
						 yytext[nchars - 1] == '-'))
					{
						int			ic;

						for (ic = nchars - 2; ic >= 0; ic--)
						{
							char c = yytext[ic];
							if (c == '~' || c == '!' || c == '@' ||
								c == '#' || c == '^' || c == '&' ||
								c == '|' || c == '`' || c == '?' ||
								c == '%')
								break;
						}
						if (ic < 0)
						{
							/*
							 * didn't find a qualifying character, so remove
							 * all trailing [+-]
							 */
							do {
								nchars--;
							} while (nchars > 1 &&
								 (yytext[nchars - 1] == '+' ||
								  yytext[nchars - 1] == '-'));
						}
					}

					if (nchars < yyleng)
					{
						/* Strip the unwanted chars from the token */
						yyless(nchars);
					}
					/* ignore */
				}

{param}			{
					/* ignore */
				}
{param_junk}	{
					/* ignore */
				}

{decinteger}	{
					/* ignore */
				}
{hexinteger}	{
					/* ignore */
				}
{octinteger}	{
					/* ignore */
				}
{bininteger}	{
					/* ignore */
				}
{hexfail}		{
					/* ignore */
				}
{octfail}		{
					/* ignore */
				}
{binfail}		{
					/* ignore */
				}
{numeric}		{
					/* ignore */
				}
{numericfail}	{
					/* throw back the .., and treat as integer */
					yyless(yyleng - 2);
					/* ignore */
				}
{real}			{
					/* ignore */
				}
{realfail}		{
					/* ignore */
				}
{integer_junk}	{
					/* ignore */
				}
{numeric_junk}	{
					/* ignore */
				}
{real_junk}		{
					/* ignore */
				}


{identifier}	{
					/* ignore */
				}

{other}			{
					/* ignore */
				}

<<EOF>>			{
					cur_state->start_state = YY_START;
					return LEXRES_EOL;	/* end of input reached */
				}

%%

/* LCOV_EXCL_STOP */

/*
 * Create a lexer working state struct.
 */
QueryScanState
query_scan_create(void)
{
	QueryScanState state;

	state = (QueryScanStateData *) palloc0(sizeof(QueryScanStateData));
	yylex_init(&state->scanner);

	yyset_extra(state, state->scanner);

	/* Set up various fields */
	state->start_state = INITIAL;
	state->elevel = INFO;
	state->xcdepth = 0;			/* not really necessary */
	state->xhintnum = 0;
	if (state->dolqstart)
		pfree(state->dolqstart);
	state->dolqstart = NULL;

	return state;
}


/*
 * Set up to perform lexing of the given input line.
 *
 * The text at *line, extending for line_len bytes, will be scanned by
 * subsequent calls to the query_scan routines.  query_scan_finish should
 * be called when scanning is complete.  Note that the lexer retains
 * a pointer to the storage at *line --- this string must not be altered
 * or freed until after query_scan_finish is called.
 */
void
query_scan_setup(QueryScanState state,
				const char *line, int line_len,
				int elevel)
{
	/* Mustn't be scanning already */
	Assert(state->scanbufhandle == NULL);

	/* elevel for reports */
	state->elevel = elevel;

	/* Set up flex input buffer with appropriate translation and padding */
	state->scanbufhandle = query_scan_prepare_buffer(state, line, line_len,
												   &state->scanbuf);
	state->scanline = line;
}

/*
 * Do lexical analysis of SQL command text.
 *
 * The text previously passed to query_scan_setup is scanned, and appended
 * (possibly with transformation) to query_buf.
 *
 * The return value indicates the condition that stopped scanning:
 *
 * QUERY_SCAN_INCOMPLETE: the end of the line was reached, but we have an
 * incomplete SQL command.
 *
 * QUERY_SCAN_EOL: the end of the line was reached, and there is no lexical
 * reason to consider the command incomplete.  The caller may or may not
 * choose to send it.
 *
 * In the QUERY_SCAN_INCOMPLETE and QUERY_SCAN_EOL cases, query_scan_finish()
 * should be called next, then the cycle may be repeated with a fresh input
 * line.
 */
QueryScanResult
query_scan(QueryScanState state,
		  StringInfo query_buf)
{
	QueryScanResult result;
	int			lexresult;

	/* Must be scanning already */
	Assert(state->scanbufhandle != NULL);

	/* Set current output target */
	state->output_buf = query_buf;

	yy_switch_to_buffer(state->scanbufhandle, state->scanner);

	/* And lex. */
	lexresult = yylex(NULL, state->scanner);

	/*
	 * Check termination state and return appropriate result info.
	 */
	switch (lexresult)
	{
		case LEXRES_EOL:		/* end of input */
			switch (state->start_state)
			{
				case INITIAL:
				case xqs:		/* we treat this like INITIAL */
					/* the resulting query may be empty if there are no hints */
					result = QUERY_SCAN_EOL;
					break;
				case xb:
				case xc:
				case xd:
				case xh:
				case xhint:
				case xe:
				case xq:
				case xdolq:
				case xui:
				case xus:
					result = QUERY_SCAN_INCOMPLETE;
					break;
				default:
					/* can't get here */
					elog(ERROR, "invalid YY_START");
			}
			break;
		default:
			/* can't get here */
			elog(ERROR, "invalid yylex result\n");
	}

	return result;
}

/*
 * Clean up after scanning a string.  This flushes any unread input and
 * releases resources (but not the QueryScanState itself).  Note however
 * that this does not reset the lexer scan state.
 *
 * It is legal to call this when not scanning anything (makes it easier
 * to deal with error recovery).
 */
void
query_scan_finish(QueryScanState state)
{
	/* Done with the outer scan buffer, too */
	if (state->scanbufhandle)
		yy_delete_buffer(state->scanbufhandle, state->scanner);
	state->scanbufhandle = NULL;
	if (state->scanbuf)
		pfree(state->scanbuf);
	state->scanbuf = NULL;

	yylex_destroy(state->scanner);
	pfree(state);
}


/*
 * Set up a flex input buffer to scan the given data.  We always make a
 * copy of the data.
 *
 * NOTE SIDE EFFECT: the new buffer is made the active flex input buffer.
 */
YY_BUFFER_STATE
query_scan_prepare_buffer(QueryScanState state, const char *txt, int len,
						char **txtcopy)
{
	char	   *newtxt;

	/* Flex wants two \0 characters after the actual data */
	newtxt = palloc(len + 2);
	*txtcopy = newtxt;
	newtxt[len] = newtxt[len + 1] = YY_END_OF_BUFFER_CHAR;

	/* Copy the text */
	memcpy(newtxt, txt, len);

	return yy_scan_buffer(newtxt, len + 2, state->scanner);
}

void
query_yyerror(int elevel, const char *txt, const char *message)
{
	ereport(elevel,
		errmsg("pg_hint_plan: hint syntax error at or near \"%s\"", txt),
		errdetail("%s", message));
}

/*
 * query_scan_emit() --- body for ECHO macro
 *
 * NB: this must be used for ALL and ONLY the text copied from the flex
 * input data.  If you pass it something that is not part of the yytext
 * string, you are making a mistake.  Internally generated text can be
 * appended directly to state->output_buf.
 */
void
query_scan_emit(QueryScanState state, const char *txt, int len)
{
	StringInfo output_buf = state->output_buf;

	/* Simply copy the data to the output */
	appendBinaryStringInfo(output_buf, txt, len);
}
