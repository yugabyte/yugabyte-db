/*-------------------------------------------------------------------------
 *
 * buffile.c
 *	  Management of large buffered temporary files.
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * IDENTIFICATION
 *	  src/backend/storage/file/buffile.c
 *
 * NOTES:
 *
 * BufFiles provide a very incomplete emulation of stdio atop virtual Files
 * (as managed by fd.c).  Currently, we only support the buffered-I/O
 * aspect of stdio: a read or write of the low-level File occurs only
 * when the buffer is filled or emptied.  This is an even bigger win
 * for virtual Files than for ordinary kernel files, since reducing the
 * frequency with which a virtual File is touched reduces "thrashing"
 * of opening/closing file descriptors.
 *
 * Note that BufFile structs are allocated with palloc(), and therefore
 * will go away automatically at query/transaction end.  Since the underlying
 * virtual Files are made with OpenTemporaryFile, all resources for
 * the file are certain to be cleaned up even if processing is aborted
 * by ereport(ERROR).  The data structures required are made in the
 * palloc context that was current when the BufFile was created, and
 * any external resources such as temp files are owned by the ResourceOwner
 * that was current at that time.
 *
 * BufFile also supports temporary files that exceed the OS file size limit
 * (by opening multiple fd.c temporary files).  This is an essential feature
 * for sorts and hashjoins on large amounts of data.
 *
 * BufFile supports temporary files that can be shared with other backends, as
 * infrastructure for parallel execution.  Such files need to be created as a
 * member of a SharedFileSet that all participants are attached to.
 *
 * BufFile also supports temporary files that can be used by the single backend
 * when the corresponding files need to be survived across the transaction and
 * need to be opened and closed multiple times.  Such files need to be created
 * as a member of a FileSet.
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "commands/tablespace.h"
#include "executor/instrument.h"
#include "miscadmin.h"
#include "pgstat.h"
#include "storage/buf_internals.h"
#include "storage/buffile.h"
#include "storage/fd.h"
#include "utils/resowner.h"

/*
 * We break BufFiles into gigabyte-sized segments, regardless of RELSEG_SIZE.
 * The reason is that we'd like large BufFiles to be spread across multiple
 * tablespaces when available.
 */
#define MAX_PHYSICAL_FILESIZE	0x40000000
#define BUFFILE_SEG_SIZE		(MAX_PHYSICAL_FILESIZE / BLCKSZ)

/*
 * This data structure represents a buffered file that consists of one or
 * more physical files (each accessed through a virtual file descriptor
 * managed by fd.c).
 */
struct BufFile
{
	int			numFiles;		/* number of physical files in set */
	/* all files except the last have length exactly MAX_PHYSICAL_FILESIZE */
	File	   *files;			/* palloc'd array with numFiles entries */

	bool		isInterXact;	/* keep open over transactions? */
	bool		dirty;			/* does buffer need to be written? */
	bool		readOnly;		/* has the file been set to read only? */

	FileSet    *fileset;		/* space for fileset based segment files */
	const char *name;			/* name of fileset based BufFile */

	/*
	 * resowner is the ResourceOwner to use for underlying temp files.  (We
	 * don't need to remember the memory context we're using explicitly,
	 * because after creation we only repalloc our arrays larger.)
	 */
	ResourceOwner resowner;

	/*
	 * "current pos" is position of start of buffer within the logical file.
	 * Position as seen by user of BufFile is (curFile, curOffset + pos).
	 */
	int			curFile;		/* file index (0..n) part of current pos */
	off_t		curOffset;		/* offset part of current pos */
	int			pos;			/* next read/write position in buffer */
	int			nbytes;			/* total # of valid bytes in buffer */
	PGAlignedBlock buffer;
};

static BufFile *makeBufFileCommon(int nfiles);
static BufFile *makeBufFile(File firstfile);
static void extendBufFile(BufFile *file);
static void BufFileLoadBuffer(BufFile *file);
static void BufFileDumpBuffer(BufFile *file);
static void BufFileFlush(BufFile *file);
static File MakeNewFileSetSegment(BufFile *file, int segment);

/*
 * Create BufFile and perform the common initialization.
 */
