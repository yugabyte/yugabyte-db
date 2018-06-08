/*-------------------------------------------------------------------------
 *
 * normalize_query.h
 *		Normalize query string.
 *
 * This header file is created from pg_stat_statements.c to implement
 * normalization of query string.
 *
 * Portions Copyright (c) 2008-2017, PostgreSQL Global Development Group
 */
#ifndef NORMALIZE_QUERY_H
#define NORMALIZE_QUERY_H

/*
 * Struct for tracking locations/lengths of constants during normalization
 */
typedef struct pgssLocationLen
{
	int			location;		/* start offset in query text */
	int			length;			/* length in bytes, or -1 to ignore */
} pgssLocationLen;

/*
 * Working state for computing a query jumble and producing a normalized
 * query string
 */
typedef struct pgssJumbleState
{
	/* Jumble of current query tree */
	unsigned char *jumble;

	/* Number of bytes used in jumble[] */
	Size		jumble_len;

	/* Array of locations of constants that should be removed */
	pgssLocationLen *clocations;

	/* Allocated length of clocations array */
	int			clocations_buf_size;

	/* Current number of valid entries in clocations array */
	int			clocations_count;

	/* highest Param id we've seen, in order to start normalization correctly */
	int			highest_extern_param_id;
} pgssJumbleState;

static char *
generate_normalized_query(pgssJumbleState *jstate, const char *query,
						  int query_loc, int *query_len_p, int encoding);
static void JumbleQuery(pgssJumbleState *jstate, Query *query);

#define JUMBLE_SIZE		1024

#endif	/* NORMALIZE_QUERY_H */
