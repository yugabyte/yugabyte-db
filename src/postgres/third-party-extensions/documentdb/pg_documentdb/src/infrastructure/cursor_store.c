/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/infrastructure/cursor_store.c
 *
 * Common declarations for the pg_documentdb cursor store
 * based on temp files on disk.
 *
 * This is based off of a similar set up as tuplestore's FileSet
 * except it ensures that on success the files are not deleted at the
 * end of the transaction. The file names are also based on the cursorId
 * so that it can be accessed by a different backend.
 *
 * There is also a background job that cleans up the cursor files after
 * a certain expiry time limit.
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include <miscadmin.h>
#include <storage/sharedfileset.h>
#include <storage/dsm.h>
#include <storage/buffile.h>
#include <utils/resowner.h>
#include <utils/wait_event.h>
#include <port.h>
#include <utils/timestamp.h>
#include <utils/resowner.h>
#include <port/atomics.h>
#include <storage/lwlock.h>
#include <storage/shmem.h>
#if PG_VERSION_NUM >= 170000
#else
#include <utils/resowner_private.h>
#endif
#include <utils/backend_status.h>

#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include "metadata/metadata_cache.h"
#include "utils/documentdb_errors.h"
#include "io/bson_core.h"
#include "infrastructure/cursor_store.h"

extern char *ApiGucPrefix;
extern bool UseFileBasedPersistedCursors;
extern int MaxAllowedCursorIntermediateFileSizeMB;
extern int DefaultCursorExpiryTimeLimitSeconds;
extern int MaxCursorFileCount;


/*
 * Serialized State that is sent to the client
 * and used to rehydrate the cursor on getMore.
 */
typedef struct SerializedCursorState
{
	/* The name of the file in the cursor directory */
	char cursorFileName[NAMEDATALEN];

	/* The offset into the file */
	uint32_t file_offset;

	/* The total file length (updated during writes) */
	uint32_t file_length;
} SerializedCursorState;

typedef struct CursorFileState
{
	/* The serialized cursor state (see above) */
	SerializedCursorState cursorState;

	/* The file handle to write to*/
	File bufFile;

	/* Whether or not we're in R/W mode or R/O mode */
	bool isReadWrite;

	/* Temporary in-memory buffer for the cursor contents */
	PGAlignedBlock buffer;

	/* Position into the buffer currently written/read */
	int pos;

	/* Number of bytes in buffer that are valid (used in reads) */
	int nbytes;

	uint32_t next_offset;

	/* In read more - whether or not the cursor is complete */
	bool cursorComplete;
} CursorFileState;


/*
 * Shared memory state for the cursor store
 * This is used to track the number of cursors
 * so that we can do resource governance.
 * Currently this is only on the limit of number
 * of cursors that can be created.
 */
typedef struct CursorStoreSharedData
{
	int sharedCursorStoreTrancheId;
	char *sharedCursorStoreTrancheName;

	LWLock sharedCursorStoreLock;

	int32_t currentCursorCount;

	int32_t cleanupCursorFileCount;
	int64_t cleanupTotalCursorSize;
} CursorStoreSharedData;


PG_FUNCTION_INFO_V1(cursor_directory_cleanup);


static void FlushBuffer(CursorFileState *cursorFileState);
static bool FillBuffer(CursorFileState *cursorFileState, char *buffer, int32_t length);

static void DecrementCursorCount(void);
static bool IncrementCursorCount(void);

static void TryCleanUpAndReserveCursor(void);
static int64_t TryDeleteCursorFile(struct dirent *de, int64_t expirtyTimeLimitSeconds);


static CursorStoreSharedData *CursorStoreSharedState = NULL;
static char PendingCursorFile[NAMEDATALEN] = { 0 };

/* Whether or not the cursor_set has been initialized during shared startup */
static bool cursor_set_initialized = false;

static const char *cursor_directory = "pg_documentdb_cursor_files";


