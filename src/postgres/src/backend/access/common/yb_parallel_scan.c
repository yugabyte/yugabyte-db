/*-------------------------------------------------------------------------
 *
 * yb_parallel_scan.c
 *	  Parallel scan state and helpers for YugabyteDB tables.
 *
 *	  Implements the shared-memory partition-key buffer used by
 *	  parallel sequential and index scans on YB-backed relations.
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
 * src/backend/access/common/yb_parallel_scan.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include <math.h>

#include "access/yb_parallel_scan.h"
#include "pg_yb_utils.h"
#include "storage/condition_variable.h"
#include "storage/spin.h"
#include "utils/wait_event.h"

/******************************************************************************
 * Parallel scan on YB tables regardless of partitioning
 *
 * Based on the sequnce of the keys retrieved from the DocDB.
 * The keys are fetched and put into a cyclic buffer by any worker that finds
 * the number of keys in the buffer is too low. If not fetching keys, the
 * workers are continuously scanning ranges between the keys. Worker takes one
 * key from the buffer as the low range bound, and copies the next key as the
 * high range bound. A mutex is used to ensure that no more than one worker is
 * putting or taking rows. The very last key remaining in the buffer can not be
 * taken until the fetch is complete, because it is used as a starting key when
 * keys are requested from DocDB.
 ******************************************************************************/

/*
 * yb_estimate_parallel_size
 *
 * Calculate the size of the shared memory block to exchange information
 * between the workers.
 * TODO(#19467) The variable part of the block is a cyclic buffer to store keys.
 * It is a constant currently, but its size should be estimated before the
 * parallel scan starts. For normal operation it should hold several keys
 * (optimal number is TBD and may depend on the number of workers). However, in
 * the worst case scenario, it is safe to allow keys no longer than 1/3 of
 * the buffer. The buffer must keep the very last key, and have room before or
 * after to add one more key. In the worst case the very last key sits in the
 * middle, and the buffer 3 times bigger than the key ensures that one would
 * fit.
 */
Size
yb_estimate_parallel_size(void)
{
	Size		size = sizeof(YBParallelPartitionKeysData);

	return add_size(size, YB_PARTITION_KEY_DATA_CAPACITY);
}

/*
 * yb_init_partition_key_data
 *
 * Initialize the YBParallelPartitionKeys structure
 */
void
yb_init_partition_key_data(void *data)
{
	YBParallelPartitionKeys ppk = (YBParallelPartitionKeys) data;

	SpinLockInit(&ppk->mutex);
	ConditionVariableInit(&ppk->cv_empty);
	ppk->database_oid = InvalidOid;
	ppk->table_relfilenode_oid = InvalidOid;
	ppk->fetch_status = FETCH_STATUS_IDLE;
	ppk->low_offset = 0;
	ppk->high_offset = 0;
	ppk->key_count = 0;
	ppk->total_key_size = 0.0;
	ppk->total_key_count = 0.0;
	ppk->key_data_size = 0;
	ppk->key_data_capacity = YB_PARTITION_KEY_DATA_CAPACITY;
}

/*
 * yb_rescan_partition_key_data
 *
 * Reset the YBParallelPartitionKeys structure, that is, discard any key data
 * that may be still there. We also have to remove the table identification, as
 * an empty buffer with valid table ID would look like exhausted, not brand new,
 * and wouldn't be valid for fetch.
 */
void
yb_rescan_partition_key_data(void *data)
{
	YBParallelPartitionKeys ppk = (YBParallelPartitionKeys) data;

	/*
	 * It shouldn't be necessary to acquire the lock here, but there's no harm
	 * if we do.
	 */
	SpinLockAcquire(&ppk->mutex);
	ppk->database_oid = InvalidOid;
	ppk->table_relfilenode_oid = InvalidOid;
	ppk->fetch_status = FETCH_STATUS_IDLE;
	ppk->low_offset = 0;
	ppk->high_offset = 0;
	ppk->key_count = 0;
	ppk->total_key_size = 0.0;
	ppk->total_key_count = 0.0;
	ppk->key_data_size = 0;
	ppk->key_data_capacity = YB_PARTITION_KEY_DATA_CAPACITY;
	SpinLockRelease(&ppk->mutex);
}

