#include "postgres.h"
#include "funcapi.h"
#include "access/heapam.h"
#include "lib/stringinfo.h"

#include "libpq/libpq.h"
#include "libpq/pqformat.h"
#include "utils/memutils.h"
#include "catalog/pg_type.h"
#include "utils/array.h"
#include "utils/lsyscache.h"

#include "orafunc.h"

static bool is_server_output = false;
static bool is_enabled = false; 
static char *buffer = NULL;
static int   buffer_size = 0;
static int   buffer_len = 0;

static int  line_len = 0;
static char line[256];
static int  lines = 0;				/* I need to know row count for get_lines() */

static void add_toLine(text *str);
static void send_buffer();

Datum dbms_output_enable(PG_FUNCTION_ARGS);
Datum dbms_output_enable_default(PG_FUNCTION_ARGS);
Datum dbms_output_disable(PG_FUNCTION_ARGS);
Datum dbms_output_serveroutput(PG_FUNCTION_ARGS);
Datum dbms_output_put(PG_FUNCTION_ARGS);
Datum dbms_output_put_line(PG_FUNCTION_ARGS);
Datum dbms_output_new_line(PG_FUNCTION_ARGS);
Datum dbms_output_get_line(PG_FUNCTION_ARGS);
Datum dbms_output_get_lines(PG_FUNCTION_ARGS);

/*
 * Main purpouse is still notification about events, but with serveroutput(false)
 * you can use this module like clasic queue implementation put_line, get_line.
 * I respect Oracle limits, data 1Mb, line 255 chars.
 */

#define BUFFER_OVERFLOW_TEXT "buffer overflow, limit of %d bytes"
#define LINE_OVERFLOW_TEXT   "line length overflow, limit of 255 bytes"

/*
 * Aux. buffer functionality
 */

static void 
add_toLine(text *str)
{
	int len = VARSIZE(str) - VARHDRSZ;
	if (line_len + len > 255)
	    elog(ERROR, LINE_OVERFLOW_TEXT);
	memcpy(line+line_len, VARDATA(str), len);
	line_len += len;
	line[line_len] = '\0';
}


static void 
send_buffer()
{
    if (buffer_len > 0)
    {
	StringInfoData msgbuf;	    
	char *cursor = buffer;
	
	while (--buffer_len > 0)
	{
	    if (*cursor == '\0')
		*cursor = '\n';
	    *cursor++;
	}
	if (*cursor != '\0')
	    elog(ERROR, "internal error");	
    
	pq_beginmessage(&msgbuf, 'N');
    
	if (PG_PROTOCOL_MAJOR(FrontendProtocol) >= 3)
	{
		pq_sendbyte(&msgbuf, PG_DIAG_MESSAGE_PRIMARY);
		pq_sendstring(&msgbuf, buffer);
		pq_sendbyte(&msgbuf, '\0');
	}
	else
	{
		*cursor++ = '\n';
		*cursor = '\0';
		pq_sendstring(&msgbuf, buffer);
	}

	pq_endmessage(&msgbuf);
	pq_flush();
	lines = 0;
    }
}


/*
 * Aux db functions
 *
 */

PG_FUNCTION_INFO_V1(dbms_output_enable_default);

Datum
dbms_output_enable_default(PG_FUNCTION_ARGS)
{
    int32 n_buf_size = 20000;

    buffer = MemoryContextAlloc(TopMemoryContext, n_buf_size+1);
    buffer_size = n_buf_size;
    buffer_len  = 0;
    line_len    = 0;
    lines       = 0;
    is_enabled  = true;

    PG_RETURN_NULL();
}


PG_FUNCTION_INFO_V1(dbms_output_enable);

Datum
dbms_output_enable(PG_FUNCTION_ARGS)
{
    int32 n_buf_size = PG_GETARG_INT32(0);
    
    if (n_buf_size > 1000000)
        elog(ERROR, "limit excaushed");
    if (n_buf_size < 2000)
    {
        n_buf_size = 2000;
        elog(WARNING, "limit increased to 2000 bytes");
    }	     
    if (buffer != NULL)
        pfree(buffer);

    buffer = MemoryContextAlloc(TopMemoryContext, n_buf_size+1);
    buffer_size = n_buf_size;
    buffer_len  = 0;
    line_len    = 0;
    lines       = 0;
    is_enabled  = true;
    
    PG_RETURN_NULL();
}

PG_FUNCTION_INFO_V1(dbms_output_disable);

Datum
dbms_output_disable(PG_FUNCTION_ARGS)
{
    if (buffer != NULL)
        pfree(buffer);

    buffer      = NULL;
    buffer_size = 0;
    buffer_len  = 0;
    line_len    = 0;
    lines       = 0;
    is_enabled  = false;
    
    PG_RETURN_NULL();
}

PG_FUNCTION_INFO_V1(dbms_output_serveroutput);

Datum 
dbms_output_serveroutput(PG_FUNCTION_ARGS)
{
    bool flag = PG_GETARG_BOOL(0);

    if (flag == true)
    {
    	if (!is_enabled)
	{	
	    buffer = MemoryContextAlloc(TopMemoryContext, 20000+1);
	    buffer_size = 20000;
	    buffer_len  = 0;
	    line_len    = 0;
	    lines       = 0;
	    is_enabled  = true;
	}
    	is_server_output = true;
    }
    else
    	is_server_output = false;

    PG_RETURN_NULL();
}


