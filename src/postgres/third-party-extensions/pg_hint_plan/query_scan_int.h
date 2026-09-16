/*-------------------------------------------------------------------------
 *
 * query_scan_int.h
 *	  lexical scanner internal declarations
 *
 * This file declares the QueryScanStateData structure used by query_scan.l.
 *
 * We use a re-entrant lexer so that all the relevant state is in the
 * yyscan_t attached to the QueryScanState.  This avoids problems with
 * static state and allows multiple concurrent scanner operations.
 *
 *
 * Portions Copyright (c) 1996-2026, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * query_scan_int.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef QUERY_SCAN_INT_H
#define QUERY_SCAN_INT_H

#include "query_scan.h"

/*
 * These are just to allow this file to be compilable standalone for header
 * validity checking; in actual use, this file should always be included
 * from the body of a flex file, where these symbols are already defined.
 */
#ifndef YY_TYPEDEF_YY_BUFFER_STATE
#define YY_TYPEDEF_YY_BUFFER_STATE
typedef struct yy_buffer_state *YY_BUFFER_STATE;
#endif
#ifndef YY_TYPEDEF_YY_SCANNER_T
#define YY_TYPEDEF_YY_SCANNER_T
typedef void *yyscan_t;
#endif

/*
 * All working state of the lexer must be stored in QueryScanStateData
 * between calls.  This allows us to have multiple open lexer operations,
 * which is needed for concurrent use of the scanner.  The lexer itself is
 * not recursive, but it must be re-entrant.
 */
typedef struct QueryScanStateData
{
	yyscan_t	scanner;		/* Flex's state for this QueryScanState */

	StringInfo	output_buf;		/* current output buffer */

	int			elevel;			/* level of reports generated at parsing */

	/*
	 * These variables always refer to the outer buffer, never to any stacked
	 * variable-expansion buffer.
	 */
	YY_BUFFER_STATE scanbufhandle;
	char	   *scanbuf;		/* start of outer-level input buffer */
	const char *scanline;		/* current input line at outer level */

	/*
	 * All this state lives across successive input lines.  start_state is
	 * adopted by yylex() on entry, and updated with its finishing state on
	 * exit.
	 */
	int			start_state;	/* yylex's starting/finishing state */
	int			state_before_str_stop;	/* start cond. before end quote */
	int			xcdepth;		/* depth of nesting in slash-star comments */
	char	   *dolqstart;		/* current $foo$ quote start string */
	int			xhintnum;		/* number of query hints found */
} QueryScanStateData;


extern YY_BUFFER_STATE query_scan_prepare_buffer(QueryScanState state,
												 const char *txt, int len,
												 char **txtcopy);
extern void query_yyerror(int elevel, const char *txt, const char *message);

extern void query_scan_emit(QueryScanState state, const char *txt, int len);

#endif							/* QUERY_SCAN_INT_H */
