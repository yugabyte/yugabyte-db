/*-------------------------------------------------------------------------
 *
 * yb_qpm.c
 *    Query Plan Management/Yugabyte (Postgres layer)
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License. You may obtain
 * a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied.  See the License for the specific language governing
 * permissions and limitations under the License.
 *
 * IDENTIFICATION
 *	  src/backend/utils/misc/yb_qpm.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include <arpa/inet.h>
#include <zlib.h>

#include "access/hash.h"
#include "catalog/catalog.h"
#include "catalog/pg_authid_d.h"
#include "catalog/pg_namespace.h"
#include "common/ip.h"
#include "executor/executor.h"
#include "executor/spi.h"
#include "fmgr.h"
#include "funcapi.h"
#include "libpq/libpq-be.h"
#include "miscadmin.h"
#include "optimizer/planner.h"
#include "parser/scansup.h"
#include "pg_yb_utils.h"
#include "pgstat.h"
#include "postmaster/bgworker.h"
#include "postmaster/interrupt.h"
#include "storage/ipc.h"
#include "storage/latch.h"
#include "storage/lwlock.h"
#include "storage/procarray.h"
#include "storage/shmem.h"
#include "tcop/utility.h"
#include "utils/builtins.h"
#include "utils/fmgroids.h"
#include "utils/guc.h"
#include "utils/syscache.h"
#include "utils/timestamp.h"
#include "utils/uuid.h"
#include "yb/yql/pggate/util/ybc_util.h"
#include "yb/yql/pggate/ybc_pg_typedefs.h"
#include "yb/yql/pggate/ybc_pggate.h"
#include "yb_qpm.h"
#include "yb_query_diagnostics.h"

#define QPM_DUMP_FILE	PGSTAT_STAT_PERMANENT_DIRECTORY "/qpm.stat"
#define QPM_SKIP_TEXT "__YB_STAT_PLANS_SKIP"
static const uint32 QPM_FILE_HEADER = 0x20250425;

/* PostgreSQL major version number, changes in which invalidate all entries */
static const uint32 QPM_PG_MAJOR_VERSION = PG_VERSION_NUM / 100;
typedef struct YbQpmHashKey
{
	uint32		databaseId;
	uint32		userId;
	uint64		queryId;
	uint64		planId;
} YbQpmHashKey;

typedef struct YbQpmTiming
{
	double		totalTime;
	double		estTotalCost;
	double		worstTotalTime;
	uint32		cnt;
} YbQpmTiming;

/*
 * Stores an entry in the shared memory hash table.
 */
typedef struct YbQpmHashEntry
{
	YbQpmHashKey
				key;
	uint32		lruSlot;
	TimestampTz firstUsedTime;
	TimestampTz lastUsedTime;
	char		hintText[QPM_HINT_TEXT_SIZE];
	uint64		originalHintTextSize;
	uint64		compressedHintTextSize;
	char		planText[QPM_PLAN_TEXT_SIZE];
	uint64		originalPlanTextSize;
	uint64		compressedPlanTextSize;
	char		worstParamText[QPM_PARAM_TEXT_SIZE];
	uint64		originalWorstParamTextSize;
	uint64		compressedWorstParamTextSize;
	int64		useCount;
	YbQpmTiming timing;
} YbQpmHashEntry;

/*
 * Used when copying data out of the hash table.
 */
typedef struct YbQpmInfoEntry
{
	YbQpmHashKey
				key;
	TimestampTz firstUsedTime;
	TimestampTz lastUsedTime;
	char	   *hintText;
	char	   *planText;
	char	   *worstParamText;
	int64		useCount;
	YbQpmTiming timing;
} YbQpmInfoEntry;

/*
 * LRU entry - points to a hash table entry
 */
typedef struct YbQpmLruEntry
{
	YbQpmHashEntry *hashEntry;
	bool		referenced;		/* Is the entry referenced? */
	bool		valid;			/* Is the entry valid? */
} YbQpmLruEntry;

/*
 * the LRU clock
 */
typedef struct YbQpmLruClock
{
	uint32		clockHand;
	YbQpmLruEntry entries[FLEXIBLE_ARRAY_MEMBER];
} YbQpmLruClock;

typedef struct YbQpmSharedState
{
	LWLock		lock;
	double		totalTime;
	int64		numCalls;
} YbQpmSharedState;

static YbQpmSharedState *qpm = NULL;
static HTAB *qpmHashTable = NULL;
static LWLock *qpmLock = NULL;
static int	qpmNestedLevel = 0;
static YbQpmLruClock *qpmLruClock = NULL;
static bool qpmUnderUtility = false;

#define CHECK_GLOBALS if (unlikely(qpm == NULL || qpmHashTable == NULL || qpmLock == NULL || \
								   qpmLruClock == NULL)) Assert(false)

static ExecutorRun_hook_type prev_ExecutorRun = NULL;
static ExecutorFinish_hook_type prev_ExecutorFinish = NULL;
static ProcessUtility_hook_type prev_ProcessUtility = NULL;
static shmem_startup_hook_type prev_shmem_startup_hook = NULL;

static void YbQpmInstallHooks(void);

static void yb_qpm_ExecutorRun(QueryDesc *queryDesc,
							   ScanDirection direction,
							   uint64 count, bool execute_once);

static void yb_qpm_ExecutorFinish(QueryDesc *queryDesc);
static void yb_qpm_ProcessUtility(PlannedStmt *pstmt, const char *queryString,
								  bool readOnlyTree,
								  ProcessUtilityContext context, ParamListInfo params,
								  QueryEnvironment *queryEnv, DestReceiver *dest,
								  QueryCompletion *qc);
static uint32 qpmHash(const void *key, Size keysize);
static uint32 findLruSlot(YbCacheReplacementAlgorithmEnum replacementAlgorithm);
static uint32 findTrueLruSlot();
static uint32 findClockLruSlot();
static void qpmProcess(QueryDesc *queryDesc);
static char *qpmDecompress(uint64 uncompressedSize, Bytef * compressedData, uint64 compressed_size);
static uint64 qpmCompress(char *srcText, Bytef * compressBuffer, uint64 bufferLen);
static void qpm_shmem_shutdown(int code, Datum arg);
static int	qpmReadFile();
static int	qpmWriteFile();
static int	cmpHashEntriesUsingLastTimeUsed(const void *p1, const void *p2);
static Size qpmLruClockSize();

static void qpmPrepareParamsForInsert(QueryDesc *queryDesc,
									  char **paramText, char **planText, char **hintText,
									  double *totalTime, double *estTotalCost,
									  bool *freePlanText,
									  bool *freeParamText,
									  bool *freeHintText);
static void qpmCleanUpAfterInsert(bool freePlanText, char *planText,
								  bool freeParamText, char *paramText,
								  bool freeHintText, char *hintText);
static bool qpmInsert(uint32 databaseId, uint32 userid, uint64 queryId, uint64 planId,
					  char *hintText,
					  char *planText, QueryDesc *queryDesc,
					  TimestampTz *firstsedTime, TimestampTz *lastUsedTime,
					  double totalTime, double estTotalCost);