typedef int yb_keylen_t;
#define KEY_LEN(ppk, key_offset) \
	(ppk)->key_data + (key_offset)
#define KEY_DATA(ppk, key_offset) \
	(ppk)->key_data + (key_offset) + sizeof(yb_keylen_t)

/*
 * yb_add_key_unsynchronized
 *
 * Copy next key into the cyclic buffer. Caller must assure exclusive access to
 * the YBParallelPartitionKeys structure.
 * Function checks for the available space in the buffer and returns false if it
 * is insufficient. Otherwise it appends the key into the buffer.
 */
static bool
yb_add_key_unsynchronized(YBParallelPartitionKeys ppk,
						  const char *key, yb_keylen_t key_len)
{
	/* Only the first key is allowed to be empty */
	Assert(key_len > 0 || ppk->key_count == 0);
	/* Special case: initially empty buffer */
	if (ppk->key_count == 0)
	{
		Assert(sizeof(key_len) + key_len <= ppk->key_data_capacity);
		memcpy(KEY_LEN(ppk, 0), &key_len, sizeof(yb_keylen_t));
		/* Update counters, etc */
		if (key_len > 0)
		{
			memcpy(KEY_DATA(ppk, 0), key, key_len);
			ppk->total_key_size += key_len;
			ppk->total_key_count += 1;
		}
		++ppk->key_count;
		ppk->key_data_size += sizeof(key_len) + key_len;
	}
	/* need to check empty space */
	else if (ppk->high_offset < ppk->low_offset)
	{
		/*
		 * Wrapped around buffer, the available space lays between the end of
		 * the high key and the beginning of the low key.
		 */
		yb_keylen_t high_key_len;

		memcpy(&high_key_len, KEY_LEN(ppk, ppk->high_offset), sizeof(yb_keylen_t));
		int			free_offset = ppk->high_offset + sizeof(int) + high_key_len;

		/* Check the room in the buffer */
		Assert(free_offset <= ppk->low_offset);
		if (ppk->low_offset - free_offset < sizeof(yb_keylen_t) + key_len)
			return false;
		memcpy(KEY_LEN(ppk, free_offset), &key_len, sizeof(yb_keylen_t));
		memcpy(KEY_DATA(ppk, free_offset), key, key_len);
		/* Update counters, etc */
		++ppk->key_count;
		ppk->high_offset = free_offset;
		ppk->total_key_size += key_len;
		ppk->total_key_count += 1;
	}
	else						/* The low_offset == high_offset iif key_count
								 * == 1 */
	{
		/*
		 * In not wrapped around buffer we maintain ppk->key_data_size
		 * pointing at the beginning of the free space.
		 */
		int			free_offset = ppk->key_data_size;

		/* Check for the trailing space capacity */
		if (ppk->key_data_capacity - free_offset >= sizeof(key_len) + key_len)
		{
			memcpy(KEY_LEN(ppk, free_offset), &key_len, sizeof(yb_keylen_t));
			memcpy(KEY_DATA(ppk, free_offset), key, key_len);
			/* Update counters, etc */
			++ppk->key_count;
			ppk->high_offset = free_offset;
			ppk->key_data_size += sizeof(key_len) + key_len;
			ppk->total_key_size += key_len;
			ppk->total_key_count += 1;
		}
		/*
		 * The key does not fit into remaining space at the end of the buffer,
		 * but there may be free space at the beginning, so we can wraparoud.
		 */
		else if (ppk->low_offset >= sizeof(key_len) + key_len)
		{
			memcpy(KEY_LEN(ppk, 0), &key_len, sizeof(yb_keylen_t));
			memcpy(KEY_DATA(ppk, 0), key, key_len);
			/* Update counters, etc */
			++ppk->key_count;
			ppk->high_offset = 0;
			ppk->total_key_size += key_len;
			ppk->total_key_count += 1;
		}
		/* No luck, let caller know */
		else
			return false;
	}
	return true;
}

