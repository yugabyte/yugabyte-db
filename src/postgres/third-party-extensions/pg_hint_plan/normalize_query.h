/*-------------------------------------------------------------------------
 *
 * normalize_query.h
 *		Normalize query string.
 *
 * This header file is created from pg_stat_statements.c to implement
 * normalization of query string.
 *
 * Portions Copyright (c) 2008-2023, PostgreSQL Global Development Group
 */
#ifndef NORMALIZE_QUERY_H
#define NORMALIZE_QUERY_H

static char *
generate_normalized_query(JumbleState *jstate, const char *query,
						  int query_loc, int *query_len_p);
#endif	/* NORMALIZE_QUERY_H */
