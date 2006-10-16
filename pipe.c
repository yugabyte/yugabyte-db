#include "postgres.h"
#include "funcapi.h"
#include "fmgr.h"
#include "storage/shmem.h"
#include "utils/memutils.h"
#include "utils/timestamp.h"
#include "storage/lwlock.h"
#include "miscadmin.h"
#include "string.h"
#include "lib/stringinfo.h"
#include "orafunc.h"
#include "catalog/pg_type.h"
#include "utils/builtins.h"
#include "utils/date.h"
#include "utils/numeric.h"

#include "shmmc.h"
#include "pipe.h"

/*
 * First test version 0.0.9
 * @ Pavel Stehule 2006
 */

#ifndef GetNowFloat
#ifdef HAVE_INT64_TIMESTAMP
#define GetNowFloat()   ((float8) GetCurrentTimestamp() / 1000000.0)
#else
#define GetNowFloat()   GetCurrentTimestamp()
#endif
#endif

#define RESULT_DATA	0
#define RESULT_WAIT	1

#define NOT_INITIALIZED -1
#define ONE_YEAR (60*60*24*365)

Datum dbms_pipe_pack_message_text(PG_FUNCTION_ARGS);
Datum dbms_pipe_unpack_message_text(PG_FUNCTION_ARGS);
Datum dbms_pipe_send_message(PG_FUNCTION_ARGS);
Datum dbms_pipe_receive_message(PG_FUNCTION_ARGS);
Datum dbms_pipe_unique_session_name (PG_FUNCTION_ARGS);
Datum dbms_pipe_list_pipes (PG_FUNCTION_ARGS);
Datum dbms_pipe_next_item_type (PG_FUNCTION_ARGS);
Datum dbms_pipe_create_pipe(PG_FUNCTION_ARGS);
Datum dbms_pipe_create_pipe_2(PG_FUNCTION_ARGS);
Datum dbms_pipe_create_pipe_1(PG_FUNCTION_ARGS);
Datum dbms_pipe_reset_buffer(PG_FUNCTION_ARGS);
Datum dbms_pipe_purge(PG_FUNCTION_ARGS);
Datum dbms_pipe_remove_pipe(PG_FUNCTION_ARGS);
Datum dbms_pipe_pack_message_date(PG_FUNCTION_ARGS);
Datum dbms_pipe_unpack_message_date(PG_FUNCTION_ARGS);
Datum dbms_pipe_pack_message_timestamp(PG_FUNCTION_ARGS);
Datum dbms_pipe_unpack_message_timestamp(PG_FUNCTION_ARGS);
Datum dbms_pipe_pack_message_number(PG_FUNCTION_ARGS);
Datum dbms_pipe_unpack_message_number(PG_FUNCTION_ARGS);
Datum dbms_pipe_pack_message_bytea(PG_FUNCTION_ARGS);
Datum dbms_pipe_unpack_message_bytea(PG_FUNCTION_ARGS);
Datum dbms_pipe_pack_message_record(PG_FUNCTION_ARGS);
Datum dbms_pipe_unpack_message_record(PG_FUNCTION_ARGS);
Datum dbms_pipe_pack_message_integer(PG_FUNCTION_ARGS);
Datum dbms_pipe_pack_message_bigint(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(dbms_pipe_pack_message_text);
PG_FUNCTION_INFO_V1(dbms_pipe_unpack_message_text);
PG_FUNCTION_INFO_V1(dbms_pipe_send_message);
PG_FUNCTION_INFO_V1(dbms_pipe_receive_message);
PG_FUNCTION_INFO_V1(dbms_pipe_unique_session_name);
PG_FUNCTION_INFO_V1(dbms_pipe_list_pipes);
PG_FUNCTION_INFO_V1(dbms_pipe_next_item_type);
PG_FUNCTION_INFO_V1(dbms_pipe_create_pipe);
PG_FUNCTION_INFO_V1(dbms_pipe_create_pipe_2);
PG_FUNCTION_INFO_V1(dbms_pipe_create_pipe_1);
PG_FUNCTION_INFO_V1(dbms_pipe_reset_buffer);
PG_FUNCTION_INFO_V1(dbms_pipe_purge);
PG_FUNCTION_INFO_V1(dbms_pipe_remove_pipe);
PG_FUNCTION_INFO_V1(dbms_pipe_pack_message_date);
PG_FUNCTION_INFO_V1(dbms_pipe_unpack_message_date);
PG_FUNCTION_INFO_V1(dbms_pipe_pack_message_timestamp);
PG_FUNCTION_INFO_V1(dbms_pipe_unpack_message_timestamp);
PG_FUNCTION_INFO_V1(dbms_pipe_pack_message_number);
PG_FUNCTION_INFO_V1(dbms_pipe_unpack_message_number);
PG_FUNCTION_INFO_V1(dbms_pipe_pack_message_bytea);
PG_FUNCTION_INFO_V1(dbms_pipe_unpack_message_bytea);
PG_FUNCTION_INFO_V1(dbms_pipe_pack_message_record);
PG_FUNCTION_INFO_V1(dbms_pipe_unpack_message_record);
PG_FUNCTION_INFO_V1(dbms_pipe_pack_message_integer);
PG_FUNCTION_INFO_V1(dbms_pipe_pack_message_bigint);

typedef enum {
	IT_NO_MORE_ITEMS = 0,
	IT_NUMBER = 9, 
	IT_VARCHAR = 11,
	IT_DATE = 12,
	IT_TIMESTAMPTZ = 13,
	IT_BYTEA = 23,
	IT_RECORD = 24
} message_data_type;


typedef struct _queue_item {
	void *ptr;
	struct _queue_item *next_item;
} queue_item;

typedef struct {
	bool is_valid;
	bool registered;
	char *pipe_name;
	char *creator;
	Oid  uid;
	struct _queue_item *items;
	int16 count;
	int16 limit;
	int size;
} pipe;

typedef struct {
	size_t size;
	int items_count;
	char data;
} message_buffer;

typedef struct {
	size_t size;
	message_data_type type;
	Oid tupType;
	char data;
} message_data_item;

typedef struct PipesFctx {
	int pipe_nth;
	char **values;
} PipesFctx;

typedef struct
{
	LWLockId shmem_lock;
	pipe *pipes;
	alert_event *events;
	alert_lock *locks;
	size_t size;
	unsigned int sid;
	char data[];
} sh_memory;


message_buffer *output_buffer = NULL;
message_buffer *input_buffer = NULL;

message_data_item *writer = NULL;
message_data_item *reader = NULL;

pipe* pipes = NULL;
LWLockId shmem_lock = NOT_INITIALIZED;
unsigned int sid;                                 /* session id */
Oid uid;

extern alert_event *events;
extern alert_lock  *locks;

/*
 * write on writer size bytes from ptr
 */

static void 
pack_field(message_buffer *message, message_data_item **writer,
		   message_data_type type, int size, void *ptr, Oid *tupType)
{
	int l;
	message_data_item *_wr = *writer;

	l = size + sizeof(message_data_item);
	if (message->size + l > LOCALMSGSZ-sizeof(message_buffer))
    		ereport(ERROR,             
                            (errcode(ERRCODE_OUT_OF_MEMORY),
                             errmsg("out of memory"),
                             errdetail("Packed message is biger than local buffer."),
			     errhint("Increase LOCALMSGSZ in 'pipe.h' and recompile library.")));                     

	if (_wr == NULL)
		_wr = (message_data_item*)&message->data;

	_wr->size = l;

	_wr->type = type;
	if (tupType != NULL)
	    _wr->tupType = *tupType;
	
	memcpy(&_wr->data, ptr, size);

	message->size += l;
	message->items_count +=1;

	_wr = (message_data_item*)((char*)_wr + l);
	*writer = _wr;
}


static void*
unpack_field(message_buffer *message, message_data_item **reader, 
			 message_data_type *type, size_t *size, Oid *tupType)
{
	void *ptr;
	message_data_item *_rd = *reader;

	if (_rd == NULL)
	{
		_rd = (message_data_item*)&message->data;
	}
	if ((message->items_count)--)
	{
		*size = _rd->size - sizeof(message_data_item);
		*type = _rd->type;
		
		if (tupType)
			*tupType = _rd->tupType;

		ptr  = (void*)&_rd->data;

		_rd = (message_data_item*)((char*)_rd + _rd->size);
		*reader = message->items_count > 0 ? _rd : NULL;

		return ptr;
	}

	return NULL;
}


/*
 * Add ptr to queue. If pipe doesn't exist, regigister new pipe
 */

bool
ora_lock_shmem(size_t size, int max_pipes, int max_events, int max_locks, bool reset)
{
	int i;
	bool found;

	sh_memory *sh_mem;

	if (pipes == NULL)
	{
		sh_mem = ShmemInitStruct("dbms_pipe", size, &found);
		uid = GetUserId();
		if (sh_mem == NULL)
    			ereport(ERROR,             
                        	(errcode(ERRCODE_OUT_OF_MEMORY),
                                 errmsg("out of memory"),
                                 errdetail("Failed while allocation block %d bytes in shared memory.", size))); 

		if (!found)
		{
			shmem_lock = sh_mem->shmem_lock = LWLockAssign();
			LWLockAcquire(sh_mem->shmem_lock, LW_EXCLUSIVE);
			sh_mem->size = size - sizeof(sh_memory);
			ora_sinit(&sh_mem->data, size, true);
			pipes = sh_mem->pipes = ora_salloc(max_pipes*sizeof(pipe));
			sid = sh_mem->sid = 1;			
			for(i = 0; i < max_pipes; i++)
				pipes[i].is_valid = false;

			events = sh_mem->events = ora_salloc(max_events*sizeof(alert_event));
			locks = sh_mem->locks = ora_salloc(max_locks*sizeof(alert_lock));

			for(i = 0; i < max_events; i++)
			{
				events[i].event_name = NULL;
				events[i].max_receivers = 0;
				events[i].receivers = NULL;
				events[i].messages = NULL;
			}
			for (i = 0; i < max_locks; i++)
			{
				locks[i].sid = -1;
				locks[i].echo = NULL;
			}
												 
		}
		else if (sh_mem->shmem_lock != 0)
		{
			pipes = sh_mem->pipes;
			shmem_lock = sh_mem->shmem_lock;
			LWLockAcquire(sh_mem->shmem_lock, LW_EXCLUSIVE);
			ora_sinit(&sh_mem->data, sh_mem->size, reset);
			sid = ++(sh_mem->sid);
			events = sh_mem->events;
			locks = sh_mem->locks;
		}
	}
	else
	{
		LWLockAcquire(shmem_lock, LW_EXCLUSIVE);
	}
/*
	if (reset && pipes == NULL)
		elog(ERROR, "Can't purge memory");
*/

	return pipes != NULL;	
}


/*
 * can be enhanced access/hash.h
 */

static pipe*
find_pipe(text* pipe_name, bool* created, bool only_check)
{
	int i;
	pipe *result = NULL;

	*created = false;
	for (i = 0; i < MAX_PIPES; i++)
	{
		if (pipes[i].is_valid && 
			strncmp((char*)VARDATA(pipe_name), pipes[i].pipe_name, VARSIZE(pipe_name) - VARHDRSZ) == 0
			&& (strlen(pipes[i].pipe_name) == (VARSIZE(pipe_name) - VARHDRSZ)))
		{
			/* check owner if non public pipe */

			if (pipes[i].creator != NULL && pipes[i].uid != uid)
			{
				LWLockRelease(shmem_lock);
    				ereport(ERROR,             
                        		(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
                                	 errmsg("insufficient privilege"),
                                	 errdetail("Insufficient privilege to access pipe"))); 
			}

			return &pipes[i];
		}
	}

	if (only_check)
		return result;

	for (i = 0; i < MAX_PIPES; i++)
		if (!pipes[i].is_valid)
		{
			if (NULL != (pipes[i].pipe_name = ora_scstring(pipe_name)))
			{
				pipes[i].is_valid = true;
				pipes[i].registered = false;
				pipes[i].creator = NULL;
				pipes[i].uid = -1;
				pipes[i].count = 0;
				pipes[i].limit = -1;

				*created = true;
				result = &pipes[i];
			}
			break;
		}

	return result;	
}


static bool
new_last(pipe *p, void *ptr)
{
	queue_item *q, *aux_q;

	if (p->count >= p->limit && p->limit != -1)
		return false;

	if (p->items == NULL)
	{
		if (NULL == (p->items = ora_salloc(sizeof(queue_item))))
			return false;
		p->items->next_item = NULL;
		p->items->ptr = ptr;
		p->count = 1;
		return true;
	}
	q = p->items;
	while (q->next_item != NULL)
		q = q->next_item;
	

	if (NULL == (aux_q = ora_salloc(sizeof(queue_item))))
		return false;

	q->next_item = aux_q;
	aux_q->next_item = NULL;
	aux_q->ptr = ptr;

	p->count += 1;

	return true;
}


static void*
remove_first(pipe *p, bool *found)
{
	struct _queue_item *q;
	void *ptr = NULL;

	*found = false;

	if (NULL != (q = p->items))
	{
		p->count -= 1;
		ptr = q->ptr;
		p->items = q->next_item;
		*found = true;

		ora_sfree(q);
		if (p->items == NULL && !p->registered)
		{
			ora_sfree(p->pipe_name);
			p->is_valid = false;
		}

	}

	return ptr;
}


/* copy message to local memory, if exists */

static message_buffer*
get_from_pipe(text *pipe_name, bool *found)
{
	pipe *p;
	bool created;
	message_buffer *shm_msg;
	message_buffer *result = NULL;

	if (!ora_lock_shmem(SHMEMMSGSZ, MAX_PIPES, MAX_EVENTS, MAX_LOCKS, false))
		return NULL;

	if (NULL != (p = find_pipe(pipe_name, &created,false)))
	{
		if (!created)
		{
			if (NULL != (shm_msg = remove_first(p, found)))
			{
				p->size -= shm_msg->size;

				result = (message_buffer*) MemoryContextAlloc(TopMemoryContext, shm_msg->size);
				memcpy(result, shm_msg, shm_msg->size);
				ora_sfree(shm_msg);
			}
		}
	}

	LWLockRelease(shmem_lock);

	return result;
}


/*
 * if ptr is null, then only register pipe
 */

static bool
add_to_pipe(text *pipe_name, message_buffer *ptr, int limit, bool limit_is_valid)
{
	pipe *p;
	bool created;
	bool result = false;
	message_buffer *sh_ptr;

	if (!ora_lock_shmem(SHMEMMSGSZ, MAX_PIPES, MAX_EVENTS, MAX_LOCKS,false))
		return false;
		
	for (;;)
	{
		if (NULL != (p = find_pipe(pipe_name, &created, false)))
		{
			if (created)
				p->registered = ptr == NULL;
		
			if (limit_is_valid && (created || (p->limit < limit)))
				p->limit = limit;
		
			if (ptr != NULL)
			{
				if (NULL != (sh_ptr = ora_salloc(ptr->size)))
				{
					memcpy(sh_ptr,ptr,ptr->size);
					if (new_last(p, sh_ptr))
					{
						p->size += ptr->size;
						result = true;
						break;
					}
					ora_sfree(sh_ptr);
				}
				if (created)
				{
					/* I created new pipe, but haven't memory for new value */
					ora_sfree(p->pipe_name);
					p->is_valid = false;
					result = false;
				}	
			}
			else
				result = true;
		}
		break;
	}
	LWLockRelease(shmem_lock);
	return result;
}


static void
remove_pipe(text *pipe_name, bool purge)
{
	pipe *p;
	bool created;

	if (NULL != (p = find_pipe(pipe_name, &created, true)))
	{
		queue_item *q = p->items;
		while (q != NULL)
		{
			queue_item *aux_q;

			aux_q = q->next_item;
			if (q->ptr)
				ora_sfree(q->ptr);
			ora_sfree(q);
			q = aux_q;
		}
		p->items = NULL;
		p->size = 0;
		p->count = 0;
		if (!(purge && p->registered))
		{
			ora_sfree(p->pipe_name);
			p->is_valid = false;
		}
	}
}


Datum
dbms_pipe_next_item_type (PG_FUNCTION_ARGS)
{
	
	PG_RETURN_INT32(reader != NULL ? reader->type : IT_NO_MORE_ITEMS);
}

static message_buffer*
check_buffer(message_buffer *buf, size_t size, message_data_item **writer)
{
	if (buf == NULL)
	{
		buf = (message_buffer*) MemoryContextAlloc(TopMemoryContext, size);
		buf->size = sizeof(message_buffer);
		buf->items_count = 0;
		*writer = (message_data_item*) &buf->data;
	}	

	return buf;
}

Datum
dbms_pipe_pack_message_text(PG_FUNCTION_ARGS)
{
	text *str = PG_GETARG_TEXT_P(0);

	output_buffer = check_buffer(output_buffer, LOCALMSGSZ, &writer);	
	pack_field(output_buffer, &writer, IT_VARCHAR, 
			   VARSIZE(str) - VARHDRSZ, VARDATA(str),
			   NULL); 

	PG_RETURN_VOID();
}


Datum
dbms_pipe_pack_message_date(PG_FUNCTION_ARGS)
{
	DateADT dt = PG_GETARG_DATEADT(0);

	output_buffer = check_buffer(output_buffer, LOCALMSGSZ, &writer);	
	pack_field(output_buffer, &writer, IT_DATE,
			   sizeof(dt), &dt,
			   NULL); 

	PG_RETURN_VOID();
}


Datum
dbms_pipe_pack_message_timestamp(PG_FUNCTION_ARGS)
{
	TimestampTz dt = PG_GETARG_TIMESTAMPTZ(0);

	output_buffer = check_buffer(output_buffer, LOCALMSGSZ, &writer);	
	pack_field(output_buffer, &writer, IT_TIMESTAMPTZ,
			   sizeof(dt), &dt,
			   NULL); 

	PG_RETURN_VOID();

}


Datum
dbms_pipe_pack_message_number(PG_FUNCTION_ARGS)
{
	Numeric num = PG_GETARG_NUMERIC(0);

	output_buffer = check_buffer(output_buffer, LOCALMSGSZ, &writer);	
	pack_field(output_buffer, &writer, IT_NUMBER,
			   VARSIZE(num) - VARHDRSZ, VARDATA(num),
			   NULL); 

	PG_RETURN_VOID();

}


Datum
dbms_pipe_pack_message_bytea(PG_FUNCTION_ARGS)
{
	bytea *data = PG_GETARG_BYTEA_P(0);

	output_buffer = check_buffer(output_buffer, LOCALMSGSZ, &writer);	
	pack_field(output_buffer, &writer, IT_BYTEA,
			   VARSIZE(data) - VARHDRSZ, VARDATA(data),
			   NULL); 

	PG_RETURN_VOID();

}


/*
 *  We can serialize only typed record
 */

Datum
dbms_pipe_pack_message_record(PG_FUNCTION_ARGS)
{
	HeapTupleHeader rec = PG_GETARG_HEAPTUPLEHEADER(0);
	Oid tupType;
	bytea *data;
	FunctionCallInfoData locfcinfo;

	tupType = HeapTupleHeaderGetTypeId(rec);
	
	/*
	 * Normally one would call record_send() using DirectFunctionCall3,
	 * but that does not work since record_send wants to cache some data
	 * using fcinfo->flinfo->fn_extra.  So we need to pass it our own
	 * flinfo parameter.
	 */
	InitFunctionCallInfoData(locfcinfo, fcinfo->flinfo, 3, NULL, NULL);

	locfcinfo.arg[0] = PointerGetDatum(rec);
	locfcinfo.arg[1] = ObjectIdGetDatum(tupType);
	locfcinfo.arg[2] = Int32GetDatum(-1);
	locfcinfo.argnull[0] = false;
	locfcinfo.argnull[1] = false;
	locfcinfo.argnull[2] = false;
			
	data = (bytea*) DatumGetPointer(record_send(&locfcinfo));

	output_buffer = check_buffer(output_buffer, LOCALMSGSZ, &writer);	
	pack_field(output_buffer, &writer, IT_RECORD,
			   VARSIZE(data), VARDATA(data),
			   &tupType); 

	PG_RETURN_VOID();
}


static int64
dbms_pipe_unpack_message(message_data_type dtype, bool *is_null, Oid *tupType)
{
	void *ptr;
	message_data_type type;
	size_t size;
	int64 result = 0;
	message_data_type next_type;

	*is_null = true;
	if (input_buffer == NULL)
		return result;

	next_type =  reader != NULL ? reader->type : IT_NO_MORE_ITEMS;
	if (next_type == IT_NO_MORE_ITEMS)
		return result;

	if (next_type != dtype)
		ereport(ERROR,             
				(errcode(ERRCODE_DATATYPE_MISMATCH),
				 errmsg("datatype mismatch"),
				 errdetail("unpack unexpected type"))); 
	
	if (NULL == (ptr = unpack_field(input_buffer, &reader, &type, &size, tupType)))
		return result;

	switch (type)
	{
		case IT_TIMESTAMPTZ:
			result = *(int64*)ptr;
			break;

		case IT_DATE:
			result = *(DateADT*)ptr;
			break;

		case IT_VARCHAR:
		case IT_NUMBER:
		case IT_BYTEA:
		case IT_RECORD:
			result = (int64)((int32)ora_make_text_fix((char*)ptr, size));
			break;			
	
		default:
			*is_null = true;
			break;
	}

	*is_null = false;

	if (input_buffer->items_count == 0)
	{
		pfree(input_buffer);
		input_buffer = NULL;
	}
	
	return result;
}


Datum
dbms_pipe_unpack_message_text(PG_FUNCTION_ARGS)
{
	bool is_null;
	text *result;
	
	result = (text*)((int32)dbms_pipe_unpack_message(IT_VARCHAR, &is_null,
													 NULL));
	if (is_null)
		PG_RETURN_NULL();
	else
		PG_RETURN_TEXT_P(result);
}


Datum
dbms_pipe_unpack_message_date(PG_FUNCTION_ARGS)
{
	bool is_null;
	DateADT result;
	
	result = (DateADT)dbms_pipe_unpack_message(IT_DATE, &is_null,
											   NULL);
	if (is_null)
		PG_RETURN_NULL();
	else
		PG_RETURN_DATEADT(result);
}

Datum
dbms_pipe_unpack_message_timestamp(PG_FUNCTION_ARGS)
{
	bool is_null;
	TimestampTz result;

	int64 value;

	value = dbms_pipe_unpack_message(IT_TIMESTAMPTZ, &is_null, 
									 NULL);
	if (is_null)
		PG_RETURN_NULL();
	else
		result = *((TimestampTz*)&value);
		PG_RETURN_TIMESTAMPTZ(result);
}


Datum
dbms_pipe_unpack_message_number(PG_FUNCTION_ARGS)
{
	bool is_null;
	Numeric result;

	result = (Numeric)((int32)dbms_pipe_unpack_message(IT_NUMBER, &is_null,
													   NULL));
	if (is_null)
		PG_RETURN_NULL();
	else
		PG_RETURN_NUMERIC(result);
}


Datum
dbms_pipe_unpack_message_bytea(PG_FUNCTION_ARGS)
{
	bool is_null;
	bytea *result;

	result = (bytea*)((int32)dbms_pipe_unpack_message(IT_BYTEA, &is_null,
													  NULL));
	if (is_null)
		PG_RETURN_NULL();
	else
		PG_RETURN_BYTEA_P(result);
}


Datum
dbms_pipe_unpack_message_record(PG_FUNCTION_ARGS)
{
	bool is_null;
	bytea *data;
	FunctionCallInfoData locfcinfo;
	Oid tupType;
	HeapTupleHeader rec; 
	StringInfoData buf;

	data = (bytea*)((int32)dbms_pipe_unpack_message(IT_RECORD, &is_null, &tupType));
	
	if (!is_null)
	{												
		buf.data = VARDATA(data);
		buf.len = VARSIZE(data) - VARHDRSZ;
		buf.maxlen = buf.len;
		buf.cursor = 0;
	
		/*
		 * Normally one would call record_recv() using DirectFunctionCall3,
		 * but that does not work since record_recv wants to cache some data
		 * using fcinfo->flinfo->fn_extra.  So we need to pass it our own
		 * flinfo parameter.
		 */
		InitFunctionCallInfoData(locfcinfo, fcinfo->flinfo, 3, NULL, NULL);
		
		locfcinfo.arg[0] = PointerGetDatum(&buf);
		locfcinfo.arg[1] = ObjectIdGetDatum(tupType);
		locfcinfo.arg[2] = Int32GetDatum(-1);
		locfcinfo.argnull[0] = false;
		locfcinfo.argnull[1] = false;
		locfcinfo.argnull[2] = false;
		
		rec =  DatumGetHeapTupleHeader(record_recv(&locfcinfo));
	
		/* I have to translate rec to tuple */
	
		pfree(data);
		PG_RETURN_HEAPTUPLEHEADER(rec);
	}

	PG_RETURN_NULL();
}


#define WATCH_PRE(t, et, c) \
et = GetNowFloat() + (float8)t; c = 0; \
do \
{ \

#define WATCH_POST(t,et,c) \
if (GetNowFloat() >= et) \
PG_RETURN_INT32(RESULT_WAIT); \
if (cycle++ % 100 == 0) \
CHECK_FOR_INTERRUPTS(); \
pg_usleep(10000L); \
} while(true && t != 0);


Datum
dbms_pipe_receive_message(PG_FUNCTION_ARGS)
{
	text *pipe_name = NULL; 
	int timeout = ONE_YEAR;
	int cycle = 0;
	float8 endtime;
	bool found = false;

	if (PG_ARGISNULL(0))
    		ereport(ERROR,
                        (errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
                         errmsg("pipe name is NULL"),
                         errdetail("Pipename may not be NULL.")));
	else
		pipe_name = PG_GETARG_TEXT_P(0);

	if (!PG_ARGISNULL(1))
		timeout = PG_GETARG_INT32(1);

	if (input_buffer != NULL)
		pfree(input_buffer);
	
	input_buffer = NULL;
	reader = NULL;
  
	WATCH_PRE(timeout, endtime, cycle);	
	if (NULL != (input_buffer = get_from_pipe(pipe_name, &found)))
	{
		reader = (message_data_item*)&input_buffer->data;
		break;
	}
/* found empty message */
	if (found)
		break;

	WATCH_POST(timeout, endtime, cycle);
	PG_RETURN_INT32(RESULT_DATA);
}


Datum
dbms_pipe_send_message(PG_FUNCTION_ARGS)
{
	text *pipe_name = NULL; 
	int timeout = ONE_YEAR;
	int limit = 0;
	bool valid_limit;

	int cycle = 0;
	float8 endtime;

	if (PG_ARGISNULL(0))
    		ereport(ERROR,
                        (errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
                         errmsg("pipe name is NULL"),
                         errdetail("Pipename may not be NULL.")));
	else
		pipe_name = PG_GETARG_TEXT_P(0);

	if (output_buffer == NULL)
	{
		output_buffer = (message_buffer*) MemoryContextAlloc(TopMemoryContext, LOCALMSGSZ);
		output_buffer->size = sizeof(message_buffer);
		output_buffer->items_count = 0;
	}	

	if (!PG_ARGISNULL(1))
		timeout = PG_GETARG_INT32(1);

	if (PG_ARGISNULL(2))
		valid_limit = false;
	else
	{
		limit = PG_GETARG_INT32(2);
		valid_limit = true;
	}

	if (input_buffer != NULL)
		pfree(input_buffer);

	input_buffer = NULL;
	reader = NULL;

	WATCH_PRE(timeout, endtime, cycle);	
	if (add_to_pipe(pipe_name, output_buffer, 
					limit, valid_limit))
		break;
	WATCH_POST(timeout, endtime, cycle);
		
	output_buffer->items_count = 0;
	output_buffer->size = sizeof(message_buffer);
	writer = (message_data_item*)&output_buffer->data;

	PG_RETURN_INT32(RESULT_DATA);
}


Datum
dbms_pipe_unique_session_name (PG_FUNCTION_ARGS)
{
	StringInfoData strbuf;
	text *result;

	float8 endtime;
	int cycle = 0;
	int timeout = 10;

	WATCH_PRE(timeout, endtime, cycle);	
	if (ora_lock_shmem(SHMEMMSGSZ, MAX_PIPES,MAX_EVENTS,MAX_LOCKS,false))
	{
		initStringInfo(&strbuf);
		appendStringInfo(&strbuf,"PG$PIPE$%d$%d",sid, MyProcPid);
		
		result = ora_make_text_fix(strbuf.data, strbuf.len);
		pfree(strbuf.data);
		LWLockRelease(shmem_lock);

		PG_RETURN_TEXT_P(result);
	}
	WATCH_POST(timeout, endtime, cycle);
	LOCK_ERROR();
	
	PG_RETURN_NULL();
}


Datum
dbms_pipe_list_pipes (PG_FUNCTION_ARGS)
{
	FuncCallContext *funcctx;
	TupleDesc        tupdesc;
	TupleTableSlot  *slot;
	AttInMetadata   *attinmeta;
	PipesFctx       *fctx;

	float8 endtime;
	int cycle = 0;
	int timeout = 10;	

	if (SRF_IS_FIRSTCALL ())
	{
		MemoryContext  oldcontext;
		bool has_lock = false;

		WATCH_PRE(timeout, endtime, cycle);	
		if (ora_lock_shmem(SHMEMMSGSZ, MAX_PIPES,MAX_EVENTS,MAX_LOCKS,false))
		{
			has_lock = true;
			break;
		}
		WATCH_POST(timeout, endtime, cycle);
		if (!has_lock)
			LOCK_ERROR();

		funcctx = SRF_FIRSTCALL_INIT ();
		oldcontext = MemoryContextSwitchTo (funcctx->multi_call_memory_ctx);

		fctx = (PipesFctx*) palloc (sizeof (PipesFctx));
		funcctx->user_fctx = (void *)fctx;

		fctx->values = (char **) palloc (4 * sizeof (char *));
		fctx->values  [0] = (char*) palloc (255 * sizeof (char));
		fctx->values  [1] = (char*) palloc  (16 * sizeof (char));
		fctx->values  [2] = (char*) palloc  (16 * sizeof (char));
		fctx->values  [3] = (char*) palloc  (16 * sizeof (char));
		fctx->values  [4] = (char*) palloc  (10 * sizeof (char));
		fctx->values  [5] = (char*) palloc (255 * sizeof (char));
		fctx->pipe_nth = 0;

		tupdesc = CreateTemplateTupleDesc (6 , false);

/* tady bude mozna nekompatibilita s 8.1, funkce mela driv 5 parametru */

		TupleDescInitEntry (tupdesc,  1, "Name",    VARCHAROID, -1, 0);
		TupleDescInitEntry (tupdesc,  2, "Items",   INT4OID   , -1, 0);
		TupleDescInitEntry (tupdesc,  3, "Size",    INT4OID,    -1, 0);
		TupleDescInitEntry (tupdesc,  4, "Limit",   INT4OID,    -1, 0);
		TupleDescInitEntry (tupdesc,  5, "Private", BOOLOID,    -1, 0);
		TupleDescInitEntry (tupdesc,  6, "Owner",   VARCHAROID, -1, 0);
		
		slot = TupleDescGetSlot (tupdesc); 
		funcctx -> slot = slot;
		
		attinmeta = TupleDescGetAttInMetadata (tupdesc);
		funcctx -> attinmeta = attinmeta;
		
		MemoryContextSwitchTo (oldcontext);
	}

	funcctx = SRF_PERCALL_SETUP ();
	fctx = (PipesFctx*) funcctx->user_fctx;

	while (fctx->pipe_nth < MAX_PIPES)
	{
		if (pipes[fctx->pipe_nth].is_valid)
		{
			Datum    result;
			char   **values;
			HeapTuple tuple;
			char     *aux_3, *aux_5;

			values = fctx->values;
			aux_3 = values[3]; aux_5 = values[5];
			values[3] = NULL; values[5] = NULL;

			snprintf (values[0], 255, "%s", pipes[fctx->pipe_nth].pipe_name);
			snprintf (values[1],  16, "%d", pipes[fctx->pipe_nth].count);
			snprintf (values[2],  16, "%d", pipes[fctx->pipe_nth].size);
			if (pipes[fctx->pipe_nth].limit != -1)
			{
				snprintf (aux_3,  16, "%d", pipes[fctx->pipe_nth].limit);
				values[3] = aux_3;

			}
			snprintf (values[4], 10, "%s", pipes[fctx->pipe_nth].creator != NULL ? "true" : "false");
			if (pipes[fctx->pipe_nth].creator != NULL)
			{
				snprintf (aux_5,  255, "%s", pipes[fctx->pipe_nth].creator);
				values[5] = aux_5;
			}
				
			tuple = BuildTupleFromCStrings (funcctx -> attinmeta,
											fctx -> values);
			result = TupleGetDatum (funcctx -> slot, tuple);
			
			values[3] = aux_3; values[5] = aux_5;
			fctx->pipe_nth += 1;
			SRF_RETURN_NEXT (funcctx, result);
		}
		fctx->pipe_nth += 1;
			
	}

	LWLockRelease(shmem_lock);	
	SRF_RETURN_DONE (funcctx);
}

/*
 * secondary functions 
 */

/*
 * Registration explicit pipes
 *   dbms_pipe.create_pipe(pipe_name varchar, limit := -1 int, private := false bool);
 */

Datum 
dbms_pipe_create_pipe (PG_FUNCTION_ARGS)
{
	text *pipe_name = NULL;
	int   limit = 0;
	bool  is_private;
	bool  limit_is_valid = false;
	bool  created;
	float8 endtime;
	int cycle = 0;
	int timeout = 10;

	if (PG_ARGISNULL(0))
    		ereport(ERROR,
                        (errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
                         errmsg("pipe name is NULL"),
                         errdetail("Pipename may not be NULL.")));
	else
		pipe_name = PG_GETARG_TEXT_P(0);

	if (!PG_ARGISNULL(1))
	{
		limit = PG_GETARG_INT32(1);
		limit_is_valid = true;
	}

	is_private = PG_ARGISNULL(2) ? false : PG_GETARG_BOOL(2);

	WATCH_PRE(timeout, endtime, cycle);	
	if (ora_lock_shmem(SHMEMMSGSZ, MAX_PIPES,MAX_EVENTS,MAX_LOCKS,false))
	{
		pipe *p;
		if (NULL != (p = find_pipe(pipe_name, &created, false)))
		{
			if (!created)
			{
				LWLockRelease(shmem_lock);
                    		ereport(ERROR,                                                                                                        
                            		(errcode(ERRCODE_DUPLICATE_OBJECT),                                                                           
                                	 errmsg("pipe creation error"),                                                                     
                                	 errdetail("Pipe is registered.")));                       
			}
			if (is_private)
			{
				char *user;
				
				p->uid = GetUserId();
				user = (char*)DirectFunctionCall1(namein, CStringGetDatum(GetUserNameFromId(p->uid)));
				p->creator = ora_sstrcpy(user);
				pfree(user);
			}
			p->limit = limit_is_valid ? limit : -1;
			p->registered = true;

			LWLockRelease(shmem_lock);
			PG_RETURN_VOID();
		}
	}
	WATCH_POST(timeout, endtime, cycle);
	LOCK_ERROR();
	
	PG_RETURN_VOID();	
}


/*
 * Clean local input, output buffers
 */

Datum
dbms_pipe_reset_buffer(PG_FUNCTION_ARGS)
{
	if (output_buffer != NULL)
	{
		pfree(output_buffer);
		output_buffer = NULL;
	}
		
	if (input_buffer != NULL)
	{
		pfree(input_buffer);
		input_buffer = NULL;
	}

	reader = NULL;
	writer = NULL;

	PG_RETURN_VOID();
}


/*
 * Remove all stored messages in pipe. Remove implicit created 
 * pipe.
 */

Datum 
dbms_pipe_purge (PG_FUNCTION_ARGS)
{
	text *pipe_name = PG_GETARG_TEXT_P(0);

	float8 endtime;
	int cycle = 0;
	int timeout = 10;

	WATCH_PRE(timeout, endtime, cycle);	
	if (ora_lock_shmem(SHMEMMSGSZ, MAX_PIPES,MAX_EVENTS,MAX_LOCKS,false))
	{

		remove_pipe(pipe_name, true);
		LWLockRelease(shmem_lock);

		PG_RETURN_VOID();
	}
	WATCH_POST(timeout, endtime, cycle);
	LOCK_ERROR();
	
	PG_RETURN_VOID();
}

/*
 * Remove pipe if exists
 */ 

Datum 
dbms_pipe_remove_pipe (PG_FUNCTION_ARGS)
{
	text *pipe_name = PG_GETARG_TEXT_P(0);

	float8 endtime;
	int cycle = 0;
	int timeout = 10;

	WATCH_PRE(timeout, endtime, cycle);	
	if (ora_lock_shmem(SHMEMMSGSZ, MAX_PIPES,MAX_EVENTS,MAX_LOCKS,false))
	{

		remove_pipe(pipe_name, false);
		LWLockRelease(shmem_lock);

		PG_RETURN_VOID();
	}
	WATCH_POST(timeout, endtime, cycle);
	LOCK_ERROR();
	
	PG_RETURN_VOID();
}


/*
 * Some void udf which I can't wrap in sql
 */

Datum
dbms_pipe_create_pipe_2 (PG_FUNCTION_ARGS)
{
	Datum arg1 = PG_GETARG_DATUM(0);
	Datum arg2 = PG_GETARG_DATUM(1);

	DirectFunctionCall3(dbms_pipe_create_pipe,
						    arg1,
						    arg2,
						    BoolGetDatum(false));

	PG_RETURN_VOID();
}

Datum
dbms_pipe_create_pipe_1 (PG_FUNCTION_ARGS)
{
	Datum arg1 = PG_GETARG_DATUM(0);

	DirectFunctionCall3(dbms_pipe_create_pipe,
						    arg1,
						    (Datum)0,
						    BoolGetDatum(false));

	PG_RETURN_VOID();
}

Datum 
dbms_pipe_pack_message_integer(PG_FUNCTION_ARGS)
{
	/* Casting from int4 to numeric */
	DirectFunctionCall1(dbms_pipe_pack_message_number, 
				DirectFunctionCall1(int4_numeric, PG_GETARG_DATUM(0)));

	PG_RETURN_VOID();
}

Datum 
dbms_pipe_pack_message_bigint(PG_FUNCTION_ARGS)
{
	/* Casting from int8 to numeric */
	DirectFunctionCall1(dbms_pipe_pack_message_number, 
				DirectFunctionCall1(int8_numeric, PG_GETARG_DATUM(0)));

	PG_RETURN_VOID();

}