/*
 * yb_remove_key_unsynchronized
 *
 * Remove the lowest key from the buffer. Caller must assure exclusive access to
 * the YBParallelPartitionKeys structure
 */
static void
yb_remove_key_unsynchronized(YBParallelPartitionKeys ppk)
{
	Assert(ppk->key_count > 0);
	yb_keylen_t key_len;

	--ppk->key_count;
	memcpy(&key_len, KEY_LEN(ppk, ppk->low_offset), sizeof(yb_keylen_t));
	/* Find offset of the next element */
	int			next = ppk->low_offset + sizeof(yb_keylen_t) + key_len;

	if (next == ppk->key_data_size)
	{
		/*
		 * The lowest key is actually the last one in the wrapped around cyclic
		 * buffer, so the next one starts from the beginning of the buffer data.
		 */
		next = 0;
		/*
		 * Also we need to update the key_data_size to point to the free space
		 * after the higest key, which will be the last after the removal,  as
		 * the buffer will no longer be wrapped around.
		 * Special case is if the lowest key is the only key in the buffer.
		 * Empty buffer is not suposed to be used, but it would make no harm
		 * to reset.
		 */
		if (ppk->key_count == 0)
		{
			ppk->high_offset = 0;
			ppk->key_data_size = 0;
		}
		else
		{
			memcpy(&key_len, KEY_LEN(ppk, ppk->high_offset), sizeof(yb_keylen_t));
			ppk->key_data_size = ppk->high_offset + sizeof(yb_keylen_t) + key_len;
		}
	}
	ppk->low_offset = next;
}

/*
 * Structure to encapsulate ppk_buffer_fetch_callback's state.
 *
 * One field, the ppk is a pointer to the YB parallel scan state, which has
 * the buffer to write the parallel keys to.
 * The other field is a counter of the discarded parallel keys.
 * Since callback discards a key due to insufficient space in the buffer, it
 * should discard all subsequent keys within the same fetch. The callback can
 * detect this by checking if the discarded value is greater than zero. Also
 * the fetcher may take the value into the account to plan the next fetch.
 */
typedef struct YbFetchKeysParam
{
	int			discarded;
	YBParallelPartitionKeys ppk;
} YbFetchKeysParam;

static void
ppk_buffer_fetch_callback(void *param, const char *key, size_t key_size)
{
	YbFetchKeysParam *fkp = (YbFetchKeysParam *) param;
	YBParallelPartitionKeys ppk = fkp->ppk;

	/* Once discarded, discard all the keys, just count them */
	if (fkp->discarded)
	{
		++fkp->discarded;
		return;
	}
	if (key_size)
	{
		bool		added;

		SpinLockAcquire(&ppk->mutex);
		/*
		 * Function is supposed to be called by the worker actively performing
		 * fetch.
		 */
		Assert(ppk->fetch_status == FETCH_STATUS_WORKING);
		added = yb_add_key_unsynchronized(ppk, key, key_size);
		SpinLockRelease(&ppk->mutex);
		/*
		 * If a value has been successfully added, notify other workers that
		 * may be waiting for available key. Key may fail to be added because
		 * the buffer has no room for it. That means the key and all subsequent
		 * messages of the block have to be discarded.
		 * Since this fetch cycle is, in fact, done, allow other workers to
		 * start another fetch, while this worker will be busy for some time
		 * throwing away remaining keys.
		 */
		if (added)
			ConditionVariableSignal(&ppk->cv_empty);
		else
			++fkp->discarded;
	}
	else
	{
		/* The last key from DocDB */
		SpinLockAcquire(&ppk->mutex);
		/* Update fetch status */
		Assert(ppk->fetch_status == FETCH_STATUS_WORKING);
		ppk->fetch_status = FETCH_STATUS_DONE;
		SpinLockRelease(&ppk->mutex);
		/*
		 * The fact that fetch is done makes very last key in the buffer
		 * available, so if there are workers waiting, let them know. One
		 * worker will be able to grab the last working range, other will be
		 * able to tell that their work is done.
		 */
		ConditionVariableBroadcast(&ppk->cv_empty);
	}
}