static long qpmNumEntries();
static bool qpmRemoveLocked(Oid databaseId, Oid userId, uint64 queryId, uint64 planId);
static long qpmRemove(Oid databaseId, Oid userId, uint64 queryId, uint64 planId);
static double qpmTotalTime();
static int64 qpmTotalCalls();
static bool qpmReferencesCatalogRelation(QueryDesc *queryDesc);
static void qpmConstructEntryInfo(YbQpmInfoEntry *infoEntry, YbQpmHashEntry *entry);
static long qpmGetAllEntries(YbQpmInfoEntry entries[]);
static void removeHintDelimiters(char *hintStr);
static void walkPlanState(PlanState *ps, bool save, List **pspList, int *pos);

/*
 * Calculate the size of the LRU clock.
 */
static Size
qpmLruClockSize()
{
	Size		size = offsetof(YbQpmLruClock, entries);

	size = add_size(size, mul_size(yb_qpm_configuration.max_cache_size, sizeof(YbQpmLruEntry)));
	size = MAXALIGN(size);
	return size;
}

/*
 * Shared memory shutdown function.
 */
static void
qpm_shmem_shutdown(int code, Datum arg)
{
	/* Don't try to dump during a crash. */
	if (code)
		return;

	(void) qpmWriteFile();

	return;
}

/*
 * Compare 2 hash table entries using last time used.
 */
static int
cmpHashEntriesUsingLastTimeUsed(const void *p1, const void *p2)
{
	int			cmp;
	YbQpmHashEntry *entry1 = *(YbQpmHashEntry **) p1;
	YbQpmHashEntry *entry2 = *(YbQpmHashEntry **) p2;

	if (entry1->lastUsedTime < entry2->lastUsedTime)
		cmp = -1;
	else if (entry1->lastUsedTime > entry2->lastUsedTime)
		cmp = 1;
	else
		cmp = 0;

	return cmp;
}

/*
 * Read the file (if it exists) that holds the saved QPM entries.
 */
static int
qpmReadFile()
{
	if (qpm == NULL || qpmHashTable == NULL || qpmLruClock == NULL)
		return -1;

	/*
	 * Attempt to load old statistics from the dump file.
	 */
	FILE	   *file = AllocateFile(QPM_DUMP_FILE, PG_BINARY_R);

	if (file == NULL)
	{
		if (errno != ENOENT)
			goto read_error;
		return -1;
	}

	int32		header;
	int32		pgver;
	int32		numEntries;

	if (fread(&header, sizeof(uint32), 1, file) != 1 ||
		fread(&pgver, sizeof(uint32), 1, file) != 1 ||
		fread(&numEntries, sizeof(int32), 1, file) != 1)
		goto read_error;

	if (header != QPM_FILE_HEADER)
		goto data_error;

	/* Set a bound on the number of entries to read. */
	int			maxEntries = Min(numEntries, yb_qpm_configuration.max_cache_size);

	if (numEntries > yb_qpm_configuration.max_cache_size)
		elog(WARNING, "QPM cache size (%d) < num. of entries in file (%d)."
			 " %d entries will be lost.", yb_qpm_configuration.max_cache_size, numEntries,
			 numEntries - yb_qpm_configuration.max_cache_size);

	/*
	 * Allocate an array to hold pointers to the entries we read.
	 */
	YbQpmHashEntry **entries = (YbQpmHashEntry **)
		palloc0(maxEntries * sizeof(YbQpmHashEntry *));

	size_t		entrySize = sizeof(YbQpmHashEntry);
	int			i;
	int			cnt = 0;

	for (i = 0; i < maxEntries; i++)
	{
		/*
		 * Read an entry.
		 */
		YbQpmHashEntry entry;

		if (fread(&entry, entrySize, 1, file) != 1)
			return -1;

		/*
		 * Insert the entry into the hash table.
		 */
		bool		found;
		YbQpmHashEntry *insertedEntry = (YbQpmHashEntry *) hash_search(qpmHashTable,
																	   &(entry.key),
																	   HASH_ENTER,
																	   &found);

		if (found)
			elog(ERROR, "entry should not be present");

		/*
		 * Populate the hash table entry.
		 */
		memcpy(insertedEntry, &entry, entrySize);

		/*
		 * Store a pointer to the inserted entry.
		 */
		entries[i] = insertedEntry;
		++cnt;
	}

	/*
	 * Clear the LRU clock.
	 */
	memset(qpmLruClock, 0, qpmLruClockSize());

	/*
	 * Populate the LRU clock. Loop over the array in reverse order
	 * since entries in higher positions are older (since we wrote the file
	 * from newest to oldest).
	 */
	for (i = cnt - 1; i >= 0; --i)
	{
		YbQpmHashEntry *entry = entries[i];
		uint32		lruSlot = findClockLruSlot();

		entry->lruSlot = lruSlot;
		YbQpmLruEntry *lruEntry = &(qpmLruClock->entries[lruSlot]);

		lruEntry->hashEntry = entry;
		lruEntry->referenced = true;
		lruEntry->valid = true;
	}

	/*
	 * Delete the file and free the pointer array.
	 */
	FreeFile(file);
	pfree(entries);

	return 0;

read_error:
	ereport(LOG,
			(errcode_for_file_access(),
			 errmsg("could not read file \"%s\": %m",
					QPM_DUMP_FILE)));
	goto fail;
data_error:
	ereport(LOG,
			(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			 errmsg("ignoring invalid data in file \"%s\"",
					QPM_DUMP_FILE)));
	goto fail;
fail:
	if (file)
		FreeFile(file);
	/* If possible, throw away the bogus file; ignore any error */
	unlink(QPM_DUMP_FILE);
	return -1;
}

/*
 * Write the QPM entries to a file.
 */
static int
qpmWriteFile()
{
	FILE	   *file;
	HASH_SEQ_STATUS hash_seq;
	int32		num_entries;
	YbQpmHashEntry *entry;

	/* Safety check ... shouldn't get here unless shmem is set up. */
	if (qpm == NULL || qpmHashTable == NULL || qpmLruClock == NULL)
	{
		return -1;
	}

	num_entries = hash_get_num_entries(qpmHashTable);
	if (num_entries == 0)
	{
		return -1;
	}

	file = AllocateFile(QPM_DUMP_FILE, PG_BINARY_W);
	if (file == NULL)
		goto error;

	if (fwrite(&QPM_FILE_HEADER, sizeof(uint32), 1,
			   file) != 1)
		goto error;

	if (fwrite(&QPM_PG_MAJOR_VERSION, sizeof(uint32), 1,
			   file) != 1)
		goto error;

	/* Write the number of entries. */
	if (fwrite(&num_entries, sizeof(int32), 1,
			   file) != 1)
		goto error;

	/*
	 * Allocate an array of pointers so we can sort the entries
	 * by last time used.
	 */
	YbQpmHashEntry **entries = (YbQpmHashEntry **) palloc0(num_entries *
														   sizeof(YbQpmHashEntry *));

	/*
	 * Loop over the hash table and store pointers to the entries
	 * in the allocated array.
	 */
	hash_seq_init(&hash_seq, qpmHashTable);
	int			cnt = 0;

	while ((entry = hash_seq_search(&hash_seq)) != NULL)
	{
		entries[cnt] = entry;
		++cnt;
	}

	/*
	 * Sort the array by time last used.
	 */
	qsort(&entries[0], cnt, sizeof(YbQpmHashEntry *),
		  cmpHashEntriesUsingLastTimeUsed);

	int			i;

	/*
	 * Loop over the array in reverse order since entries in higher positions
	 * have been accessed more recently. We want to make sure more recently
	 * used entries are stored first when the entries are read back in in
	 * case the cache size shrinks.
	 */
	for (i = cnt - 1; i >= 0; --i)
	{
		entry = entries[i];
		if (fwrite(entry, sizeof(YbQpmHashEntry), 1, file) != 1)
		{
			/* Note: We assume hash_seq_term won't change errno. */
			hash_seq_term(&hash_seq);
			pfree(entries);
			goto error;
		}
	}

	/* Free the pointer array. */
	pfree(entries);

	/* Delete the file. */
	if (FreeFile(file))
	{
		file = NULL;
		goto error;
	}

	return 0;

error:
	ereport(LOG,
			(errcode_for_file_access(),
			 errmsg("could not write file \"%s\"",
					QPM_DUMP_FILE)));
	if (file)
		FreeFile(file);
	unlink(QPM_DUMP_FILE);
	return -1;
}

