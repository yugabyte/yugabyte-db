/*-------------------------------------------------------------------------
 *
 * query_scan.h
 *	  lexical scanner for SQL commands
 *
 * This lexer can be used to extract hints from query contents, taking into
 * account what the backend would consider as values, for example.
 *
 * Portions Copyright (c) 1996-2026, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * query_scan.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef QUERY_SCAN_H
#define QUERY_SCAN_H

#include "lib/stringinfo.h"

/* Abstract type for lexer's internal state */
typedef struct QueryScanStateData *QueryScanState;

/* Termination states for query_scan() */
typedef enum
{
	QUERY_SCAN_INCOMPLETE,			/* end of line, SQL statement incomplete */
	QUERY_SCAN_EOL					/* end of line, SQL possibly complete */
} QueryScanResult;

extern QueryScanState query_scan_create(void);
extern void query_scan_setup(QueryScanState state,
							 const char *line, int line_len,
							 int elevel);
extern void query_scan_finish(QueryScanState state);
extern QueryScanResult query_scan(QueryScanState state,
								  StringInfo query_buf);

#endif							/* QUERY_SCAN_H */