/*
 * Runs a cleanup of the cursor directory.
 * Expires cursors that are older than the specified expiry limit.
 * If not set, uses the default GUC value for the expiry.
 * Note:
 * TODO: This should also likely handle scenarios like disk space into
 * the pruning algorithm.
 */
Datum
cursor_directory_cleanup(PG_FUNCTION_ARGS)
{
	if (!cursor_set_initialized || !UseFileBasedPersistedCursors)
	{
		PG_RETURN_VOID();
	}

	int64_t expiryTimeLimitSeconds = 0;
	if (PG_ARGISNULL(0))
	{
		expiryTimeLimitSeconds = DefaultCursorExpiryTimeLimitSeconds;
	}
	else
	{
		expiryTimeLimitSeconds = PG_GETARG_INT64(0);
	}

	DIR *dirdesc;
	struct dirent *de;

	dirdesc = AllocateDir(cursor_directory);
	if (!dirdesc)
	{
		/* Skip if dir doesn't exist appropriate */
		if (errno == ENOENT)
		{
			PG_RETURN_VOID();
		}
	}

	Size totalCursorSize = 0;
	int32_t totalCursorCount = 0;
	while ((de = ReadDir(dirdesc, cursor_directory)) != NULL)
	{
		int64_t deleteRes = TryDeleteCursorFile(de, expiryTimeLimitSeconds);

		if (deleteRes == 0)
		{
			/* Invalid file or concurrent delete */
			continue;
		}
		else if (deleteRes < 0)
		{
			/* Successfully deleted */
			DecrementCursorCount();
		}
		else
		{
			/* Live cursor */
			totalCursorSize += deleteRes;
		}

		totalCursorCount++;
	}

	FreeDir(dirdesc);

	pg_memory_barrier();
	CursorStoreSharedState->cleanupCursorFileCount = totalCursorCount;
	CursorStoreSharedState->cleanupTotalCursorSize = totalCursorSize;

	ereport(DEBUG1, (errmsg("Total size of cursor files: %ld, count %d", totalCursorSize,
							totalCursorCount)));
	PG_RETURN_VOID();
}


/*
 * We set up the shared file set storage for the cursor files.
 * This happens on shared_preload_libraries initialization so we
 * last for the entire life of the server.
 */
void
SetupCursorStorage(void)
{
	if (!process_shared_preload_libraries_in_progress)
	{
		ereport(ERROR, (errmsg(
							"Cursor storage initialization must happen under shared_preload_libraries")));
	}

	if (!rmtree(cursor_directory, true) && errno != ENOENT)
	{
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not remove directory \"%s\": %m", cursor_directory)));
	}

	int result = MakePGDirectory(cursor_directory);
	if (result != 0 && result != EEXIST)
	{
		ereport(ERROR, (errcode_for_file_access(),
						errmsg("could not create directory for cursor files")));
	}

	cursor_set_initialized = true;
}


/*
 * Starts a new query cursor file. This is called
 * on the first page of a query.
 * Registers the file in the cursor directory and
 * returns an opaque structure to track this cursor.
 * TODO: Need to apply storage backpressure for the cursor
 * files.
 */