static uint32
findLruSlot(YbCacheReplacementAlgorithmEnum replacementAlgorithm)
{
	uint32		slotId = 0;

	switch (replacementAlgorithm)
	{
		case YB_QPM_SIMPLE_CLOCK_LRU:
			slotId = findClockLruSlot();
			break;
		case YB_QPM_TRUE_LRU:
			slotId = findTrueLruSlot();
			break;
		default:
			elog(ERROR, "unknown replacement algorithm");
			break;
	}

	return slotId;
}

/*
 * Find an available slot using "true" LRU algorithm.
 */
static uint32
findTrueLruSlot()
{
	uint32		slotId = 0;

	if (qpmNumEntries() == yb_qpm_configuration.max_cache_size + 1)
	{
		/*
		 * The hash table is full so iterate and find least
		 * recently used entry.
		 */
		YbQpmHashEntry *oldestEntry = NULL;
		HASH_SEQ_STATUS status;
		YbQpmHashEntry *currentEntry;

		hash_seq_init(&status, qpmHashTable);

		while ((currentEntry
				= (YbQpmHashEntry *) hash_seq_search(&status)) != NULL)
		{
			if (oldestEntry == NULL ||
				currentEntry->lastUsedTime < oldestEntry->lastUsedTime)
				oldestEntry = currentEntry;
		}

		slotId = oldestEntry->lruSlot;
	}
	else
		/*
		 * Just get any entry since we are going to examine all
		 * entries when we need to find a victim.
		 */
		slotId = findClockLruSlot();

	return slotId;
}

/*
 * Find an available slot using simple clock LRU algorithm.
 */
static uint32
findClockLruSlot()
{
	uint32		slotId = 0;

	/*
	 * Sweep over the entries starting at the clockhand.
	 */
	bool		found = false;

	while (!found)
	{
		YbQpmLruEntry *entry = &(qpmLruClock->entries[qpmLruClock->clockHand]);

		Assert(!(entry->referenced) || entry->valid);
		if (!entry->valid || !entry->referenced)
		{
			/*
			 * Found an available entry.
			 */
			slotId = qpmLruClock->clockHand;
			qpmLruClock->clockHand = (qpmLruClock->clockHand + 1)
				% yb_qpm_configuration.max_cache_size;
			found = true;
		}
		else
		{
			/*
			 * This entry is used right now but mark it as unreferenced
			 * so it can get chosen if we get back to it.
			 */
			entry->referenced = false;
			qpmLruClock->clockHand = (qpmLruClock->clockHand + 1)
				% yb_qpm_configuration.max_cache_size;
		}
	}

	return slotId;
}

/*
 * Hash a QPM key.
 */
static uint32
qpmHash(const void *key, Size keysize)
{
	YbQpmHashKey *qpmHashKey = (YbQpmHashKey *) key;
	uint32		hashValue = 0;

	/* Break 64-bit values into two 32-bit halves. */
	uint32_t	queryId_lo = (uint32_t) (qpmHashKey->queryId);
	uint32_t	queryId_hi = (uint32_t) ((qpmHashKey->queryId) >> 32);
	uint32_t	planId_lo = (uint32_t) qpmHashKey->planId;
	uint32_t	planId_hi = (uint32_t) ((qpmHashKey->planId) >> 32);

	/* Mix all parts together. */
	hashValue = hash_combine(hashValue, queryId_lo);
	hashValue = hash_combine(hashValue, queryId_hi);
	hashValue = hash_combine(hashValue, planId_lo);
	hashValue = hash_combine(hashValue, planId_hi);

	hashValue = hash_combine(hashValue, qpmHashKey->databaseId);
	hashValue = hash_combine(hashValue, qpmHashKey->userId);

	return hashValue;
}

/*
 * See if 2 hash entries are equal.
 */
static int
qpmMatch(const void *key1, const void *key2, Size keysize)
{
	int			match;
	YbQpmHashKey *qpmHashKey1 = (YbQpmHashKey *) key1;
	YbQpmHashKey *qpmHashKey2 = (YbQpmHashKey *) key2;

	if (qpmHashKey1->databaseId == qpmHashKey2->databaseId &&
		qpmHashKey1->userId == qpmHashKey2->userId &&
		qpmHashKey1->queryId == qpmHashKey2->queryId &&
		qpmHashKey1->planId == qpmHashKey2->planId)
		match = 0;
	else
		match = 1;

	return match;
}

/*
 * Install the Executor hooks.
 */
static void
YbQpmInstallHooks(void)
{
	prev_ExecutorRun = ExecutorRun_hook;
	ExecutorRun_hook = yb_qpm_ExecutorRun;

	prev_ExecutorFinish = ExecutorFinish_hook;
	ExecutorFinish_hook = yb_qpm_ExecutorFinish;

	prev_ProcessUtility = ProcessUtility_hook;
	ProcessUtility_hook = yb_qpm_ProcessUtility;

	prev_shmem_startup_hook = shmem_startup_hook;
	shmem_startup_hook = YbQpmShmemInit;
}

static void
yb_qpm_ExecutorRun(QueryDesc *queryDesc, ScanDirection direction, uint64 count,
				   bool execute_once)
{
	/* Increase the nesting level. */
	++qpmNestedLevel;
	PG_TRY();
	{
		if (prev_ExecutorRun)
			prev_ExecutorRun(queryDesc, direction, count, execute_once);
		else
			standard_ExecutorRun(queryDesc, direction, count, execute_once);
	}
	PG_FINALLY();
	{
		/* Decrease the nesting level. */
		--qpmNestedLevel;
	}
	PG_END_TRY();
}

/*
 * Compress the text.
 */
static uint64
qpmCompress(char *srcText, Bytef * compressBuffer, uint64 bufferLen)
{
	uint64		compressedSize = 0;
	uint64		srcLen = strlen(srcText);

	memset(compressBuffer, 0, bufferLen);

	uint64		destLen = bufferLen - 1;

	if (yb_qpm_configuration.compress_text &&
		compress(compressBuffer, &destLen, (const Bytef *) srcText, srcLen) == Z_OK)
		compressedSize = destLen;
	else
	{
		/*
		 * Cannot compress so truncate.
		 */
		strncpy((char *) compressBuffer, srcText, bufferLen - 1);
		compressBuffer[bufferLen - 1] = '\0';
	}

	return compressedSize;
}