/*
 * main functions
 */

PG_FUNCTION_INFO_V1(dbms_output_put);

Datum
dbms_output_put(PG_FUNCTION_ARGS)
{
    if (is_enabled)
    {
		text *str = PG_GETARG_TEXT_P(0);
		add_toLine(str);
    }
    PG_RETURN_NULL();
} 

PG_FUNCTION_INFO_V1(dbms_output_put_line);

Datum
dbms_output_put_line(PG_FUNCTION_ARGS)
{
    if (is_enabled)
    {
		text *str = PG_GETARG_TEXT_P(0);
		add_toLine(str);
		if (buffer_len + line_len + 1 > buffer_size)
			elog(ERROR, BUFFER_OVERFLOW_TEXT, buffer_size);
		memcpy(buffer + buffer_len, line, line_len + 1);
		buffer_len += line_len + 1;
		line_len = 0; 
		lines++;
		if (is_server_output)
			send_buffer();
    }
    PG_RETURN_NULL();
}

PG_FUNCTION_INFO_V1(dbms_output_new_line);
    
Datum
dbms_output_new_line(PG_FUNCTION_ARGS)
{
    if (is_enabled)
    {
		if (buffer_len + line_len + 1 > buffer_size)
			elog(ERROR, BUFFER_OVERFLOW_TEXT, buffer_size);
		memcpy(buffer + buffer_len, line, line_len + 1);
		buffer_len += line_len + 1;
		line_len = 0; 
		lines++;
		if (is_server_output)
			send_buffer();
    }
    PG_RETURN_NULL();
}

PG_FUNCTION_INFO_V1(dbms_output_get_line);

Datum
dbms_output_get_line(PG_FUNCTION_ARGS)
{
    TupleDesc	tupdesc, btupdesc;
    AttInMetadata	*attinmeta;    
    HeapTuple	tuple;
    Datum 	result;
    
    char *str[2] = {NULL,"0"};
	
    if (lines > 0)
    {
		str[0] = buffer;
		str[1] = "1";
    }

    get_call_result_type(fcinfo, NULL, &tupdesc);
    btupdesc = BlessTupleDesc(tupdesc);
    attinmeta = TupleDescGetAttInMetadata(btupdesc);
    tuple = BuildTupleFromCStrings(attinmeta, str);
    result = HeapTupleGetDatum(tuple);
    
    if (lines > 0)
    {
		int len = strlen(buffer) + 1;
		memcpy(buffer, buffer + len, buffer_len - len);
		buffer_len -= len;
		lines--;
    }
    
    return result;
}



PG_FUNCTION_INFO_V1(dbms_output_get_lines);

Datum
dbms_output_get_lines(PG_FUNCTION_ARGS)
{
    int32 max_lines = PG_GETARG_INT32(0);
    bool disnull = false;
	
    ArrayBuildState *astate = NULL;
	
    TupleDesc	tupdesc, btupdesc;
    HeapTuple	tuple;
    Datum 	result;

    Datum dvalues[2];
    bool isnull[2] = {false, false};

    int fldnum = 0;
    char *cursor = buffer;

    text *line = palloc(255 + VARHDRSZ);
	
    if (max_lines == 0)
	max_lines = lines;
    
    
    if (lines > 0 && max_lines > 0)
    {
		while (lines > 0 && max_lines-- > 0)
		{
			Datum dvalue;
			
			int len = strlen(cursor);
			memcpy(VARDATA(line), cursor, len);
			VARATT_SIZEP(line) = len + VARHDRSZ;
	    
			dvalue = PointerGetDatum(line);
			astate = accumArrayResult(astate, dvalue,
									  disnull, TEXTOID,  CurrentMemoryContext);
			cursor += len + 1;
			fldnum++;    
			lines--;
		}
		dvalues[0] = makeArrayResult(astate, CurrentMemoryContext);
		
		if (lines > 0)
		{
			memcpy(buffer, cursor, buffer_len - (cursor - buffer));
			buffer_len -= cursor - buffer;
		}
		else
		{
			buffer_len = 0;
		}
    }
    else
    {
		int16		typlen;
		bool		typbyval;
		char		typalign;

		get_typlenbyvalalign(TEXTOID, &typlen, &typbyval, &typalign);
	
#ifdef PG_VERSION_82_COMPAT
		dvalues[0] = (Datum) construct_md_array(NULL, NULL, 0, NULL, NULL, TEXTOID, typlen, typbyval, typalign);
#else
		dvalues[0] = (Datum) construct_md_array(NULL, 0, NULL, NULL, TEXTOID, typlen, typbyval, typalign);
#endif

    }

    dvalues[1] = Int32GetDatum(fldnum);
    get_call_result_type(fcinfo, NULL, &tupdesc);
    btupdesc = BlessTupleDesc(tupdesc);
    tuple = heap_form_tuple(btupdesc, dvalues, isnull);
    result = HeapTupleGetDatum(tuple);

    return result;    
}