static BufFile *
makeBufFileCommon(int nfiles)
{
	BufFile    *file = (BufFile *) palloc(sizeof(BufFile));

	file->numFiles = nfiles;
	file->isInterXact = false;
	file->dirty = false;
	file->resowner = CurrentResourceOwner;
	file->curFile = 0;
	file->curOffset = 0L;
	file->pos = 0;
	file->nbytes = 0;

	return file;
}

/*
 * Create a BufFile given the first underlying physical file.
 * NOTE: caller must set isInterXact if appropriate.
 */
static BufFile *
makeBufFile(File firstfile)
{
	BufFile    *file = makeBufFileCommon(1);

	file->files = (File *) palloc(sizeof(File));
	file->files[0] = firstfile;
	file->readOnly = false;
	file->fileset = NULL;
	file->name = NULL;

	return file;
}

/*
 * Add another component temp file.
 */
static void
extendBufFile(BufFile *file)
{
	File		pfile;
	ResourceOwner oldowner;

	/* Be sure to associate the file with the BufFile's resource owner */
	oldowner = CurrentResourceOwner;
	CurrentResourceOwner = file->resowner;

	if (file->fileset == NULL)
		pfile = OpenTemporaryFile(file->isInterXact);
	else
		pfile = MakeNewFileSetSegment(file, file->numFiles);

	Assert(pfile >= 0);

	CurrentResourceOwner = oldowner;

	file->files = (File *) repalloc(file->files,
									(file->numFiles + 1) * sizeof(File));
	file->files[file->numFiles] = pfile;
	file->numFiles++;
}

/*
 * Create a BufFile for a new temporary file (which will expand to become
 * multiple temporary files if more than MAX_PHYSICAL_FILESIZE bytes are
 * written to it).
 *
 * If interXact is true, the temp file will not be automatically deleted
 * at end of transaction.
 *
 * Note: if interXact is true, the caller had better be calling us in a
 * memory context, and with a resource owner, that will survive across
 * transaction boundaries.
 */
BufFile *
BufFileCreateTemp(bool interXact)
{
	BufFile    *file;
	File		pfile;

	/*
	 * Ensure that temp tablespaces are set up for OpenTemporaryFile to use.
	 * Possibly the caller will have done this already, but it seems useful to
	 * double-check here.  Failure to do this at all would result in the temp
	 * files always getting placed in the default tablespace, which is a
	 * pretty hard-to-detect bug.  Callers may prefer to do it earlier if they
	 * want to be sure that any required catalog access is done in some other
	 * resource context.
	 */
	PrepareTempTablespaces();

	pfile = OpenTemporaryFile(interXact);
	Assert(pfile >= 0);

	file = makeBufFile(pfile);
	file->isInterXact = interXact;

	return file;
}

/*
 * Build the name for a given segment of a given BufFile.
 */
static void
FileSetSegmentName(char *name, const char *buffile_name, int segment)
{
	snprintf(name, MAXPGPATH, "%s.%d", buffile_name, segment);
}

/*
 * Create a new segment file backing a fileset based BufFile.
 */
static File
MakeNewFileSetSegment(BufFile *buffile, int segment)
{
	char		name[MAXPGPATH];
	File		file;

	/*
	 * It is possible that there are files left over from before a crash
	 * restart with the same name.  In order for BufFileOpenFileSet() not to
	 * get confused about how many segments there are, we'll unlink the next
	 * segment number if it already exists.
	 */
	FileSetSegmentName(name, buffile->name, segment + 1);
	FileSetDelete(buffile->fileset, name, true);

	/* Create the new segment. */
	FileSetSegmentName(name, buffile->name, segment);
	file = FileSetCreate(buffile->fileset, name);

	/* FileSetCreate would've errored out */
	Assert(file > 0);

	return file;
}

/*
 * Create a BufFile that can be discovered and opened read-only by other
 * backends that are attached to the same SharedFileSet using the same name.
 *
 * The naming scheme for fileset based BufFiles is left up to the calling code.
 * The name will appear as part of one or more filenames on disk, and might
 * provide clues to administrators about which subsystem is generating
 * temporary file data.  Since each SharedFileSet object is backed by one or
 * more uniquely named temporary directory, names don't conflict with
 * unrelated SharedFileSet objects.
 */