/*
 * Decompress the text.
 */
static char *
qpmDecompress(uint64 uncompressedSize, Bytef * compressedData, uint64 compressed_size)
{
	uint64		uncompressBufferSize = uncompressedSize * 2;
	Bytef	   *decompressBuffer = (Bytef *) palloc0(uncompressBufferSize);

	if (uncompress(decompressBuffer, &uncompressBufferSize, compressedData,
				   compressed_size) != Z_OK)
		elog(ERROR, "decompress failed");

	return (char *) decompressBuffer;
}

/*
 * See if the query references at least 1 catalog table.
 */
static bool
qpmReferencesCatalogRelation(QueryDesc *queryDesc)
{
	bool		referencesCatalogRel = false;

	ListCell   *lc;

	foreach(lc, queryDesc->plannedstmt->rtable)
	{
		RangeTblEntry *rtEntry = lfirst_node(RangeTblEntry, lc);

		if (rtEntry->rtekind == RTE_RELATION &&
			IsCatalogRelationOid(rtEntry->relid))
		{
			referencesCatalogRel = true;
			break;
		}
	}

	return referencesCatalogRel;
}

/*
 * Process a query/plan.
 */
static void
qpmProcess(QueryDesc *queryDesc)
{
	if (YbQpmIsEnabled() &&
		!qpmUnderUtility &&
		(qpmNestedLevel == 1 || yb_qpm_configuration.track == YB_QPM_TRACK_ALL) &&
		(queryDesc->plannedstmt->commandType == CMD_SELECT ||
		 queryDesc->plannedstmt->commandType == CMD_DELETE ||
		 queryDesc->plannedstmt->commandType == CMD_UPDATE ||
		 queryDesc->plannedstmt->commandType == CMD_MERGE ||
		 queryDesc->plannedstmt->commandType == CMD_INSERT) &&
		queryDesc->plannedstmt->queryId > 0 &&
		queryDesc->sourceText != NULL &&
		strstr(queryDesc->sourceText, QPM_SKIP_TEXT) == NULL &&
		(yb_qpm_configuration.track_catalog_queries ||
		 !qpmReferencesCatalogRelation(queryDesc)))
	{
		CHECK_GLOBALS;
		if (queryDesc->totaltime != NULL)
			InstrEndLoop(queryDesc->totaltime);

		/*
		 * Insert or update an entry.
		 */
		qpmInsert(0, 0, queryDesc->plannedstmt->queryId, 0,
				  NULL, NULL,
				  queryDesc, NULL, NULL, 0.0, 0.0);
	}
}

static void
yb_qpm_ExecutorFinish(QueryDesc *queryDesc)
{
	/* Increase the nesting level. */
	++qpmNestedLevel;

	PG_TRY();
	{
		if (prev_ExecutorFinish)
			prev_ExecutorFinish(queryDesc);
		else
			standard_ExecutorFinish(queryDesc);

		qpmProcess(queryDesc);
	}
	PG_FINALLY();
	{
		/* Decrease the nesting level. */
		--qpmNestedLevel;
	}
	PG_END_TRY();
}

static void
yb_qpm_ProcessUtility(PlannedStmt *pstmt,
					  const char *queryString,
					  bool readOnlyTree,
					  ProcessUtilityContext context, ParamListInfo params,
					  QueryEnvironment *queryEnv, DestReceiver *dest,
					  QueryCompletion *qc)
{
	bool		saveQpmUnderUtility;
	bool		decr;

	if (!IsA(pstmt->utilityStmt, ExecuteStmt))
	{
		/*
		 * Increase the nesting level if this is a utility statement
		 * that is not an EXECUTE.
		 */
		++qpmNestedLevel;
		saveQpmUnderUtility = qpmUnderUtility;
		qpmUnderUtility = true;
		decr = true;
	}
	else
		decr = false;

	PG_TRY();
	{
		if (prev_ProcessUtility)
			prev_ProcessUtility(pstmt, queryString,
								readOnlyTree,
								context, params, queryEnv,
								dest, qc);
		else
			standard_ProcessUtility(pstmt, queryString,
									readOnlyTree,
									context, params, queryEnv,
									dest, qc);
	}
	PG_FINALLY();
	{
		if (decr)
		{
			/* Decrease the nesting level. */
			--qpmNestedLevel;
			qpmUnderUtility = saveQpmUnderUtility;
		}
	}
	PG_END_TRY();
}

/*
 * Get the total elapsed time spent in QPM code.
 */
static double
qpmTotalTime()
{
	CHECK_GLOBALS;
	return qpm->totalTime;
}

/*
 * Get the total number of calls to the QPM code.
 */
static int64
qpmTotalCalls()
{
	CHECK_GLOBALS;
	return qpm->numCalls;
}

static void
qpmPrepareParamsForInsert(QueryDesc *queryDesc, char **paramText,
						  char **planText, char **hintText, double *totalTime, double *estTotalCost,
						  bool *freePlanText, bool *freeParamText, bool *freeHintText)
{
	Assert(*hintText == NULL);
	/*
	 * We have an actual query (i.e., not an explicit insert). Generate
	 * a hint string from the plan.
	 */
	*hintText = ybGenerateHintString(queryDesc->plannedstmt);

	if (*hintText != NULL)
	{
		*freeHintText = true;

		if (queryDesc->params != NULL)
		{
			/*
			 * If we have parameters, build a string representation.
			 */
			*paramText = BuildParamLogString(queryDesc->params,
											 NULL, -1);

			if (*paramText != NULL)
				*freeParamText = true;
		}

		Assert(*planText == NULL);
		*planText = YbQpmExplainPlan(queryDesc, yb_qpm_configuration.plan_format);

		Assert(*planText != NULL);
		*freePlanText = true;

		if (queryDesc->totaltime != NULL)
		{
			/*
			 * Get the total elapsed time from the query descriptor.
			 */
			*totalTime = queryDesc->totaltime->total;
			*estTotalCost = queryDesc->plannedstmt->planTree->total_cost;
		}
	}
}

/*
 * Insert or update an entry.
 */
