#include "postgres.h"
#include "fmgr.h"
#include "stdio.h"
#include "utils/memutils.h"


#define INVALID_OPERATION		"UTL_FILE_INVALID_OPERATION"
#define WRITE_ERROR			"UTL_FILE_WRITE_ERROR"
#define INVALID_FILEHANDLE		"UTL_FILE_INVALID_FILEHANDLE"
#define INVALID_MAXLINESIZE		"UTL_FILE_INVALID_MAXLINESIZE"
#define INVALID_MODE			"UTL_FILE_INVALID_MODE"
#define	INVALID_PATH			"UTL_FILE_INVALID_PATH"
#define VALUE_ERROR			"UTL_FILE_VALUE_ERROR"


Datum utl_file_fopen(PG_FUNCTION_ARGS);
Datum utl_file_get_line(PG_FUNCTION_ARGS);
Datum utl_file___put(PG_FUNCTION_ARGS);
Datum utl_file_putf(PG_FUNCTION_ARGS);
Datum utl_file_fflush(PG_FUNCTION_ARGS);
Datum utl_file_fclose(PG_FUNCTION_ARGS);
Datum utl_file_fclose_all(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(utl_file_fopen);
PG_FUNCTION_INFO_V1(utl_file_get_line);
PG_FUNCTION_INFO_V1(utl_file___put);
PG_FUNCTION_INFO_V1(utl_file_putf);
PG_FUNCTION_INFO_V1(utl_file_fflush);
PG_FUNCTION_INFO_V1(utl_file_fclose);
PG_FUNCTION_INFO_V1(utl_file_fclose_all);


#define PARAMETER_ERROR(detail) \
	ereport(ERROR, \
		(errcode(ERRCODE_INVALID_PARAMETER_VALUE), \
		 errmsg("invalid parameter"), \
		 errdetail(detail)))

#define CUSTOM_EXCEPTION(msg, detail) \
	ereport(ERROR, \
		(errcode(ERRCODE_RAISE_EXCEPTION), \
		 errmsg(msg), \
		 errdetail(detail)))
	
#define INVALID_FILEHANDLE_EXCEPTION()	CUSTOM_EXCEPTION(INVALID_FILEHANDLE, "Used file handle isn't valid.")

#define CHECK_FILE_HANDLE() \
	if (PG_ARGISNULL(0)) \
		CUSTOM_EXCEPTION(INVALID_FILEHANDLE, "Used file handle isn't valid.")

#define NON_EMPTY_TEXT(dat) \
	if (VARSIZE(dat) - VARHDRSZ == 0) \
		ereport(ERROR, \
			(errcode(ERRCODE_INVALID_PARAMETER_VALUE), \
			 errmsg("invalid parameter"), \
			 errdetail("Empty string isn't allowed.")));

#define NOT_NULL_ARG(n) \
	if (PG_ARGISNULL(n)) \
		ereport(ERROR, \
			(errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED), \
			 errmsg("null value not allowed"), \
			 errhint("%dth argument is NULL.", n)));

#define GET_FILESTREAM()	get_stream(PG_GETARG_INT32(0), NULL)

#define MAX_LINESIZE		32767

typedef struct FileSlot
{
	FILE *file;
	int	max_linesize;
} FileSlot;

#define MAX_SLOTS		10

static FileSlot slots[MAX_SLOTS] = {{NULL, 0},{NULL, 0},{NULL, 0},{NULL, 0},{NULL, 0},
			    	    {NULL, 0},{NULL, 0},{NULL, 0},{NULL, 0},{NULL, 0}};


/*
 * get_descriptor(FILE *file) find any free slot for FILE pointer. 
 * If isn't realloc array slots and add 32 new free slots.
 *
 */