/*
 * yb_fetch_partition_keys
 *
 * Fetch some keys from the DocDB and put them into the parallel state
 * buffer. Function estimates how many keys to request, but if there are too
 * many keys to fit into the buffer, the remaining keys are discarded.
 */
static void
yb_fetch_partition_keys(YBParallelPartitionKeys ppk)
{
	const char *lower_bound_key = NULL;
	size_t		lower_bound_key_size = 0;
	const char *upper_bound_key = NULL;
	size_t		upper_bound_key_size = 0;
	uint64_t	max_num_ranges = YB_PARTITION_KEYS_DEFAULT_FETCH_SIZE;
	YbFetchKeysParam fkp = {0, ppk};

	/* Estimate fetch parameter values */
	SpinLockAcquire(&ppk->mutex);
	/* Until fetch is done at least one key must remain in the buffer */
	Assert(ppk->key_count > 0);
	if (ppk->total_key_count > 0)
	{
		const char *latest_key;
		yb_keylen_t key_len;
		double		average_key_size;

		/* Find average key size so far. */
		average_key_size = ppk->total_key_size / ppk->total_key_count;
		/* Account for the key length stored in the buffer */
		average_key_size += sizeof(yb_keylen_t);
		max_num_ranges =
			floor(ppk->key_data_capacity / average_key_size) - ppk->key_count;
		/* Global minimum and maximum limits for number of parallel ranges */
		if (max_num_ranges < 16)
			max_num_ranges = 16;
		else if (max_num_ranges > 1024)
			max_num_ranges = 1024;

		/*
		 * Determine starting point to fetch from.
		 * Currently the ending point is always null.
		 */
		memcpy(&key_len, KEY_LEN(ppk, ppk->high_offset), sizeof(yb_keylen_t));
		Assert(key_len);
		/*
		 * It is safe to refer the key data in place, since the latest key can
		 * not be removed from the buffer until fetch is completed.
		 */
		latest_key = KEY_DATA(ppk, ppk->high_offset);
		if (ppk->is_forward)
		{
			lower_bound_key = latest_key;
			lower_bound_key_size = key_len;
		}
		else
		{
			upper_bound_key = latest_key;
			upper_bound_key_size = key_len;
		}
	}
	SpinLockRelease(&ppk->mutex);

	/*
	 * We don't bother to take the lock to read ppk->key_data_capacity because
	 * it remains constant since its initialization. However, later on we will
	 * calculate fetch sizes and will take the lock, and capture
	 * ppk->key_data_capacity under that lock.
	 */
	HandleYBStatus(YBCGetTableKeyRanges(ppk->database_oid,
										ppk->table_relfilenode_oid,
										lower_bound_key, lower_bound_key_size,
										upper_bound_key, upper_bound_key_size,
										max_num_ranges, yb_parallel_range_size, ppk->is_forward,
										(ppk->key_data_capacity / 3) - sizeof(yb_keylen_t),
										ppk_buffer_fetch_callback, &fkp));
	SpinLockAcquire(&ppk->mutex);
	/* Update fetch status */
	if (ppk->fetch_status == FETCH_STATUS_WORKING)
		ppk->fetch_status = FETCH_STATUS_IDLE;
	else
		Assert(ppk->fetch_status == FETCH_STATUS_DONE);
	SpinLockRelease(&ppk->mutex);
	/* Log results for debugging and fine tuning */
	if (fkp.discarded)
		elog(LOG, "Had to discard %d keys out of requested %d. Plan better!",
			 fkp.discarded, (int) max_num_ranges);
	else
		elog(LOG, "Fetch of up to %d keys is completed", (int) max_num_ranges);
	/* All keys are accounted for, log stats */
	if (ppk->fetch_status == FETCH_STATUS_DONE)
		elog(LOG, "Fetch is done, received %.0f keys (%.0f bytes)",
			 ppk->total_key_count, ppk->total_key_size);
}