BufFile *
BufFileCreateFileSet(FileSet *fileset, const char *name)
{
	BufFile    *file;

	file = makeBufFileCommon(1);
	file->fileset = fileset;
	file->name = pstrdup(name);
	file->files = (File *) palloc(sizeof(File));
	file->files[0] = MakeNewFileSetSegment(file, 0);
	file->readOnly = false;

	return file;
}

/*
 * Open a file that was previously created in another backend (or this one)
 * with BufFileCreateFileSet in the same FileSet using the same name.
 * The backend that created the file must have called BufFileClose() or
 * BufFileExportFileSet() to make sure that it is ready to be opened by other
 * backends and render it read-only.  If missing_ok is true, which indicates
 * that missing files can be safely ignored, then return NULL if the BufFile
 * with the given name is not found, otherwise, throw an error.
 */
BufFile *
BufFileOpenFileSet(FileSet *fileset, const char *name, int mode,
				   bool missing_ok)
{
	BufFile    *file;
	char		segment_name[MAXPGPATH];
	Size		capacity = 16;
	File	   *files;
	int			nfiles = 0;

	files = palloc(sizeof(File) * capacity);

	/*
	 * We don't know how many segments there are, so we'll probe the
	 * filesystem to find out.
	 */
	for (;;)
	{
		/* See if we need to expand our file segment array. */
		if (nfiles + 1 > capacity)
		{
			capacity *= 2;
			files = repalloc(files, sizeof(File) * capacity);
		}
		/* Try to load a segment. */
		FileSetSegmentName(segment_name, name, nfiles);
		files[nfiles] = FileSetOpen(fileset, segment_name, mode);
		if (files[nfiles] <= 0)
			break;
		++nfiles;

		CHECK_FOR_INTERRUPTS();
	}

	/*
	 * If we didn't find any files at all, then no BufFile exists with this
	 * name.
	 */
	if (nfiles == 0)
	{
		/* free the memory */
		pfree(files);

		if (missing_ok)
			return NULL;

		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not open temporary file \"%s\" from BufFile \"%s\": %m",
						segment_name, name)));
	}

	file = makeBufFileCommon(nfiles);
	file->files = files;
	file->readOnly = (mode == O_RDONLY);
	file->fileset = fileset;
	file->name = pstrdup(name);

	return file;
}

/*
 * Delete a BufFile that was created by BufFileCreateFileSet in the given
 * FileSet using the given name.
 *
 * It is not necessary to delete files explicitly with this function.  It is
 * provided only as a way to delete files proactively, rather than waiting for
 * the FileSet to be cleaned up.
 *
 * Only one backend should attempt to delete a given name, and should know
 * that it exists and has been exported or closed otherwise missing_ok should
 * be passed true.
 */
void
BufFileDeleteFileSet(FileSet *fileset, const char *name, bool missing_ok)
{
	char		segment_name[MAXPGPATH];
	int			segment = 0;
	bool		found = false;

	/*
	 * We don't know how many segments the file has.  We'll keep deleting
	 * until we run out.  If we don't manage to find even an initial segment,
	 * raise an error.
	 */
	for (;;)
	{
		FileSetSegmentName(segment_name, name, segment);
		if (!FileSetDelete(fileset, segment_name, true))
			break;
		found = true;
		++segment;

		CHECK_FOR_INTERRUPTS();
	}

	if (!found && !missing_ok)
		elog(ERROR, "could not delete unknown BufFile \"%s\"", name);
}

/*
 * BufFileExportFileSet --- flush and make read-only, in preparation for sharing.
 */
void
BufFileExportFileSet(BufFile *file)
{
	/* Must be a file belonging to a FileSet. */
	Assert(file->fileset != NULL);

	/* It's probably a bug if someone calls this twice. */
	Assert(!file->readOnly);

	BufFileFlush(file);
	file->readOnly = true;
}

/*
 * Close a BufFile
 *
 * Like fclose(), this also implicitly FileCloses the underlying File.
 */