static bool
qpmInsert(uint32 databaseId, uint32 userId, uint64 queryId, uint64 planId,
		  char *hintText, char *planText,
		  QueryDesc *queryDesc, TimestampTz *firstUsedTime,
		  TimestampTz *lastUsedTime, double totalTime,
		  double estTotalCost)
{
	CHECK_GLOBALS;

	if (planId == 0)
	{
		planId = ybGetPlanId(queryDesc->plannedstmt);
		databaseId = MyDatabaseId;
		userId = GetUserId();
	}

	bool		inserted = false;

	YbQpmHashEntry *hashEntry = NULL;
	YbQpmLruEntry *lruEntry = NULL;
	bool		found;
	TimestampTz currentTs;
	char	   *paramText = NULL;
	bool		freeHintText = false;
	bool		freePlanText = false;
	bool		freeParamText = false;

	YbQpmHashKey key = {.databaseId = databaseId,.userId = userId,
	.queryId = queryId,.planId = planId};

	if (firstUsedTime == NULL || lastUsedTime == NULL)
		currentTs = GetCurrentTimestamp();

	/*
	 * Get the start time.
	 */
	instr_time	start;
	instr_time	duration;

	INSTR_TIME_SET_CURRENT(start);

	/* Lock the QPM structures. */
	LWLockAcquire(qpmLock, LW_EXCLUSIVE);

	/*
	 * Since the hash table size is always the cache size + 1
	 * the insert will always either find an entry or insert a new one.
	 */
	hashEntry = (YbQpmHashEntry *) hash_search(qpmHashTable,
											   &key,
											   HASH_ENTER,
											   &found);

	if (found)
	{
		/*
		 * Found an entry so update it.
		 */
		lruEntry = &(qpmLruClock->entries[hashEntry->lruSlot]);

		/* Mark the LRU entry as referenced. */
		lruEntry->referenced = true;
		lruEntry->valid = true;

		/* Update the QPM info. */
		++(hashEntry->useCount);

		if (lastUsedTime == NULL)
			hashEntry->lastUsedTime = currentTs;
		else
			hashEntry->lastUsedTime = *lastUsedTime;

		if (totalTime > 0.0)
		{
			hashEntry->timing.totalTime += totalTime;
			hashEntry->timing.estTotalCost += estTotalCost;
			if (totalTime > hashEntry->timing.worstTotalTime)
				hashEntry->timing.worstTotalTime = totalTime;

			++(hashEntry->timing.cnt);
		}
	}
	else
	{
		/* Did not find a matching entry. */
		if (queryDesc != NULL)
		{
			/* Remove the 'extra' hash table entry and release the lock. */
			(void) hash_search(qpmHashTable, &(hashEntry->key), HASH_REMOVE, NULL);
			LWLockRelease(qpmLock);

			/*
			 * We have an actual query (i.e., not an explicit insert) since we
			 * have a query descriptor.
			 *
			 * An "explicit insert" is one where a user calls yb_pg_stat_plans_insert(), e.g.,
			 *
			 *    SELECT yb_pg_stat_plans_insert(1, 1, 1, 1, 'hint text', 'plan text', now(),
			 *                         now() + INTERVAL '1 second', 0.5, 1.2);
			 *
			 * This would insert a row with dbid = 1, userid = 1, queryid = 1, planid = 1,
			 * hint_text = 'hint text', plan_text = 'plan text', first_used = now(),
			 * last_used = new() + 1 sec., total_time = 0.5 secs, and
			 * est_total_cost = 1.2 secs.
			 *
			 * Since we do not have an explicit insert, generate plan and param text.
			 */
			qpmPrepareParamsForInsert(queryDesc, &paramText,
									  &planText, &hintText, &totalTime, &estTotalCost,
									  &freePlanText, &freeParamText,
									  &freeHintText);

			/*
			 * Generate hint, plan, and param text. If we can generate hints, insert
			 * an entry by recursive call.
			 */
			if (hintText != NULL)
				inserted = qpmInsert(databaseId, userId, queryId, planId, hintText, planText,
									 (QueryDesc *) NULL, (TimestampTz *) NULL,
									 (TimestampTz *) NULL, totalTime, estTotalCost);

			qpmCleanUpAfterInsert(freePlanText, planText, freeParamText, paramText,
								  freeHintText, hintText);
			return inserted;
		}

		if (hintText != NULL)
		{
			/*
			 * Find an LRU slot.
			 */
			uint32		lruSlot = findLruSlot(yb_qpm_configuration.cache_replacement_algorithm);

			lruEntry = &(qpmLruClock->entries[lruSlot]);

			if (qpmNumEntries() == yb_qpm_configuration.max_cache_size + 1)
			{
				/*
				 * Delete the victim to keep the number
				 * of entries == yb_qpm_configuration.max_cache_size.
				 */
				YbQpmHashEntry *removedEntry = hash_search(qpmHashTable,
														   &(lruEntry->hashEntry->key),
														   HASH_REMOVE,
														   (bool *) NULL);

				if (removedEntry == NULL)
				{
					LWLockRelease(qpmLock);
					elog(ERROR, "remove from QPM hash table failed");
				}
			}

			memset(hashEntry, 0, sizeof(YbQpmHashEntry));
			hashEntry->key = key;

			if (firstUsedTime == NULL)
				hashEntry->firstUsedTime = currentTs;
			else
				hashEntry->firstUsedTime = *firstUsedTime;

			if (lastUsedTime == NULL)
				hashEntry->lastUsedTime = currentTs;
			else
				hashEntry->lastUsedTime = *lastUsedTime;

			hashEntry->lruSlot = lruSlot;
			lruEntry = &(qpmLruClock->entries[lruSlot]);
			lruEntry->hashEntry = hashEntry;

			/* Remember the original size of the hint text. */
			hashEntry->originalHintTextSize = strlen(hintText);

			/*
			 * Insert hint text, compressing if necessary,
			 */
			if (hashEntry->originalHintTextSize + 1 < QPM_HINT_TEXT_SIZE)
			{
				/* Hint text fits in the slot so copy it. */
				strcpy(hashEntry->hintText, hintText);
				hashEntry->compressedHintTextSize = 0;
			}
			else
				/* Compress the text. */
				hashEntry->compressedHintTextSize \
					= qpmCompress(hintText,
								  (Bytef *) (hashEntry->hintText),
								  QPM_HINT_TEXT_SIZE);

			Assert(planText != NULL);

			/*
			 * Insert plan text, compressing (or truncating) if necessary,
			 */
			hashEntry->originalPlanTextSize = strlen(planText);

			if (hashEntry->originalPlanTextSize + 1 <
				QPM_PLAN_TEXT_SIZE)
			{
				strcpy(hashEntry->planText, planText);
				hashEntry->compressedPlanTextSize = 0;
			}
			else
				hashEntry->compressedPlanTextSize
					= qpmCompress(planText,
								  (Bytef *) (hashEntry->planText),
								  QPM_PLAN_TEXT_SIZE);

			/*
			 * Check for param text.
			 */
			if (paramText != NULL)
			{
				/*
				 * Insert hint text, compressing (or truncating) if necessary,
				 */
				hashEntry->originalWorstParamTextSize
					= strlen(paramText);

				if (hashEntry->originalWorstParamTextSize + 1 <
					QPM_PARAM_TEXT_SIZE)
				{
					strcpy(hashEntry->worstParamText, paramText);
					hashEntry->compressedWorstParamTextSize = 0;
				}
				else
					hashEntry->compressedWorstParamTextSize
						= qpmCompress(paramText,
									  (Bytef *) (hashEntry->worstParamText),
									  QPM_PARAM_TEXT_SIZE);
			}

			lruEntry->referenced = true;
			lruEntry->valid = true;

			hashEntry->useCount = 1;

			if (totalTime > 0.0)
			{
				hashEntry->timing.totalTime += totalTime;
				hashEntry->timing.estTotalCost += estTotalCost;
				if (totalTime > hashEntry->timing.worstTotalTime)
					hashEntry->timing.worstTotalTime = totalTime;

				++(hashEntry->timing.cnt);
			}

			inserted = true;
		}
		else
		{
			/*
			 * Plan hints could not be generated, or were not provided by the caller,
			 * so delete the hash table entry.
			 */
			(void) hash_search(qpmHashTable, &(hashEntry->key), HASH_REMOVE, NULL);
		}
	}

	/*
	 * Get the stop time.
	 */
	INSTR_TIME_SET_CURRENT(duration);
	INSTR_TIME_SUBTRACT(duration, start);

	double		elapsed = INSTR_TIME_GET_DOUBLE(duration);

	/*
	 * Increment the total elapsed time and the number
	 * of QPM calls.
	 */
	qpm->totalTime += elapsed;
	++(qpm->numCalls);

	LWLockRelease(qpmLock);

	return inserted;
}

