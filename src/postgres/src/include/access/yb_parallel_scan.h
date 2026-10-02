/*-------------------------------------------------------------------------
 *
 * yb_parallel_scan.h
 *	  Parallel partition-key distribution for YB scans.
 *
 *	  Manages a shared-memory cyclic buffer of partition-key
 *	  ranges that parallel workers consume to divide a YB table
 *	  scan across backends.  Used by nodeYbSeqscan (sequential)
 *	  and yb_lsm (index) parallel scans.  The AM callbacks
 *	  amestimateparallelscan and aminitparallelscan delegate to
 *	  functions declared here.
 *
 *	  Lifecycle: the index-scan side consumes this properly through
 *	  amapi.  The direct calls in nodeYbSeqscan's DSM functions are
 *	  an interim executor bypass of the tableam parallelscan_*
 *	  callbacks (parallelscan_estimate, parallelscan_initialize,
 *	  parallelscan_reinitialize), which upstream nodeSeqscan routes
 *	  through.  The mapping is nearly one to one, so routing it is
 *	  mechanical: once done, YBParallelPartitionKeysData's layout
 *	  can retreat to an AM-private header, and the executor keeps
 *	  only an opaque pointer.
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * permissions and limitations under the License.
 *
 * src/include/access/yb_parallel_scan.h
 *
 *-------------------------------------------------------------------------
 */

#pragma once

#include "postgres.h"

#include "access/relscan.h"
#include "storage/condition_variable.h"
#include "storage/spin.h"

/*
 * In fact, only initial fetch uses the default size, we estimate the number
 * based on average key length so far. In future we may be able to estimate
 * key length before fetch starts and always use estimated value.
 */
#define YB_PARTITION_KEYS_DEFAULT_FETCH_SIZE 100

/*
 * Size of the buffer (in bytes) to store the parallel keys.
 * Ideally it should be adjustable to specific relation, since different
 * relations has different key sizes. Moreover, key sizes may vary within a
 * relation.
 */
#define YB_PARTITION_KEY_DATA_CAPACITY 16384

/*
 * State of the parallel key fetch. We only allow one fetching backend at a
 * time, so FETCH_STATUS_WORKING indicates that one of the backends is actively
 * performing the fetch.
 * FETCH_STATUS_IDLE means that DocDB has more keys to return, a worker should
 * request them.
 * FETCH_STATUS_DONE means that all the keys has been fetched, no more fetch is
 * needed.
 */
typedef enum
{
	FETCH_STATUS_IDLE,
	FETCH_STATUS_WORKING,
	FETCH_STATUS_DONE
} YbFetchStatus;

/*
 * The parallel scan state structure
 * Most important data stored in the structure is the buffer of parallel key,
 * representing bounds of the blocks to scan by the parallel workers.
 * The keys are ordered, and parallel workers take one at a time in their
 * order as the lower bound of the scan range, and peek at the next key to use
 * as the higher bound. The key is essentially a byte array of variable length,
 * its actual length is stored as the prefixes to the actual data. The keys are
 * stored without gaps, so the end of the key is the beginning of the next. Keys
 * are stored continuously, so if the next key does not fit into the space
 * between the end of the last key and the end of the buffer, it is written from
 * the beginning of the buffer (referred as buffer wraparound).
 *
 * The low_offset and high_offset indicate two "active" keys in the buffer. The
 * low_offset is the key to be used and removed by the worker, grabbing next
 * scan range, and the high_offset points to key used by fetching worker: it is
 * the lower bound when making a request, and the point to append keys from the
 * response. In fact, the keys in the buffer form a FIFO queue. As it was
 * mentioned above, there may be unused space at the end of the buffer, because
 * of the key continuity. The key_data_size variable points to the beginning of
 * that space.
 *
 * The keys are fetched from the DocDB by a random worker when number of the
 * keys available in the buffer goes low. The fetch_status indicates current
 * fetch state. The last key in the buffer (highest key) is used as a fetch
 * starting point. For that reason we do not allow to take the last remaining
 * key from the buffer, until DocDB fetch is done.
 *
 * Purpose of other fields of the structure:
 *  - mutex to synchronize access to other fields
 *  - cv_empty is the conditional variable to wait while buffer has keys
 *    available to take
 *  - total_key_size, total_key_count key stats, also used to estimate number
 *    of keys to fetch (provides average key length).
 */
typedef struct YBParallelPartitionKeysData
{
	slock_t		mutex;			/* to synchronize access from the workers */
	ConditionVariable cv_empty; /* to wait until buffer has more entries */
	Oid			database_oid;	/* database of the target relation */
	Oid			table_relfilenode_oid;	/* relfilenode_oid of the target
										 * relation */
	bool		is_forward;		/* scan direction */
	YbFetchStatus fetch_status; /* if fetch is in progress or completed */
	int			low_offset;		/* offset of the lowest key in the buffer */
	int			high_offset;	/* offset of the highest key in the buffer */
	int			key_count;		/* number of keys in the buffer */
	double		total_key_size; /* combined length of the keys fetched so far */
	double		total_key_count;	/* number of keys fetched so far */
	int			key_data_size;	/* end of the last entry in the wraparound
								 * key_data */
	int			key_data_capacity;	/* YB_PARTITION_KEY_DATA_CAPACITY now, but
									 * may change */
	char		key_data[FLEXIBLE_ARRAY_MEMBER];	/* the buffer */
} YBParallelPartitionKeysData;

typedef struct YBParallelPartitionKeysData *YBParallelPartitionKeys;

/* tableam callback helpers: sizing, init, and rescan */
extern Size yb_estimate_parallel_size(void);
extern void yb_init_partition_key_data(void *data);
extern void yb_rescan_partition_key_data(void *data);

/* Prepare the buffer for a new parallel scan of the given relation. */
extern void ybParallelPrepare(YBParallelPartitionKeys ppk,
							  Relation relation,
							  bool is_forward);

/*
 * Claim the next partition-key range for this worker.  Returns false
 * when no more ranges are available (scan complete).
 */
extern bool ybParallelNextRange(YBParallelPartitionKeys ppk,
								const char **low_bound,
								size_t *low_bound_size,
								const char **high_bound,
								size_t *high_bound_size);