void
BufFileClose(BufFile *file)
{
	int			i;

	/* flush any unwritten data */
	BufFileFlush(file);
	/* close and delete the underlying file(s) */
	for (i = 0; i < file->numFiles; i++)
		FileClose(file->files[i]);
	/* release the buffer space */
	pfree(file->files);
	pfree(file);
}

/*
 * BufFileLoadBuffer
 *
 * Load some data into buffer, if possible, starting from curOffset.
 * At call, must have dirty = false, pos and nbytes = 0.
 * On exit, nbytes is number of bytes loaded.
 */
static void
BufFileLoadBuffer(BufFile *file)
{
	File		thisfile;
	instr_time	io_start;
	instr_time	io_time;

	/*
	 * Advance to next component file if necessary and possible.
	 */
	if (file->curOffset >= MAX_PHYSICAL_FILESIZE &&
		file->curFile + 1 < file->numFiles)
	{
		file->curFile++;
		file->curOffset = 0L;
	}

	thisfile = file->files[file->curFile];

	if (track_io_timing)
		INSTR_TIME_SET_CURRENT(io_start);

	/*
	 * Read whatever we can get, up to a full bufferload.
	 */
	file->nbytes = FileRead(thisfile,
							file->buffer.data,
							sizeof(file->buffer),
							file->curOffset,
							WAIT_EVENT_BUFFILE_READ);
	if (file->nbytes < 0)
	{
		file->nbytes = 0;
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not read file \"%s\": %m",
						FilePathName(thisfile))));
	}

	if (track_io_timing)
	{
		INSTR_TIME_SET_CURRENT(io_time);
		INSTR_TIME_SUBTRACT(io_time, io_start);
		INSTR_TIME_ADD(pgBufferUsage.temp_blk_read_time, io_time);
	}

	/* we choose not to advance curOffset here */

	if (file->nbytes > 0)
		pgBufferUsage.temp_blks_read++;
}

/*
 * BufFileDumpBuffer
 *
 * Dump buffer contents starting at curOffset.
 * At call, should have dirty = true, nbytes > 0.
 * On exit, dirty is cleared if successful write, and curOffset is advanced.
 */
static void
BufFileDumpBuffer(BufFile *file)
{
	int			wpos = 0;
	int			bytestowrite;
	File		thisfile;

	/*
	 * Unlike BufFileLoadBuffer, we must dump the whole buffer even if it
	 * crosses a component-file boundary; so we need a loop.
	 */
	while (wpos < file->nbytes)
	{
		off_t		availbytes;
		instr_time	io_start;
		instr_time	io_time;

		/*
		 * Advance to next component file if necessary and possible.
		 */
		if (file->curOffset >= MAX_PHYSICAL_FILESIZE)
		{
			while (file->curFile + 1 >= file->numFiles)
				extendBufFile(file);
			file->curFile++;
			file->curOffset = 0L;
		}

		/*
		 * Determine how much we need to write into this file.
		 */
		bytestowrite = file->nbytes - wpos;
		availbytes = MAX_PHYSICAL_FILESIZE - file->curOffset;

		if ((off_t) bytestowrite > availbytes)
			bytestowrite = (int) availbytes;

		thisfile = file->files[file->curFile];

		if (track_io_timing)
			INSTR_TIME_SET_CURRENT(io_start);

		bytestowrite = FileWrite(thisfile,
								 file->buffer.data + wpos,
								 bytestowrite,
								 file->curOffset,
								 WAIT_EVENT_BUFFILE_WRITE);
		if (bytestowrite <= 0)
			ereport(ERROR,
					(errcode_for_file_access(),
					 errmsg("could not write to file \"%s\": %m",
							FilePathName(thisfile))));

		if (track_io_timing)
		{
			INSTR_TIME_SET_CURRENT(io_time);
			INSTR_TIME_SUBTRACT(io_time, io_start);
			INSTR_TIME_ADD(pgBufferUsage.temp_blk_write_time, io_time);
		}

		file->curOffset += bytestowrite;
		wpos += bytestowrite;

		pgBufferUsage.temp_blks_written++;
	}
	file->dirty = false;

	/*
	 * At this point, curOffset has been advanced to the end of the buffer,
	 * ie, its original value + nbytes.  We need to make it point to the
	 * logical file position, ie, original value + pos, in case that is less
	 * (as could happen due to a small backwards seek in a dirty buffer!)
	 */
	file->curOffset -= (file->nbytes - file->pos);
	if (file->curOffset < 0)	/* handle possible segment crossing */
	{
		file->curFile--;
		Assert(file->curFile >= 0);
		file->curOffset += MAX_PHYSICAL_FILESIZE;
	}

	/*
	 * Now we can set the buffer empty without changing the logical position
	 */
	file->pos = 0;
	file->nbytes = 0;
}