static void
qpmCleanUpAfterInsert(bool freePlanText, char *planText,
					  bool freeParamText, char *paramText,
					  bool freeHintText, char *hintText)
{
	if (freePlanText)
		pfree(planText);

	if (freeParamText)
		pfree(paramText);

	if (freeHintText)
		pfree(hintText);
}

/*
 * Get the total number of entries in the hash table.
 */
static long
qpmNumEntries()
{
	long		cnt = 0;

	if (qpmHashTable != NULL)
		cnt = hash_get_num_entries(qpmHashTable);

	return cnt;
}

static bool
qpmRemoveLocked(Oid databaseId, Oid userId, uint64 queryId, uint64 planId)
{
	YbQpmHashKey key = {.databaseId = databaseId,.userId = userId,
	.queryId = queryId,.planId = planId};
	YbQpmHashEntry *entry = hash_search(qpmHashTable, &key, HASH_FIND, NULL);
	bool		removed;

	if (entry != NULL)
	{
		YbQpmLruEntry *lruEntry = &(qpmLruClock->entries[entry->lruSlot]);

		lruEntry->hashEntry = NULL;
		lruEntry->referenced = false;
		lruEntry->valid = false;

		(void) hash_search(qpmHashTable, &key, HASH_REMOVE, NULL);

		removed = true;
	}
	else
		removed = false;

	return removed;
}

/*
 * Remove an entry, or all entries, for a given query id.
 * Values of '0' for any of the parameters means 'match all
 * values', i.e., a wildcard.
 */
static long
qpmRemove(Oid databaseId, Oid userId, uint64 queryId, uint64 planId)
{
	long		removedCnt = 0;

	CHECK_GLOBALS;
	LWLockAcquire(qpmLock, LW_EXCLUSIVE);

	if (databaseId > 0 && userId > 0 && queryId > 0 && planId > 0)
	{
		if (qpmRemoveLocked(databaseId, userId, queryId, planId))
			removedCnt = 1;
	}
	else
	{
		int			i = 0;

		for (i = 0; i < yb_qpm_configuration.max_cache_size; ++i)
		{
			YbQpmLruEntry *lruEntry = &(qpmLruClock->entries[i]);

			if (lruEntry->referenced || lruEntry->valid)
			{
				YbQpmHashKey *key = &(lruEntry->hashEntry->key);

				if ((databaseId == 0 || key->databaseId == databaseId) &&
					(userId == 0 || key->userId == userId) &&
					(queryId == 0 || key->queryId == queryId) &&
					(planId == 0 || key->planId == planId))
				{
					bool		removed = qpmRemoveLocked(key->databaseId, key->userId,
														  key->queryId, key->planId);

					if (!removed)
					{
						LWLockRelease(qpmLock);
						elog(ERROR, "remove from QPM hash table failed");
					}

					++removedCnt;
				}
			}
		}

		if (databaseId == 0 && userId == 0 && queryId == 0 && planId == 0)
		{
			/*
			 * Removing all entries so should have no entries left.
			 */
			Assert(hash_get_num_entries(qpmHashTable) == 0);

			qpm->totalTime = 0.0;
			qpm->numCalls = 0;
		}
	}

	LWLockRelease(qpmLock);

	return removedCnt;
}

/*
 * Construct information to pass QPM information around.
 */
static void
qpmConstructEntryInfo(YbQpmInfoEntry *infoEntry, YbQpmHashEntry *entry)
{
	memset(infoEntry, 0, sizeof(YbQpmInfoEntry));
	infoEntry->key = entry->key;
	infoEntry->firstUsedTime = entry->firstUsedTime;
	infoEntry->lastUsedTime = entry->lastUsedTime;

	if (entry->originalHintTextSize > 0)
	{
		if (entry->compressedHintTextSize == 0)
			infoEntry->hintText = pstrdup(entry->hintText);
		else
			infoEntry->hintText = qpmDecompress(entry->originalHintTextSize,
												(Bytef *) entry->hintText,
												entry->compressedHintTextSize);
	}

	if (entry->originalPlanTextSize > 0)
	{
		if (entry->compressedPlanTextSize == 0)
			infoEntry->planText = pstrdup(entry->planText);
		else
			infoEntry->planText = qpmDecompress(entry->originalPlanTextSize,
												(Bytef *) entry->planText,
												entry->compressedPlanTextSize);
	}

	if (entry->originalWorstParamTextSize > 0)
	{
		if (entry->compressedWorstParamTextSize == 0)
			infoEntry->worstParamText = pstrdup(entry->worstParamText);
		else
			infoEntry->worstParamText = qpmDecompress(entry->originalWorstParamTextSize,
													  (Bytef *) entry->worstParamText,
													  entry->compressedWorstParamTextSize);
	}

	infoEntry->useCount = entry->useCount;
	infoEntry->timing = entry->timing;

	return;
}

/*
 * Retrieve all the entries in the QPM hash table. This
 * is done by populating an array of YbQpmInfoEntry elements.
 * The array is assumed to be large enough to hold all of
 * the entries.
 */
static long
qpmGetAllEntries(YbQpmInfoEntry entries[])
{
	long		cnt = 0;
	Oid			userid = GetUserId();
	bool		is_allowed_role = false;

	/*
	 * Superusers or roles with the privileges of pg_read_all_stats members
	 * are allowed
	 */
	is_allowed_role = has_privs_of_role(userid, ROLE_PG_READ_ALL_STATS);

	if (qpmHashTable != NULL)
	{
		HASH_SEQ_STATUS status;
		YbQpmHashEntry *entry;

		LWLockAcquire(qpmLock, LW_SHARED);

		hash_seq_init(&status, qpmHashTable);

		while ((entry = (YbQpmHashEntry *) hash_seq_search(&status)) != NULL)
		{
			if (is_allowed_role)
			{
				YbQpmInfoEntry *infoEntry = &(entries[cnt]);

				qpmConstructEntryInfo(infoEntry, entry);
				++cnt;
			}
		}

		LWLockRelease(qpmLock);
	}

	return cnt;
}

void
YbQpmInit(void)
{
	YbQpmInstallHooks();
	EnableQueryId();
}

/*
 * Compute the space needed for QPM-related shared memory.
 */
Size
YbQpmShmemSize(void)
{
	Size		size = sizeof(YbQpmSharedState);

	size = add_size(size, qpmLruClockSize());
	size = add_size(size, hash_estimate_size(yb_qpm_configuration.max_cache_size + 1,
											 sizeof(YbQpmHashEntry)));
	size = MAXALIGN(size);

	return size;
}

/*
 * Allocate and initialize QPM-related shared memory.
 */