CursorFileState *
CreateCursorFile(const char *cursorName)
{
	if (!cursor_set_initialized)
	{
		ereport(ERROR, (errmsg(
							"Cursor storage has not been properly initialized. Before using cursors, the server must be restarted")));
	}

	if (!UseFileBasedPersistedCursors)
	{
		ereport(ERROR, (errmsg("File based cursors are not enabled. "
							   "set %s.useFileBasedPersistedCursors to true",
							   ApiGucPrefix)));
	}

	if (strlen(cursorName) + strlen(cursor_directory) >= (NAMEDATALEN - 5))
	{
		ereport(ERROR, (errmsg(
							"Cursor name exceeds the max allowed length.")));
	}

	CursorFileState *fileState = palloc0(sizeof(CursorFileState));
	snprintf(fileState->cursorState.cursorFileName, NAMEDATALEN, "%s/%s",
			 cursor_directory, cursorName);

	File cursorFile = PathNameOpenTemporaryFile(fileState->cursorState.cursorFileName,
												O_RDWR | O_CREAT | O_EXCL | PG_BINARY);
	if (cursorFile < 0)
	{
		if (errno == EEXIST)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_CURSORINUSE),
							errmsg("Cursor already present on server: %s", cursorName)));
		}
		else
		{
			ereport(ERROR,
					(errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
					 errmsg("Failed to open file \"%s\": %m",
							fileState->cursorState.cursorFileName)));
		}
	}

	if (FileSize(cursorFile) != 0)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_CURSORINUSE),
						errmsg("Cursor already present on server: %s", cursorName)));
	}

	/* Register the cursor file for transaction abort */
	strncpy(PendingCursorFile, fileState->cursorState.cursorFileName, NAMEDATALEN);

	/* Ensure we have sufficient space to create cursor files */
	if (!IncrementCursorCount())
	{
		/* We've reached capacity, try to clean up and try again */
		TryCleanUpAndReserveCursor();
	}

	fileState->bufFile = cursorFile;
	fileState->isReadWrite = true;

	return fileState;
}


/*
 * Given a cursor file that is created, writes a given document to that
 * cursor file. The file is written as
 * <length><document> where length is the size of the pgbson including the
 * varlen header.
 * The data is buffered in memory and flushed every BLCKSZ bytes.
 */
void
WriteToCursorFile(CursorFileState *cursorFileState, pgbson *dataBson)
{
	int32_t sizeRemaining = BLCKSZ - cursorFileState->pos;

	/* We don't have enough to write even the length - flush the buffer */
	if (sizeRemaining < 4)
	{
		FlushBuffer(cursorFileState);
	}

	int32_t dataSize = VARSIZE(dataBson);
	char *data = (char *) dataBson;

	/* Write the length to the buffer */
	memcpy(cursorFileState->buffer.data + cursorFileState->pos, &dataSize, 4);
	cursorFileState->pos += 4;

	/* Now write the file into the buffer and then write it out to the file */
	while (dataSize > 0)
	{
		sizeRemaining = BLCKSZ - cursorFileState->pos;
		if (sizeRemaining >= dataSize)
		{
			memcpy(cursorFileState->buffer.data + cursorFileState->pos, data, dataSize);
			cursorFileState->pos += dataSize;
			break;
		}
		else
		{
			memcpy(cursorFileState->buffer.data + cursorFileState->pos, data,
				   sizeRemaining);
			cursorFileState->pos += sizeRemaining;
			data += sizeRemaining;
			dataSize -= sizeRemaining;
			FlushBuffer(cursorFileState);
		}
	}
}


void
GetCurrentCursorCount(int32_t *currentCursorCount, int32_t *measuredCursorCount,
					  int64_t *lastCursorSize)
{
	if (!cursor_set_initialized || !UseFileBasedPersistedCursors)
	{
		*currentCursorCount = 0;
		*measuredCursorCount = 0;
		*lastCursorSize = 0;
	}

	*currentCursorCount = CursorStoreSharedState->currentCursorCount;
	*measuredCursorCount = CursorStoreSharedState->cleanupCursorFileCount;
	*lastCursorSize = CursorStoreSharedState->cleanupTotalCursorSize;
}


void
DeletePendingCursorFiles(void)
{
	if (!UseFileBasedPersistedCursors || !cursor_set_initialized)
	{
		return;
	}

	if (PendingCursorFile[0] == '\0')
	{
		/* No pending cursor file to delete */
		return;
	}

	/* Delete the pending cursor file */
	bool errorOnFailure = false;
	PathNameDeleteTemporaryFile(PendingCursorFile, errorOnFailure);
	PendingCursorFile[0] = '\0';
}