/*
 * ybParallelPrepare
 *
 * Assign the scan details (relation id and direction) to the parallel state.
 * In Postgres the parallel DSM block initialization routines are parameterless,
 * but for Yugabyte parallel scan we need to know some details about the scan.
 *
 * The scan details need to be set only once after the parallel state is
 * initialized, however due to the parameters, it is hard to fit into the DSM
 * initialization routines where the state is exclusive to the main worker.
 * Hence it is called by each parallel worker, and has to be idempotent in
 * concurrent environment. To achieve this, the function acquires the lock and
 * checks if the relation id is already set. If it is, the function returns
 * without making any changes. Otherwise it proceeds with the initialization.
 */
void
ybParallelPrepare(YBParallelPartitionKeys ppk, Relation relation,
							  bool is_forward)
{
	Oid database_oid = YBCGetDatabaseOid(relation);
	Oid rel_oid = YbGetRelfileNodeId(relation);
	SpinLockAcquire(&ppk->mutex);
	if (OidIsValid(ppk->table_relfilenode_oid))
	{
		Assert(ppk->table_relfilenode_oid == rel_oid &&
			   ppk->database_oid == database_oid);
		SpinLockRelease(&ppk->mutex);
		return;
	}

	/* We expect freshly initialized parallel state */
	Assert(ppk->fetch_status == FETCH_STATUS_IDLE);
	Assert(ppk->low_offset == 0);
	Assert(ppk->high_offset == 0);
	Assert(ppk->key_count == 0);

	ppk->database_oid = database_oid;
	ppk->table_relfilenode_oid = rel_oid;
	ppk->is_forward = is_forward;

	/*
	 * Put empty key as the first to be taken. Empty key corresponds to the
	 * beginning of the relation.
	 * Currently the parallel ranges start from the beginning. If the request
	 * has the scan bounds derived from the conditions, the PgGate will
	 * calculate the intersection of the scan bounds and the parallel range
	 * bounds. If the intersection is empty, the request won't be sent to DocDB
	 * for this parallel range. That way the overhead is minimized.
	 * TODO(#19465) It is possible to eliminate the overhead by taking the scan
	 * bounds from the request and using them here as the first and the last
	 * keys. We may have to change PgGate to move the conditions analysis
	 * logic earlier to take advantage of it at this point.
	 */
	yb_add_key_unsynchronized(ppk, NULL, 0);
	SpinLockRelease(&ppk->mutex);
}

typedef enum YbNextRangeResult
{
	NEXT_RANGE_WAIT,
	NEXT_RANGE_SUCCESS,
	NEXT_RANGE_FETCH,
	NEXT_RANGE_DONE
} YbNextRangeResult;

/*
 * yb_copy_key_unsynchronized
 *
 * Copy the lowest key from the buffer into newly palloc'ed space and return
 * pointer to the space in bound parameter.
 * Return NULL if the key is empty.
 */
static void
yb_copy_key_unsynchronized(YBParallelPartitionKeys ppk,
						   const char **bound,
						   size_t *bound_size)
{
	yb_keylen_t key_len;

	memcpy(&key_len, KEY_LEN(ppk, ppk->low_offset), sizeof(yb_keylen_t));
	*bound_size = key_len;
	if (key_len > 0)
	{
		*bound = (const char *) palloc(key_len);
		memcpy((void *) *bound, KEY_DATA(ppk, ppk->low_offset), key_len);
	}
	else
		*bound = NULL;
}