void
YbQpmShmemInit(void)
{
	bool		foundSharedState = false;

	if (prev_shmem_startup_hook)
		prev_shmem_startup_hook();

	qpm = NULL;
	qpmHashTable = NULL;
	qpmLruClock = NULL;
	qpmLock = NULL;

	LWLockAcquire(AddinShmemInitLock, LW_EXCLUSIVE);

	qpm = ShmemInitStruct("qpm", sizeof(YbQpmSharedState), &foundSharedState);

	if (!foundSharedState)
	{
		/*
		 * Initialize the QPM lock.
		 */
		qpmLock = &(qpm->lock);
		int			trancheId = LWLockNewTrancheId();

		LWLockInitialize(qpmLock, trancheId);
		LWLockRegisterTranche(trancheId, "qpm");

		/*
		 * Allocate the LRU clock.
		 */
		bool		foundLruClock;

		qpmLruClock = ShmemInitStruct("qpmLruClock", qpmLruClockSize(), &foundLruClock);
		(void) foundLruClock;

		memset(qpmLruClock, 0, qpmLruClockSize());
	}

	HASHCTL		info;

	memset(&info, 0, sizeof(info));
	info.keysize = sizeof(YbQpmHashKey);
	info.entrysize = sizeof(YbQpmHashEntry);
	info.hash = qpmHash;
	info.match = qpmMatch;

	/*
	 * Initialize the hash table.
	 */
	qpmHashTable = ShmemInitHash("qpm: hashtable", yb_qpm_configuration.max_cache_size + 1,
								 yb_qpm_configuration.max_cache_size + 1, &info,
								 HASH_ELEM | HASH_FUNCTION | HASH_COMPARE | HASH_CONTEXT);

	qpm->totalTime = 0;
	qpm->numCalls = 0;

	LWLockRelease(AddinShmemInitLock);

	/*
	 * If we're in the postmaster (or a standalone backend...), set up a shmem
	 * exit hook to dump the statistics to disk.
	 */
	if (!IsUnderPostmaster)
		on_shmem_exit(qpm_shmem_shutdown, (Datum) 0);

	/*
	 * Done if some other process already completed our initialization.
	 */
	if (foundSharedState)
		return;

	/*
	 * Read the QPM entry file (if any) and populate the QPM structures.
	 */
	(void) qpmReadFile();

	unlink(QPM_DUMP_FILE);

	return;
}

/*
 * This structure stores the instrumentation pointers (if present).
 * We want these to all be NULL so no incorrect modification of
 * their values happens when generating the plan text.
 */
typedef struct YbPlanStatePtr
{
	PlanState  *ps;
	Instrumentation *instrument;
	WorkerInstrumentation *worker_instrument;
	struct SharedJitInstrumentation *worker_jit_instrument;
} YbPlanStatePtr;

static void
walkPlanState(PlanState *ps, bool save, List **pspList, int *pos)
{
	if (ps == NULL)
		return;

	if (save)
	{
		/*
		 * Save the pointers and set them to NULL. NULL pointers
		 * are not allowed in a List hence the need for the
		 * PlanStatePtr structure.
		 */
		YbPlanStatePtr *psp = palloc0(sizeof(YbPlanStatePtr));

		psp->ps = ps;
		psp->instrument = ps->instrument;
		psp->worker_instrument = ps->worker_instrument;
		psp->worker_jit_instrument = ps->worker_jit_instrument;
		*pspList = lappend(*pspList, psp);
		ps->instrument = NULL;
		ps->worker_instrument = NULL;
		ps->worker_jit_instrument = NULL;
		++(*pos);
	}
	else
	{
		/*
		 * Restore the instrumentation pointers.
		 *
		 * Since the traversal order is the same we can use positional
		 * access to the list of stored pointer information.
		 */
		YbPlanStatePtr *psp = (YbPlanStatePtr *) list_nth(*pspList, *pos);

		Assert(psp->ps == ps);
		ps->instrument = psp->instrument;
		ps->worker_instrument = psp->worker_instrument;
		ps->worker_jit_instrument = psp->worker_jit_instrument;
		++(*pos);
	}

	/*
	 * Recurse into children.
	 */

	/* left child */
	walkPlanState(ps->lefttree, save, pspList, pos);

	/* right child */
	walkPlanState(ps->righttree, save, pspList, pos);

	/* initPlans */
	ListCell   *lc;

	foreach(lc, ps->initPlan)
	{
		SubPlanState *sps = (SubPlanState *) lfirst(lc);

		walkPlanState(sps->planstate, save, pspList, pos);
	}

	/* subPlans (execution subplans) */
	foreach(lc, ps->subPlan)
	{
		SubPlanState *sps = (SubPlanState *) lfirst(lc);

		walkPlanState(sps->planstate, save, pspList, pos);
	}
}

/*
 * Generate a textual representation of a plan in the
 * specified format.
 */
char *
YbQpmExplainPlan(QueryDesc *queryDesc, ExplainFormat format)
{
	ExplainState *es;
	StringInfo	es_str;

	es = NewExplainState();
	es_str = es->str;

	es->costs = false;
	es->format = format;
	es->verbose = yb_qpm_configuration.verbose_plans;
	es->rpc = false;
	es->yb_debug = false;
	es->yb_commit = false;

	/*
	 * Save and set the instrumentation pointers to NULL
	 * so we do not inadvertently update any values while
	 * generating the plan text.
	 */
	List	   *pspList = NULL;
	int			pos = 0;

	walkPlanState(queryDesc->planstate, true, &pspList, &pos);

	ExplainBeginOutput(es);
	ExplainOpenGroup("Query", NULL, true, es);
	ExplainPrintPlan(es, queryDesc);
	ExplainCloseGroup("Query", NULL, true, es);
	ExplainEndOutput(es);

	/*
	 * Restore the instrumentation pointers.
	 */
	pos = 0;
	walkPlanState(queryDesc->planstate, false, &pspList, &pos);
	list_free_deep(pspList);

	/* Ignore trailing newline emitted by ExplainPrintPlan */
	if (es_str->len > 0)
		es_str->data[es_str->len - 1] = '\0';

	return es_str->data;
}

/*
 * Insert an entry into QPM.
 */
Datum
yb_pg_stat_plans_insert(PG_FUNCTION_ARGS)
{
	Oid			databaseid = PG_GETARG_OID(0);
	Oid			userid = PG_GETARG_OID(1);
	int64		queryId = PG_GETARG_INT64(2);
	int64		planId = PG_GETARG_INT64(3);
	text	   *hintText = PG_GETARG_TEXT_P(4);
	text	   *planText = PG_GETARG_TEXT_P(5);
	TimestampTz firstUsedTime = PG_GETARG_TIMESTAMP(6);
	TimestampTz lastUsedTime = PG_GETARG_TIMESTAMP(7);
	double		totalTime = PG_GETARG_FLOAT8(8);
	double		estTotalCost = PG_GETARG_FLOAT8(9);

	/* Get the lengths of the string data (excluding the header). */
	int			hintTextLen = VARSIZE_ANY_EXHDR(hintText);
	int			planTextLen = VARSIZE_ANY_EXHDR(planText);

	/* Create a null-terminated copies to use C string functions. */
	char	   *hintBuffer = palloc0(hintTextLen + 1);

	memcpy(hintBuffer, VARDATA(hintText), hintTextLen);

	char	   *planBuffer = palloc0(planTextLen + 1);

	memcpy(planBuffer, VARDATA(planText), planTextLen);

	removeHintDelimiters(hintBuffer);

	/* Insert or update the entry. */
	bool		inserted = qpmInsert(databaseid, userid, (uint64) queryId, (uint64) planId,
									 hintBuffer, planBuffer, NULL,
									 &firstUsedTime, &lastUsedTime, totalTime,
									 estTotalCost);

	pfree(hintBuffer);
	pfree(planBuffer);

	PG_RETURN_BOOL(inserted);	/* true if new entry */
}