static int
get_descriptor(FILE *file, int max_linesize)
{
	int i;
	
	for (i = 0; i < MAX_SLOTS; i++)
	{
		if (!slots[i].file)
		{
			slots[i].file = file;
			slots[i].max_linesize = max_linesize;
			return i;
		}
	}

	ereport(ERROR,
		    (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
		     errmsg("program limit exceeded"),
		     errdetail("Too much concurent opened files"),
		     errhint("You can only open a maximum of ten files for each session")));
	
	return i;
}

/* return stored pointer to FILE */
static FILE *
get_stream(int d, int *max_linesize)
{
	if (d < 0 || d >= MAX_SLOTS)
		INVALID_FILEHANDLE_EXCEPTION();

	if (!slots[d].file)
		INVALID_FILEHANDLE_EXCEPTION();

	if (max_linesize)
		*max_linesize = slots[d].max_linesize;

	return slots[d].file;
}


/*
 * FUNCTION UTL_FILE.FOPEN(location text,
 *			   filename text,
 *			   open_mode text,
 *			   max_linesize integer)
 *          RETURNS UTL_FILE.FILE_TYPE;
 * 
 * The FOPEN function opens specified file and returns file handle.
 *  open_mode: ['R', 'W', 'A']
 *  max_linesize: [1 .. 32767]
 *
 * Exceptions:
 *  INVALID_MODE, INVALID_OPERATION, INVALID_PATH, INVALID_MAXLINESIZE
 */
Datum 
utl_file_fopen(PG_FUNCTION_ARGS)
{
	text *loc;
	text *filename;
	text *open_mode;
	int	max_linesize;
	const char *mode = NULL;
	FILE *file;

	NOT_NULL_ARG(0);
	NOT_NULL_ARG(1);
	NOT_NULL_ARG(2);
	NOT_NULL_ARG(3);

	loc = PG_GETARG_TEXT_P(0);
	filename = PG_GETARG_TEXT_P(1);
	open_mode = PG_GETARG_TEXT_P(2);

	NON_EMPTY_TEXT(loc);
	NON_EMPTY_TEXT(filename);
	NON_EMPTY_TEXT(open_mode);

	max_linesize = PG_GETARG_INT32(3);
	if (max_linesize < 1 || max_linesize > MAX_LINESIZE)
		CUSTOM_EXCEPTION(INVALID_MAXLINESIZE, "maxlinesize is out of range");

	if (VARSIZE(open_mode) - VARHDRSZ != 1)
		CUSTOM_EXCEPTION(INVALID_MODE, "open mode is different than [R,W,A]");

	switch (*((char*)VARDATA(open_mode)))
	{
		case 'a':
		case 'A':
			mode = "a";
			break;

		case 'r':
		case 'R':
			mode = "r";
			break;

		case 'w':
		case 'W':
			mode = "w";
			break;

		default:
			CUSTOM_EXCEPTION(INVALID_MODE, "open mode is different than [R,W,A]");
	}
		
	file = fopen("/tmp/test.txt", mode);

	PG_RETURN_INT32(get_descriptor(file, max_linesize));
}


Datum
utl_file_get_line(PG_FUNCTION_ARGS)
{
	PG_RETURN_NULL();
}

/*
 * FUNCTION UTL_FILE.PUT(file UTL_FILE.FILE_TYPE, buffer text)
 *          RETURNS bool;
 *
 * The PUT function puts data out to specified file. Buffer length allowed is
 * 32K or 1023 (max_linesize); 
 *
 * Exceptions:
 *  INVALID_FILEHANDLE, INVALID_OPERATION, WRITE_ERROR, VALUE_ERROR
 * 
 * Note: returns bool because I cannot do envelope over void function
 */

#define CHECK_ERRNO_PUT()  \
	switch (errno) \
	{ \
		case EBADF: \
			CUSTOM_EXCEPTION(INVALID_OPERATION, "file descriptor isn't valid for writing"); \
			break; \
		default: \
			CUSTOM_EXCEPTION(WRITE_ERROR, strerror(errno)); \
	}  