/*
 * ybParallelNextRange
 *
 * Take another range to work on from the parallel state.
 * If there are too few ybctids in the buffer this function may fetch some
 * first. Function may block if there are no ybctids available.
 * Return values low_bound and high_bound are the boundaries for the range.
 * If they are not NULLs, they are palloc'ed, caller must free them.
 * Function returns true if next range exists and valid bounds are returned.
 * If false is returned it means no more ranges and the worker should stop.
 */
bool
ybParallelNextRange(YBParallelPartitionKeys ppk,
					const char **low_bound,
					size_t *low_bound_size,
					const char **high_bound,
					size_t *high_bound_size)
{
	YbNextRangeResult result;

	while (true)
	{
		SpinLockAcquire(&ppk->mutex);
		/*
		 * Check if we should fetch key
		 * TODO(#19469) create config variable for key count triggering the
		 * fetch or find logic better than the magic number
		 */
		if (ppk->fetch_status == FETCH_STATUS_IDLE && ppk->key_count < 4)
		{
			/*
			 * We will fetch more ranges after mutex is released, for now,
			 * prevent other workers from attempting to fetch.
			 */
			ppk->fetch_status = FETCH_STATUS_WORKING;
			result = NEXT_RANGE_FETCH;
		}
		else
		{
			/*
			 * When performing forward scan, keys in the buffer are in the
			 * ascending order, so first one is going to be the lower bound,
			 * and second, if exists, the higher bound.
			 * When performing backward scan, keys in the buffer are in the
			 * descending order, so destination bouns are opposite.
			 */
			const char **first_key_dest_ptr = ppk->is_forward ? low_bound : high_bound;
			size_t	   *first_key_size_ptr = ppk->is_forward ? low_bound_size : high_bound_size;
			const char **second_key_dest_ptr = ppk->is_forward ? high_bound : low_bound;
			size_t	   *second_key_size_ptr = ppk->is_forward ? high_bound_size : low_bound_size;

			/* Have multiple keys, can take one. */
			if (ppk->key_count > 1)
			{
				yb_copy_key_unsynchronized(ppk, first_key_dest_ptr,
										   first_key_size_ptr);
				yb_remove_key_unsynchronized(ppk);
				yb_copy_key_unsynchronized(ppk, second_key_dest_ptr,
										   second_key_size_ptr);
				result = NEXT_RANGE_SUCCESS;
			}
			/* If the fetch is completed it is OK to take the last key. */
			else if (ppk->fetch_status == FETCH_STATUS_DONE)
			{
				if (ppk->key_count == 1)
				{
					yb_copy_key_unsynchronized(ppk, first_key_dest_ptr,
											   first_key_size_ptr);
					yb_remove_key_unsynchronized(ppk);
					*second_key_dest_ptr = NULL;
					*second_key_size_ptr = 0;
					result = NEXT_RANGE_SUCCESS;
				}
				else
				{
					/* No more data. */
					result = NEXT_RANGE_DONE;
				}
				/* The buffer should be empty now. */
				Assert(ppk->key_count == 0);
			}
			else
			{
				/* Wait otherwise. */
				result = NEXT_RANGE_WAIT;
			}
		}
		SpinLockRelease(&ppk->mutex);
		if (result == NEXT_RANGE_SUCCESS || result == NEXT_RANGE_DONE)
			/* All is done. */
			break;
		else if (result == NEXT_RANGE_FETCH)
			/* Fetch more keys and try again. */
			yb_fetch_partition_keys(ppk);
		else					/* result == NEXT_RANGE_WAIT */
		{
			elog(LOG, "ybParallelNextRange: waiting on empty queue");
			ConditionVariableSleep(&ppk->cv_empty,
								   WAIT_EVENT_YB_PARALLEL_SCAN_EMPTY);
		}
	}
	ConditionVariableCancelSleep();
	/*
	 * One value has been taken from the buffer, if there is a worker attempting
	 * to put fetched data it may be able to proceed now.
	 */
	Assert(result == NEXT_RANGE_SUCCESS || result == NEXT_RANGE_DONE);
	return result == NEXT_RANGE_SUCCESS;
}