void
DeleteCursorFile(const char *cursorName)
{
	if (!cursor_set_initialized)
	{
		ereport(ERROR, (errmsg("Cursor storage has not been properly initialized")));
	}

	char cursorFileName[MAXPGPATH];
	snprintf(cursorFileName, MAXPGPATH, "%s/%s",
			 cursor_directory, cursorName);

	/* TODO: Should we be ignoring errors here */
	bool errorOnFailure = true;
	bool deleted = PathNameDeleteTemporaryFile(cursorFileName, errorOnFailure);

	/* Decrement the count if the result is 0 */
	if (deleted)
	{
		DecrementCursorCount();
	}
}


/*
 * Given an opaque serialized cursor state as a bytea, creates
 * a CursorFileState object that can be used to read from the
 * cursor file. This is the inverse of GetCursorFile.
 * The file is opened in read-only mode.
 * The file is expected to be in the cursor directory.
 */
CursorFileState *
DeserializeFileState(bytea *cursorFileState)
{
	if (!cursor_set_initialized)
	{
		ereport(ERROR, (errmsg("Cursor storage has not been properly initialized")));
	}

	if (!UseFileBasedPersistedCursors)
	{
		ereport(ERROR, (errmsg("File based cursor is not enabled")));
	}

	CursorFileState *fileState = palloc0(sizeof(CursorFileState));
	fileState->cursorState = *(SerializedCursorState *) VARDATA(cursorFileState);
	fileState->bufFile = PathNameOpenTemporaryFile(fileState->cursorState.cursorFileName,
												   O_RDONLY | PG_BINARY | O_EXCL);
	fileState->isReadWrite = false;
	fileState->next_offset = fileState->cursorState.file_offset;

	if (fileState->bufFile < 0 && errno == ENOENT)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_CURSORNOTFOUND),
						errmsg("Cursor could not be located")));
	}
	else if (fileState->bufFile < 0)
	{
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("Failed to open file \"%s\": %m",
						fileState->cursorState.cursorFileName)));
	}

	/* Register the cursor file for transaction abort */
	strncpy(PendingCursorFile, fileState->cursorState.cursorFileName, NAMEDATALEN);
	return fileState;
}


/*
 * Given a cursor file state, reads the next document from the
 * cursor file. The file is expected to be in the cursor directory.
 * Blocks are pre-buffered in BLCKSZ chunks.
 *
 * Also updates the flush state of the cursor file state based on the
 * prior value read. This ensures that if we return a document, that we
 * only advance the cursor to include that when the next Read is called.
 */
pgbson *
ReadFromCursorFile(CursorFileState *cursorFileState)
{
	/* First step, advance the file stream forward with what was buffered before */
	cursorFileState->cursorState.file_offset = cursorFileState->next_offset;

	int32_t length = 0;
	if (!FillBuffer(cursorFileState, (char *) &length, 4))
	{
		return NULL;
	}

	if ((Size) length > BSON_MAX_SIZE)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
						errmsg("Invalid BSON size in cursor file %d", length)));
	}

	pgbson *bson = palloc(length);
	if (!FillBuffer(cursorFileState, (char *) bson, length))
	{
		pfree(bson);
		return NULL;
	}

	return bson;
}


/*
 * Fills the specified buffer with length bytes from the cursor file.
 * returns false if the end of the file is reached.
 */
static bool
FillBuffer(CursorFileState *cursorFileState, char *buffer, int32_t length)
{
	while (length > 0)
	{
		if (cursorFileState->nbytes == 0)
		{
			int bytesRead = FileRead(cursorFileState->bufFile,
									 cursorFileState->buffer.data, BLCKSZ,
									 cursorFileState->next_offset,
									 WAIT_EVENT_BUFFILE_READ);
			cursorFileState->nbytes += bytesRead;
			cursorFileState->pos = 0;
			if (bytesRead == 0)
			{
				/* There's no more bytes left */
				cursorFileState->cursorComplete = true;
				return false;
			}
		}

		int32_t currentAvailable = cursorFileState->nbytes - cursorFileState->pos;
		if (currentAvailable >= length)
		{
			memcpy(buffer, cursorFileState->buffer.data + cursorFileState->pos, length);
			cursorFileState->pos += length;
			cursorFileState->next_offset += length;
			return true;
		}

		memcpy(buffer, cursorFileState->buffer.data + cursorFileState->pos,
			   currentAvailable);

		cursorFileState->pos = cursorFileState->nbytes = 0;
		cursorFileState->next_offset += currentAvailable;
		buffer += currentAvailable;
		length -= currentAvailable;
	}

	return true;
}