/*
 * Retrieve all of the QPM entries.
 */
Datum
yb_pg_stat_plans_get_all_entries(PG_FUNCTION_ARGS)
{
	YbQpmInfoEntry *entries;
	FuncCallContext *funcctx;
	TupleDesc	tupdesc;
	AttInMetadata *attinmeta;

	/* First call setup. */
	if (SRF_IS_FIRSTCALL())
	{
		MemoryContext oldcontext;

		funcctx = SRF_FIRSTCALL_INIT();
		oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

		/*
		 * Allocate and populate the YbQpmInfoEntry array.
		 */
		long		numEntries = qpmNumEntries();

		entries = (YbQpmInfoEntry *) palloc0(numEntries * sizeof(YbQpmInfoEntry));
		(void) qpmGetAllEntries(entries);

		/*
		 * Describe the tuple we are returning.
		 */
		tupdesc = CreateTemplateTupleDesc(13);
		TupleDescInitEntry(tupdesc, (AttrNumber) 1, "dbid", OIDOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 2, "userid", OIDOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 3, "queryid", INT8OID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 4, "planid", INT8OID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 5, "first_used", TIMESTAMPTZOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 6, "last_used", TIMESTAMPTZOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 7, "hints", TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 8, "calls", INT8OID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 9, "avg_exec_time", FLOAT8OID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 10, "max_exec_time", FLOAT8OID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 11, "max_exec_time_params", TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 12, "avg_est_cost", FLOAT8OID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 13, "plan", TEXTOID, -1, 0);

		attinmeta = TupleDescGetAttInMetadata(tupdesc);
		funcctx->attinmeta = attinmeta;

		/* Store max calls, and the entry array as function context. */
		funcctx->max_calls = numEntries;
		funcctx->user_fctx = entries;

		MemoryContextSwitchTo(oldcontext);
	}

	funcctx = SRF_PERCALL_SETUP();
	entries = (YbQpmInfoEntry *) (funcctx->user_fctx);

	if (funcctx->call_cntr < funcctx->max_calls)
	{
		Datum		result;
		HeapTuple	tuple;
		char	  **values;

		/* Get the next entry. */
		YbQpmInfoEntry *currentEntry = &(entries[funcctx->call_cntr]);

		values = (char **) palloc(13 * sizeof(char *));
		values[0] = psprintf("%u", currentEntry->key.databaseId);
		values[1] = psprintf("%u", currentEntry->key.userId);
		values[2] = psprintf(INT64_FORMAT, currentEntry->key.queryId);
		values[3] = psprintf(INT64_FORMAT, currentEntry->key.planId);
		values[4] = DatumGetCString(DirectFunctionCall1(timestamptz_out, currentEntry->firstUsedTime));
		values[5] = DatumGetCString(DirectFunctionCall1(timestamptz_out, currentEntry->lastUsedTime));
		values[6] = currentEntry->hintText;
		values[7] = psprintf("%ld", currentEntry->useCount);

		if (currentEntry->timing.cnt > 0)
		{
			values[8] = psprintf("%.4lf",
								 currentEntry->timing.totalTime / (double) (currentEntry->timing.cnt));
			values[9] = psprintf("%.4lf", currentEntry->timing.worstTotalTime);
			values[11] = psprintf("%.4lf",
								  currentEntry->timing.estTotalCost / (double) (currentEntry->timing.cnt));
		}
		else
		{
			values[8] = psprintf("%.0lf", -1.0);
			values[9] = psprintf("%.0lf", -1.0);
			values[11] = psprintf("%.0lf", -1.0);
		}

		values[10] = currentEntry->worstParamText;
		values[12] = currentEntry->planText;

		/* Form the tuple and return as a Datum. */
		tuple = BuildTupleFromCStrings(funcctx->attinmeta, values);
		result = HeapTupleGetDatum(tuple);

		/* Clean up. */
		if (currentEntry->hintText != NULL)
			pfree(currentEntry->hintText);

		if (currentEntry->planText != NULL)
			pfree(currentEntry->planText);

		if (currentEntry->worstParamText != NULL)
			pfree(currentEntry->worstParamText);

		SRF_RETURN_NEXT(funcctx, result);
	}
	else
	{
		/* We are done so clean up. */
		pfree(entries);
		SRF_RETURN_DONE(funcctx);
	}
}

/*
 * Remove one (or all) entries.
 */
Datum
yb_pg_stat_plans_reset(PG_FUNCTION_ARGS)
{
	long		removedEntryCnt = 0;
	Oid			databaseId;
	Oid			userId;
	int64		queryId;
	int64		planId;

	if (PG_ARGISNULL(0))
		databaseId = 0;
	else
		databaseId = PG_GETARG_OID(0);

	if (PG_ARGISNULL(1))
		userId = 0;
	else
		userId = PG_GETARG_OID(1);

	if (PG_ARGISNULL(2))
		queryId = 0;
	else
		queryId = PG_GETARG_INT64(2);

	if (PG_ARGISNULL(3))
		planId = 0;
	else
		planId = PG_GETARG_INT64(3);

	removedEntryCnt = qpmRemove(databaseId, userId, queryId, planId);

	PG_RETURN_INT64(removedEntryCnt);
}

/*
 * Get the total elapsed time spent in QPM code.
 */
Datum
yb_pg_stat_plans_total_time(PG_FUNCTION_ARGS)
{
	double		totalTime = qpmTotalTime();

	PG_RETURN_FLOAT8(totalTime);
}

/*
 * Get the total number of calls to the QPM code.
 */
Datum
yb_pg_stat_plans_total_calls(PG_FUNCTION_ARGS)
{
	int64		totalCalls = qpmTotalCalls();

	PG_RETURN_INT64(totalCalls);
}

/*
 * Read the QPM dump file and then delete it.
 */
Datum
yb_pg_stat_plans_read_file(PG_FUNCTION_ARGS)
{
	/* Lock the QPM structures. */
	LWLockAcquire(qpmLock, LW_EXCLUSIVE);

	int32		retCode = qpmReadFile();

	if (retCode == 0)
		unlink(QPM_DUMP_FILE);

	LWLockRelease(qpmLock);

	PG_RETURN_INT32(retCode);
}

/*
 * Write the QPM dump file.
 */
Datum
yb_pg_stat_plans_write_file(PG_FUNCTION_ARGS)
{
	/* Lock the QPM structures. */
	LWLockAcquire(qpmLock, LW_EXCLUSIVE);

	int32		retCode = qpmWriteFile();

	LWLockRelease(qpmLock);

	PG_RETURN_INT32(retCode);
}

/*
 * Remove start/stop hint delimiters from the hint string if present.
 */
static void
removeHintDelimiters(char *hintStr)
{
	char	   *start,
			   *end;

	while ((start = strstr(hintStr, "/*+")) != NULL &&
		   (end = strstr(start, "*/")) != NULL)
	{
		end += 2;				/* move past end delimiter */

		/* Shift remaining string left. */
		memmove(start, end, strlen(end) + 1);	/* +1 to move null terminator */
	}
}