/*
 * BufFileRead
 *
 * Like fread() except we assume 1-byte element size and report I/O errors via
 * ereport().
 */
size_t
BufFileRead(BufFile *file, void *ptr, size_t size)
{
	size_t		nread = 0;
	size_t		nthistime;

	BufFileFlush(file);

	while (size > 0)
	{
		if (file->pos >= file->nbytes)
		{
			/* Try to load more data into buffer. */
			file->curOffset += file->pos;
			file->pos = 0;
			file->nbytes = 0;
			BufFileLoadBuffer(file);
			if (file->nbytes <= 0)
				break;			/* no more data available */
		}

		nthistime = file->nbytes - file->pos;
		if (nthistime > size)
			nthistime = size;
		Assert(nthistime > 0);

		memcpy(ptr, file->buffer.data + file->pos, nthistime);

		file->pos += nthistime;
		ptr = (void *) ((char *) ptr + nthistime);
		size -= nthistime;
		nread += nthistime;
	}

	return nread;
}

/*
 * BufFileWrite
 *
 * Like fwrite() except we assume 1-byte element size and report errors via
 * ereport().
 */
void
BufFileWrite(BufFile *file, void *ptr, size_t size)
{
	size_t		nthistime;

	Assert(!file->readOnly);

	while (size > 0)
	{
		if (file->pos >= BLCKSZ)
		{
			/* Buffer full, dump it out */
			if (file->dirty)
				BufFileDumpBuffer(file);
			else
			{
				/* Hmm, went directly from reading to writing? */
				file->curOffset += file->pos;
				file->pos = 0;
				file->nbytes = 0;
			}
		}

		nthistime = BLCKSZ - file->pos;
		if (nthistime > size)
			nthistime = size;
		Assert(nthistime > 0);

		memcpy(file->buffer.data + file->pos, ptr, nthistime);

		file->dirty = true;
		file->pos += nthistime;
		if (file->nbytes < file->pos)
			file->nbytes = file->pos;
		ptr = (void *) ((char *) ptr + nthistime);
		size -= nthistime;
	}
}

/*
 * BufFileFlush
 *
 * Like fflush(), except that I/O errors are reported with ereport().
 */
static void
BufFileFlush(BufFile *file)
{
	if (file->dirty)
		BufFileDumpBuffer(file);

	Assert(!file->dirty);
}

/*
 * BufFileSeek
 *
 * Like fseek(), except that target position needs two values in order to
 * work when logical filesize exceeds maximum value representable by off_t.
 * We do not support relative seeks across more than that, however.
 * I/O errors are reported by ereport().
 *
 * Result is 0 if OK, EOF if not.  Logical position is not moved if an
 * impossible seek is attempted.
 */