/*
 * Writes whatever bytes have been filed into the buffer to the
 * cursor file. This is called when the buffer is full or
 * when the cursor file is closed.
 */
static void
FlushBuffer(CursorFileState *cursorFileState)
{
	if (cursorFileState->pos > 0)
	{
		int bytesWritten = FileWrite(cursorFileState->bufFile,
									 cursorFileState->buffer.data, cursorFileState->pos,
									 cursorFileState->cursorState.file_offset,
									 WAIT_EVENT_BUFFILE_WRITE);

		if (bytesWritten != cursorFileState->pos)
		{
			ereport(ERROR,
					(errcode_for_file_access(),
					 errmsg("Failed to save data to file")));
		}

		cursorFileState->cursorState.file_offset += cursorFileState->pos;
		cursorFileState->pos = 0;

		Size maxFileSize = ((Size) MaxAllowedCursorIntermediateFileSizeMB) * 1024L * 1024;
		if (cursorFileState->cursorState.file_offset > maxFileSize)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
							errmsg("Cursor file size %u exceeded the limit %d MB",
								   cursorFileState->cursorState.file_offset,
								   MaxAllowedCursorIntermediateFileSizeMB)));
		}
	}
}


/*
 * Closes the cursor file and returns the serialized state
 * of the cursor. This is used to rehydrate the cursor
 * on getMore.
 * returns NULL if the cursor is complete.
 */
bytea *
CursorFileStateClose(CursorFileState *cursorFileState, MemoryContext writerContext)
{
	if (cursorFileState->isReadWrite)
	{
		FlushBuffer(cursorFileState);
		cursorFileState->cursorState.file_length =
			cursorFileState->cursorState.file_offset;
		cursorFileState->cursorState.file_offset = 0;
		pgstat_report_tempfile(cursorFileState->cursorState.file_length);
	}

	PendingCursorFile[0] = '\0';
	FileClose(cursorFileState->bufFile);
	if (cursorFileState->cursorComplete)
	{
		/* Continuation state is null, delete the file */
		bool errorOnFailure = true;
		if (PathNameDeleteTemporaryFile(cursorFileState->cursorState.cursorFileName,
										errorOnFailure))
		{
			DecrementCursorCount();
		}

		return NULL;
	}

	/* Write the state for getMore */
	bytea *serializedSpec = MemoryContextAlloc(writerContext,
											   sizeof(SerializedCursorState) + VARHDRSZ);
	SET_VARSIZE(serializedSpec, sizeof(SerializedCursorState) + VARHDRSZ);
	memcpy(VARDATA(serializedSpec), &cursorFileState->cursorState,
		   sizeof(SerializedCursorState));
	return serializedSpec;
}


Size
FileCursorShmemSize(void)
{
	Size size = 0;
	size = add_size(size, sizeof(CursorStoreSharedData));
	return size;
}


void
InitializeFileCursorShmem(void)
{
	bool found = false;

	/*
	 * make consistent with other extensions running.
	 */
	LWLockAcquire(AddinShmemInitLock, LW_EXCLUSIVE);
	CursorStoreSharedState =
		(CursorStoreSharedData *) ShmemInitStruct(
			"Shared Cursor Store Data",
			sizeof(CursorStoreSharedData),
			&found);

	if (!found)
	{
		CursorStoreSharedState->sharedCursorStoreTrancheId = LWLockNewTrancheId();
		CursorStoreSharedState->sharedCursorStoreTrancheName = "Cursor Store Tranche";
		LWLockRegisterTranche(CursorStoreSharedState->sharedCursorStoreTrancheId,
							  CursorStoreSharedState->sharedCursorStoreTrancheName);

		LWLockInitialize(&CursorStoreSharedState->sharedCursorStoreLock,
						 CursorStoreSharedState->sharedCursorStoreTrancheId);
	}

	LWLockRelease(AddinShmemInitLock);
	Assert(CursorStoreSharedState->sharedCursorStoreTrancheId != 0);
}


