/*-------------------------------------------------------------------------
 *
 * yb_queryjumble.h
 *    Query normalization and fingerprinting.
 *
 *-------------------------------------------------------------------------
 */
#ifndef YB_QUERYJUMBLE_H
#define YB_QUERYJUMBLE_H

#include "nodes/parsenodes.h"

typedef struct YbLocationLen
{
	int location;		/* start offset in query text */
	int length;			/* length in bytes, or -1 to ignore */
	bool squashed;		/* Location represents the beginning or end of a run
							of squashed constants? */
} YbLocationLen;

/*
 * Working state for computing a query jumble and producing a normalized
 * query string. This is a PG struct that has YB fields added.
 */
typedef struct YbJumbleState
{
	/* Jumble of current query tree */
	unsigned char *jumble;

	/* Number of bytes used in jumble[] */
	Size        jumble_len;

	/* Array of locations of constants that should be removed */
	YbLocationLen *clocations;

	/* Allocated length of clocations array */
	int         clocations_buf_size;

	/* Current number of valid entries in clocations array */
	int         clocations_count;

	/* highest Param id we've seen, in order to start normalization correctly */
	int         highest_extern_param_id;

	/*
	 * Count of the number of NULL nodes seen since last appending a value.
	 * These are flushed out to the jumble buffer before subsequent appends
	 * and before performing the final jumble hash.
	 */
	unsigned int pending_nulls;

	/* YB */

	/*
	 * This field tracks how to original RT index maps to the sorted
	 * range table list entry.
	 */
	int		   *ybRtIndexMap;

	/*
	 * Stores the final target list while we are computing the jumble.
	 */
	List	   *ybTargetList;

	bool		ybUseNames;

#ifdef USE_ASSERT_CHECKING
	/* The total number of bytes added to the jumble buffer */
	Size        total_jumble_len;
#endif
} YbJumbleState;

#endif /* YB_QUERYJUMBLE_H */