int
BufFileSeek(BufFile *file, int fileno, off_t offset, int whence)
{
	int			newFile;
	off_t		newOffset;

	switch (whence)
	{
		case SEEK_SET:
			if (fileno < 0)
				return EOF;
			newFile = fileno;
			newOffset = offset;
			break;
		case SEEK_CUR:

			/*
			 * Relative seek considers only the signed offset, ignoring
			 * fileno. Note that large offsets (> 1 GB) risk overflow in this
			 * add, unless we have 64-bit off_t.
			 */
			newFile = file->curFile;
			newOffset = (file->curOffset + file->pos) + offset;
			break;
		case SEEK_END:

			/*
			 * The file size of the last file gives us the end offset of that
			 * file.
			 */
			newFile = file->numFiles - 1;
			newOffset = FileSize(file->files[file->numFiles - 1]);
			if (newOffset < 0)
				ereport(ERROR,
						(errcode_for_file_access(),
						 errmsg("could not determine size of temporary file \"%s\" from BufFile \"%s\": %m",
								FilePathName(file->files[file->numFiles - 1]),
								file->name)));
			break;
		default:
			elog(ERROR, "invalid whence: %d", whence);
			return EOF;
	}
	while (newOffset < 0)
	{
		if (--newFile < 0)
			return EOF;
		newOffset += MAX_PHYSICAL_FILESIZE;
	}
	if (newFile == file->curFile &&
		newOffset >= file->curOffset &&
		newOffset <= file->curOffset + file->nbytes)
	{
		/*
		 * Seek is to a point within existing buffer; we can just adjust
		 * pos-within-buffer, without flushing buffer.  Note this is OK
		 * whether reading or writing, but buffer remains dirty if we were
		 * writing.
		 */
		file->pos = (int) (newOffset - file->curOffset);
		return 0;
	}
	/* Otherwise, must reposition buffer, so flush any dirty data */
	BufFileFlush(file);

	/*
	 * At this point and no sooner, check for seek past last segment. The
	 * above flush could have created a new segment, so checking sooner would
	 * not work (at least not with this code).
	 */

	/* convert seek to "start of next seg" to "end of last seg" */
	if (newFile == file->numFiles && newOffset == 0)
	{
		newFile--;
		newOffset = MAX_PHYSICAL_FILESIZE;
	}
	while (newOffset > MAX_PHYSICAL_FILESIZE)
	{
		if (++newFile >= file->numFiles)
			return EOF;
		newOffset -= MAX_PHYSICAL_FILESIZE;
	}
	if (newFile >= file->numFiles)
		return EOF;
	/* Seek is OK! */
	file->curFile = newFile;
	file->curOffset = newOffset;
	file->pos = 0;
	file->nbytes = 0;
	return 0;
}

void
BufFileTell(BufFile *file, int *fileno, off_t *offset)
{
	*fileno = file->curFile;
	*offset = file->curOffset + file->pos;
}

/*
 * BufFileSeekBlock --- block-oriented seek
 *
 * Performs absolute seek to the start of the n'th BLCKSZ-sized block of
 * the file.  Note that users of this interface will fail if their files
 * exceed BLCKSZ * LONG_MAX bytes, but that is quite a lot; we don't work
 * with tables bigger than that, either...
 *
 * Result is 0 if OK, EOF if not.  Logical position is not moved if an
 * impossible seek is attempted.
 */
int
BufFileSeekBlock(BufFile *file, long blknum)
{
	return BufFileSeek(file,
					   (int) (blknum / BUFFILE_SEG_SIZE),
					   (off_t) (blknum % BUFFILE_SEG_SIZE) * BLCKSZ,
					   SEEK_SET);
}

#ifdef NOT_USED
/*
 * BufFileTellBlock --- block-oriented tell
 *
 * Any fractional part of a block in the current seek position is ignored.
 */
long
BufFileTellBlock(BufFile *file)
{
	long		blknum;

	blknum = (file->curOffset + file->pos) / BLCKSZ;
	blknum += file->curFile * BUFFILE_SEG_SIZE;
	return blknum;
}

#endif

/*
 * Return the current fileset based BufFile size.
 *
 * Counts any holes left behind by BufFileAppend as part of the size.
 * ereport()s on failure.
 */
int64
BufFileSize(BufFile *file)
{
	int64		lastFileSize;

	Assert(file->fileset != NULL);

	/* Get the size of the last physical file. */
	lastFileSize = FileSize(file->files[file->numFiles - 1]);
	if (lastFileSize < 0)
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not determine size of temporary file \"%s\" from BufFile \"%s\": %m",
						FilePathName(file->files[file->numFiles - 1]),
						file->name)));

	return ((file->numFiles - 1) * (int64) MAX_PHYSICAL_FILESIZE) +
		lastFileSize;
}

