%{
/* 
** A scanner for EMP-style numeric ranges 
*/

#include "postgres.h"
#include "parser/keywords.h"

/* No reason to constrain amount of data slurped */
#define YY_READ_BUF_SIZE 16777216

/* Avoid exit() on fatal scanner errors (a bit ugly -- see yy_fatal_error) */
#define fprintf(file, fmt, msg)  ereport(ERROR, (errmsg_internal("%s", msg)))

/* Handles to the buffer that the lexer uses internally */
static YY_BUFFER_STATE scanbufhandle;

/* flex 2.5.4 doesn't bother with a decl for this */
int orafce_sql_yylex(void);

extern YYSTYPE yylval;

void orafce_sql_scanner_init(const char *str);
void orafce_sql_scanner_finish(void);

%}

%option 8bit
%option never-interactive
%option nodefault
%option nounput
%option noyywrap
%option prefix="orafce_sql_yy"


n            [0-9]+
integer      {n}
real         ({n}\.{n}?|\.{n})
identifier   [A-Za-z_][A-Z_a-z0-9]*

  
%%

{

{integer}    yylval.str = yytext; return ICONST;

{real}	     yylval.str = yytext; return DCONST;

{identifier} {
	    const ScanKeyword *keyword;
	    keyword = ScanKeywordLookup(yytext);
	    if (keyword == NULL)
	    {
		yylval.str = yytext;
		return SCONST;
	    }
	    yylval.str = keyword->name;
	    return SCONST;
	}

[ \t\n\r]+ yylval.str = yytext; return OTHERS;
.          yylval.str = yytext; return OTHERS;  /* alert parser of the garbage */
}
%%

 
void
yyerror(const char *message)
{
	if (*yytext == YY_END_OF_BUFFER_CHAR)
	{
		ereport(ERROR,
				(errcode(ERRCODE_SYNTAX_ERROR),
				 errmsg("bad representation"),
				 /* translator: %s is typically "syntax error" */
				 errdetail("%s at end of input", message)));
	}
	else
	{
		ereport(ERROR,
				(errcode(ERRCODE_SYNTAX_ERROR),
				 errmsg("bad representation"),
				 /* translator: first %s is typically "syntax error" */
				 errdetail("%s at or near \"%s\"", message, yytext)));
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