Datum
utl_file___put(PG_FUNCTION_ARGS)
{
	FILE *f;
	text *buffer;
	int max_linesize;
	long bfsize;
	char *tbuf;
	bool new_line = false;

	CHECK_FILE_HANDLE();
	f = get_stream(PG_GETARG_INT32(0), &max_linesize);

	NOT_NULL_ARG(1);
	buffer = PG_GETARG_TEXT_P(1);

	if (!PG_ARGISNULL(2))
		new_line = PG_GETARG_BOOL(2);

	/* I have to check buffer's size */
	bfsize = VARSIZE(buffer) - VARHDRSZ;
	if (bfsize > max_linesize)
		CUSTOM_EXCEPTION(VALUE_ERROR, "buffer is too long");
	
	tbuf = (char *) palloc(bfsize + 1);
	memcpy(tbuf, VARDATA(buffer), bfsize);
	tbuf[bfsize] = '\0';

	if (fputs(tbuf, f) == EOF)
		CHECK_ERRNO_PUT();

	if (new_line)
		if (fputc('\n', f) == EOF)
		    CHECK_ERRNO_PUT();

	PG_RETURN_BOOL(true);
}


Datum
utl_file_putf(PG_FUNCTION_ARGS)
{
	PG_RETURN_VOID();
}


/*
 * FUNCTION UTL_FILE.FFLUSH(file UTL_FILE.FILE_TYPE)
 *          RETURNS void;
 *
 * This function makes sure that all pending data for the specified file is written
 * physically out to file. 
 *
 * Exceptions:
 *  INVALID_FILEHANDLE, INVALID_OPERATION, WRITE_ERROR
 */
Datum
utl_file_fflush(PG_FUNCTION_ARGS)
{
	FILE *f;

	CHECK_FILE_HANDLE();
	f = GET_FILESTREAM();

	if (fflush(f) != 0)
	{
		if (errno == EBADF)
			CUSTOM_EXCEPTION(INVALID_OPERATION, "File is not an opened, or is not open for writing");
		else
			CUSTOM_EXCEPTION(WRITE_ERROR, strerror(errno));
	}

	PG_RETURN_VOID();
}


/*
 * FUNCTION UTL_FILE.FCLOSE(file UTL_FILE.FILE_TYPE)
 *          RETURNS NULL
 *
 * Close an open file. This function reset file handle to NULL on Oracle platform.
 * It isn't possible in PostgreSQL, and then you have to call fclose function 
 * like:
 *        file := utl_file.fclose(file);
 *
 * Exception:
 *  INVALID_FILEHANDLE, WRITE_ERROR
 */
Datum
utl_file_fclose(PG_FUNCTION_ARGS)
{
	FILE *f;
	int slot;

	CHECK_FILE_HANDLE();
	
	slot = PG_GETARG_INT32(0);

	f = get_stream(slot, NULL);

	slots[slot].file = NULL;

	if (fclose(f) != 0)
	{
		if (errno == EBADF)
			CUSTOM_EXCEPTION(INVALID_FILEHANDLE, "File is not an opened");
		else
			CUSTOM_EXCEPTION(WRITE_ERROR, strerror(errno));
	}

	PG_RETURN_NULL();
}


/*
 * FUNCTION UTL_FILE.FCLOSE_ALL() 
 *          RETURNS void
 *
 * Close all opened files.
 *
 * Exceptions: WRITE_ERROR
 */
Datum
utl_file_fclose_all(PG_FUNCTION_ARGS)
{
	int i;
	bool any_error = false;

	for(i = 0; i < MAX_SLOTS; i++)
	{
		if (slots[i].file)
		{	
			int res = fclose(slots[i].file);
			any_error = res || any_error;
			slots[i].file = NULL;
		}
	}

	if (any_error)
		CUSTOM_EXCEPTION(WRITE_ERROR, strerror(errno));

	PG_RETURN_VOID();
}