/*
 * Append the contents of source file (managed within fileset) to
 * end of target file (managed within same fileset).
 *
 * Note that operation subsumes ownership of underlying resources from
 * "source".  Caller should never call BufFileClose against source having
 * called here first.  Resource owners for source and target must match,
 * too.
 *
 * This operation works by manipulating lists of segment files, so the
 * file content is always appended at a MAX_PHYSICAL_FILESIZE-aligned
 * boundary, typically creating empty holes before the boundary.  These
 * areas do not contain any interesting data, and cannot be read from by
 * caller.
 *
 * Returns the block number within target where the contents of source
 * begins.  Caller should apply this as an offset when working off block
 * positions that are in terms of the original BufFile space.
 */
long
BufFileAppend(BufFile *target, BufFile *source)
{
	long		startBlock = target->numFiles * BUFFILE_SEG_SIZE;
	int			newNumFiles = target->numFiles + source->numFiles;
	int			i;

	Assert(target->fileset != NULL);
	Assert(source->readOnly);
	Assert(!source->dirty);
	Assert(source->fileset != NULL);

	if (target->resowner != source->resowner)
		elog(ERROR, "could not append BufFile with non-matching resource owner");

	target->files = (File *)
		repalloc(target->files, sizeof(File) * newNumFiles);
	for (i = target->numFiles; i < newNumFiles; i++)
		target->files[i] = source->files[i - target->numFiles];
	target->numFiles = newNumFiles;

	return startBlock;
}

/*
 * Truncate a BufFile created by BufFileCreateFileSet up to the given fileno
 * and the offset.
 */
void
BufFileTruncateFileSet(BufFile *file, int fileno, off_t offset)
{
	int			numFiles = file->numFiles;
	int			newFile = fileno;
	off_t		newOffset = file->curOffset;
	char		segment_name[MAXPGPATH];
	int			i;

	/*
	 * Loop over all the files up to the given fileno and remove the files
	 * that are greater than the fileno and truncate the given file up to the
	 * offset. Note that we also remove the given fileno if the offset is 0
	 * provided it is not the first file in which we truncate it.
	 */
	for (i = file->numFiles - 1; i >= fileno; i--)
	{
		if ((i != fileno || offset == 0) && i != 0)
		{
			FileSetSegmentName(segment_name, file->name, i);
			FileClose(file->files[i]);
			if (!FileSetDelete(file->fileset, segment_name, true))
				ereport(ERROR,
						(errcode_for_file_access(),
						 errmsg("could not delete fileset \"%s\": %m",
								segment_name)));
			numFiles--;
			newOffset = MAX_PHYSICAL_FILESIZE;

			/*
			 * This is required to indicate that we have deleted the given
			 * fileno.
			 */
			if (i == fileno)
				newFile--;
		}
		else
		{
			if (FileTruncate(file->files[i], offset,
							 WAIT_EVENT_BUFFILE_TRUNCATE) < 0)
				ereport(ERROR,
						(errcode_for_file_access(),
						 errmsg("could not truncate file \"%s\": %m",
								FilePathName(file->files[i]))));
			newOffset = offset;
		}
	}

	file->numFiles = numFiles;

	/*
	 * If the truncate point is within existing buffer then we can just adjust
	 * pos within buffer.
	 */
	if (newFile == file->curFile &&
		newOffset >= file->curOffset &&
		newOffset <= file->curOffset + file->nbytes)
	{
		/* No need to reset the current pos if the new pos is greater. */
		if (newOffset <= file->curOffset + file->pos)
			file->pos = (int) (newOffset - file->curOffset);

		/* Adjust the nbytes for the current buffer. */
		file->nbytes = (int) (newOffset - file->curOffset);
	}
	else if (newFile == file->curFile &&
			 newOffset < file->curOffset)
	{
		/*
		 * The truncate point is within the existing file but prior to the
		 * current position, so we can forget the current buffer and reset the
		 * current position.
		 */
		file->curOffset = newOffset;
		file->pos = 0;
		file->nbytes = 0;
	}
	else if (newFile < file->curFile)
	{
		/*
		 * The truncate point is prior to the current file, so need to reset
		 * the current position accordingly.
		 */
		file->curFile = newFile;
		file->curOffset = newOffset;
		file->pos = 0;
		file->nbytes = 0;
	}
	/* Nothing to do, if the truncate point is beyond current file. */
}