static bool
IncrementCursorCount(void)
{
	LWLockAcquire(&CursorStoreSharedState->sharedCursorStoreLock, LW_EXCLUSIVE);

	pg_memory_barrier();
	if (MaxCursorFileCount > 0 &&
		CursorStoreSharedState->currentCursorCount + 1 > MaxCursorFileCount)
	{
		LWLockRelease(&CursorStoreSharedState->sharedCursorStoreLock);
		return false;
	}

	CursorStoreSharedState->currentCursorCount++;
	LWLockRelease(&CursorStoreSharedState->sharedCursorStoreLock);
	return true;
}


static void
DecrementCursorCount(void)
{
	LWLockAcquire(&CursorStoreSharedState->sharedCursorStoreLock, LW_EXCLUSIVE);

	pg_memory_barrier();
	CursorStoreSharedState->currentCursorCount--;
	if (CursorStoreSharedState->currentCursorCount < 0)
	{
		CursorStoreSharedState->currentCursorCount = 0;
	}

	LWLockRelease(&CursorStoreSharedState->sharedCursorStoreLock);
}


static void
TryCleanUpAndReserveCursor(void)
{
	DIR *dirdesc;
	struct dirent *de;

	dirdesc = AllocateDir(cursor_directory);
	if (!dirdesc)
	{
		/* Skip if dir doesn't exist appropriate */
		if (errno == ENOENT)
		{
			ereport(ERROR, (errmsg("Specified cursor directory could not be found")));
		}
	}

	/* Clean up expired cursors */
	bool deleted = false;
	while ((de = ReadDir(dirdesc, cursor_directory)) != NULL)
	{
		int64_t deleteRes = TryDeleteCursorFile(de, DefaultCursorExpiryTimeLimitSeconds);
		if (deleteRes < 0)
		{
			/* Successfully deleted an expired cursor */
			deleted = true;
			break;
		}
	}

	FreeDir(dirdesc);

	if (deleted)
	{
		return;
	}

	/* Could not delete any cursors - all are valid - fail */
	ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_CURSORINUSE),
					errmsg(
						"Could not reserve a cursor - all cursors are in use and not expired")));
}


static int64_t
TryDeleteCursorFile(struct dirent *de, int64_t expiryTimeLimitSeconds)
{
	char path[MAXPGPATH * 2];
	struct stat attrib;

	/* Skip hidden files */
	if (de->d_name[0] == '.')
	{
		return 0;
	}

	/* Get the file info */
	snprintf(path, sizeof(path), "%s/%s", cursor_directory, de->d_name);
	if (stat(path, &attrib) < 0)
	{
		/* Ignore concurrently-deleted files, else complain */
		if (errno == ENOENT)
		{
			return 0;
		}

		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not stat file \"%s\": %m", path)));
	}

	/* Ignore anything but regular files */
	if (!S_ISREG(attrib.st_mode))
	{
		return 0;
	}

	TimestampTz lastModified = time_t_to_timestamptz(attrib.st_mtime);
	TimestampTz currentTime = GetCurrentTimestamp();
	if (TimestampDifferenceExceeds(lastModified, currentTime, expiryTimeLimitSeconds *
								   1000))
	{
		ereport(LOG, (errmsg("Deleting expired cursor file %s", path)));
		bool errorOnFailure = false;
		bool deleted = PathNameDeleteTemporaryFile(path, errorOnFailure);
		if (deleted)
		{
			return -attrib.st_size;
		}
		else
		{
			return attrib.st_size;
		}
	}
	else
	{
		return attrib.st_size;
	}
}
