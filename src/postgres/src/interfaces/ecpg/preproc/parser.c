/*-------------------------------------------------------------------------
 *
 * parser.c
 *		Main entry point/driver for PostgreSQL grammar
 *
 * This should match src/backend/parser/parser.c, except that we do not
 * need to bother with re-entrant interfaces.
 *
 * Note: ECPG doesn't report error location like the backend does.
 * This file will need work if we ever want it to.
 *
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * IDENTIFICATION
 *	  src/interfaces/ecpg/preproc/parser.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres_fe.h"

#include "preproc_extern.h"
#include "preproc.h"


static bool have_lookahead;		/* is lookahead info valid? */
static int	lookahead_token;	/* one-token lookahead */
static YYSTYPE lookahead_yylval;	/* yylval for lookahead token */
static YYLTYPE lookahead_yylloc;	/* yylloc for lookahead token */
static char *lookahead_yytext;	/* start current token */

static bool check_uescapechar(unsigned char escape);
static bool ecpg_isspace(char ch);


/*
 * Intermediate filter between parser and base lexer (base_yylex in scan.l).
 *
 * This filter is needed because in some cases the standard SQL grammar
 * requires more than one token lookahead.  We reduce these cases to one-token
 * lookahead by replacing tokens here, in order to keep the grammar LALR(1).
 *
 * Using a filter is simpler than trying to recognize multiword tokens
 * directly in scan.l, because we'd have to allow for comments between the
 * words.  Furthermore it's not clear how to do that without re-introducing
 * scanner backtrack, which would cost more performance than this filter
 * layer does.
 *
 * We also use this filter to convert UIDENT and USCONST sequences into
 * plain IDENT and SCONST tokens.  While that could be handled by additional
 * productions in the main grammar, it's more efficient to do it like this.
 */
int
filtered_base_yylex(void)
{
	int			cur_token;
	int			next_token;
	YYSTYPE		cur_yylval;
	YYLTYPE		cur_yylloc;
	char	   *cur_yytext;

	/* Get next token --- we might already have it */
	if (have_lookahead)
	{
		cur_token = lookahead_token;
		base_yylval = lookahead_yylval;
		base_yylloc = lookahead_yylloc;
		base_yytext = lookahead_yytext;
		have_lookahead = false;
	}
	else
		cur_token = base_yylex();

	/*
	 * If this token isn't one that requires lookahead, just return it.
	 */
	switch (cur_token)
	{
		case NOT:
		case NULLS_P:
		case WITH:
		case UIDENT:
		case USCONST:
			break;
		default:
			return cur_token;
	}

	/* Save and restore lexer output variables around the call */
	cur_yylval = base_yylval;
	cur_yylloc = base_yylloc;
	cur_yytext = base_yytext;

	/* Get next token, saving outputs into lookahead variables */
	next_token = base_yylex();

	lookahead_token = next_token;
	lookahead_yylval = base_yylval;
	lookahead_yylloc = base_yylloc;
	lookahead_yytext = base_yytext;

	base_yylval = cur_yylval;
	base_yylloc = cur_yylloc;
	base_yytext = cur_yytext;

	have_lookahead = true;

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
		case UIDENT:
		case USCONST:
			/* Look ahead for UESCAPE */
			if (next_token == UESCAPE)
			{
				/* Yup, so get third token, which had better be SCONST */
				const char *escstr;

				/*
				 * Again save and restore lexer output variables around the
				 * call
				 */
				cur_yylval = base_yylval;
				cur_yylloc = base_yylloc;
				cur_yytext = base_yytext;

				/* Get third token */
				next_token = base_yylex();

				if (next_token != SCONST)
					mmerror(PARSE_ERROR, ET_ERROR, "UESCAPE must be followed by a simple string literal");

				/*
				 * Save and check escape string, which the scanner returns
				 * with quotes
				 */
				escstr = base_yylval.str;
				if (strlen(escstr) != 3 || !check_uescapechar(escstr[1]))
					mmerror(PARSE_ERROR, ET_ERROR, "invalid Unicode escape character");

				base_yylval = cur_yylval;
				base_yylloc = cur_yylloc;
				base_yytext = cur_yytext;

				/* Combine 3 tokens into 1 */
				base_yylval.str = psprintf("%s UESCAPE %s", base_yylval.str, escstr);

				/* Clear have_lookahead, thereby consuming all three tokens */
				have_lookahead = false;
			}

			if (cur_token == UIDENT)
				cur_token = IDENT;
			else if (cur_token == USCONST)
				cur_token = SCONST;
			break;
	}

	return cur_token;
}

/*
 * check_uescapechar() and ecpg_isspace() should match their equivalents
 * in pgc.l.
 */

/* is 'escape' acceptable as Unicode escape character (UESCAPE syntax) ? */
static bool
check_uescapechar(unsigned char escape)
{
	if (isxdigit(escape)
		|| escape == '+'
		|| escape == '\''
		|| escape == '"'
		|| ecpg_isspace(escape))
		return false;
	else
		return true;
}

/*
 * ecpg_isspace() --- return true if flex scanner considers char whitespace
 */
static bool
ecpg_isspace(char ch)
{
	if (ch == ' ' ||
		ch == '\t' ||
		ch == '\n' ||
		ch == '\r' ||
		ch == '\f')
		return true;
	return false;
}
