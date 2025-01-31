#include "postgres.h"
#include "funcapi.h"
#include "fmgr.h"
#include "access/htup_details.h"
#include "storage/shmem.h"
#include "utils/memutils.h"
#include "utils/timestamp.h"
#include "storage/lwlock.h"
#include "miscadmin.h"
#include "lib/stringinfo.h"
#include "catalog/pg_type.h"
#include "utils/builtins.h"
#include "utils/date.h"
#include "utils/numeric.h"

#if PG_VERSION_NUM >= 140000

#include "utils/wait_event.h"

#elif PG_VERSION_NUM >= 130000

#include "pgstat.h"

#endif

#include "shmmc.h"
#include "pipe.h"
#include "orafce.h"
#include "builtins.h"

#include <string.h>

/*
 * @ Pavel Stehule 2006-2023
 */

#ifndef _GetCurrentTimestamp
#define _GetCurrentTimestamp()	GetCurrentTimestamp()
#endif

#ifndef GetNowFloat
#ifdef HAVE_INT64_TIMESTAMP
#define GetNowFloat()   ((float8) _GetCurrentTimestamp() / 1000000.0)
#else
#define GetNowFloat()   _GetCurrentTimestamp()
#endif
#endif

#define RESULT_DATA			0
#define RESULT_TIMEOUT		1

/* in sec 1000 days */
#define MAXWAIT		86400000


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
	long		identity;
	bool		is_valid;
	bool		registered;
	char	   *pipe_name;
	char	   *creator;
	Oid			uid;
	struct _queue_item *items;
	struct _queue_item *last_item;
	int16		count;
	int16		limit;
	int			size;
} orafce_pipe;

typedef struct {
	int32 size;
	message_data_type type;
	Oid tupType;
} message_data_item;

typedef struct {
	int32 size;
	int32 items_count;
	message_data_item *next;
} message_buffer;

#define message_buffer_size		(MAXALIGN(sizeof(message_buffer)))
#define message_buffer_get_content(buf)	((message_data_item *) (((char*)buf)+message_buffer_size))


#define message_data_item_size	(MAXALIGN(sizeof(message_data_item)))
#define message_data_get_content(msg) (((char *)msg) + message_data_item_size)
#define message_data_item_next(msg) \
	((message_data_item *) (message_data_get_content(msg) + MAXALIGN(msg->size)))

typedef struct PipesFctx {
	int pipe_nth;
} PipesFctx;

typedef struct
{
	int			tranche_id;
	LWLock		shmem_lock;

	orafce_pipe *pipes;
	alert_event *events;
	alert_lock *locks;

#if PG_VERSION_NUM >= 130000

	ConditionVariable pipe_cv;
	ConditionVariable alert_cv;

#endif

	size_t		size;
	int			sid;
	long		identity_seq;
	vardata		data[1]; /* flexible array member */
} sh_memory;

#define sh_memory_size			(offsetof(sh_memory, data))

message_buffer *output_buffer = NULL;
message_buffer *input_buffer = NULL;

orafce_pipe* pipes = NULL;

long	   *identity_seq = NULL;

#define NOT_INITIALIZED		NULL

LWLockId shmem_lockid = NOT_INITIALIZED;

int sid;                                 /* session id */

extern alert_event *events;
extern alert_lock  *locks;

#if PG_VERSION_NUM >= 130000

ConditionVariable *pipe_cv = NULL;
ConditionVariable *alert_cv = NULL;

#endif

/*
 * write on writer size bytes from ptr
 */
static void
pack_field(message_buffer *buffer, message_data_type type,
			int32 size, void *ptr, Oid tupType)
{
	int			len;
	message_data_item *message;

	len = MAXALIGN(size) + message_data_item_size;
	if (MAXALIGN(buffer->size) + len > LOCALMSGSZ - message_buffer_size)
		ereport(ERROR,
				(errcode(ERRCODE_OUT_OF_MEMORY),
				 errmsg("out of memory"),
				 errdetail("Packed message is bigger than local buffer."),
				 errhint("Increase LOCALMSGSZ in 'pipe.h' and recompile library.")));

	if (buffer->next == NULL)
		buffer->next =  message_buffer_get_content(buffer);

	message = buffer->next;

	message->size = size;
	message->type = type;
	message->tupType = tupType;

	/* padding bytes have to be zeroed - buffer creator is responsible to clear memory */

	memcpy(message_data_get_content(message), ptr, size);

	buffer->size += len;
	buffer->items_count++;
	buffer->next = message_data_item_next(message);
}

static void*
unpack_field(message_buffer *buffer, message_data_type *type,
				int32 *size, Oid *tupType)
{
	void	   *ptr;
	message_data_item *message;

	Assert(buffer);
	Assert(buffer->items_count > 0);
	Assert(buffer->next);

	message = buffer->next;

	Assert(message);

	*size = message->size;
	*type = message->type;
	*tupType = message->tupType;
	ptr = message_data_get_content(message);

	buffer->next = --buffer->items_count > 0 ? message_data_item_next(message) : NULL;

	return ptr;
}

/*
 * Add ptr to queue. If pipe doesn't exist, register new pipe
 */
bool
ora_lock_shmem(size_t size, int max_pipes, int max_events, int max_locks, bool reset)
{
	bool		found;

	/* reset is always false, really */
	Assert(!reset);

	if (pipes == NULL)
	{
		sh_memory  *sh_mem;

		LWLockAcquire(AddinShmemInitLock, LW_EXCLUSIVE);
		sh_mem = ShmemInitStruct("dbms_pipe", size, &found);
		if (!found)
		{
			int			i;

			sh_mem->tranche_id = LWLockNewTrancheId();
			LWLockInitialize(&sh_mem->shmem_lock, sh_mem->tranche_id);

			LWLockRegisterTranche(sh_mem->tranche_id, "orafce");
			shmem_lockid = &sh_mem->shmem_lock;

			sh_mem->identity_seq = 0;

			sh_mem->size = size - sh_memory_size;
			ora_sinit(sh_mem->data, size, true);
			pipes = sh_mem->pipes = ora_salloc(max_pipes*sizeof(orafce_pipe));
			sid = sh_mem->sid = 1;

			for (i = 0; i < max_pipes; i++)
				pipes[i].is_valid = false;

			events = sh_mem->events = ora_salloc(max_events*sizeof(alert_event));
			locks = sh_mem->locks = ora_salloc(max_locks*sizeof(alert_lock));

			for (i = 0; i < max_events; i++)
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

#if PG_VERSION_NUM >= 130000

			ConditionVariableInit(&sh_mem->pipe_cv);
			ConditionVariableInit(&sh_mem->alert_cv);

			pipe_cv = &sh_mem->pipe_cv;
			alert_cv = &sh_mem->alert_cv;

#endif

			identity_seq = &sh_mem->identity_seq;
		}
		else
		{
			LWLockRegisterTranche(sh_mem->tranche_id, "orafce");
			shmem_lockid = &sh_mem->shmem_lock;

#if PG_VERSION_NUM >= 130000

			pipe_cv = &sh_mem->pipe_cv;
			alert_cv = &sh_mem->alert_cv;

#endif

			pipes = sh_mem->pipes;
			ora_sinit(sh_mem->data, sh_mem->size, false);
			sid = ++(sh_mem->sid);
			events = sh_mem->events;
			locks = sh_mem->locks;

			identity_seq = &sh_mem->identity_seq;
		}

		LWLockRelease(AddinShmemInitLock);
	}

	Assert(pipes != NULL);
	LWLockAcquire(shmem_lockid, LW_EXCLUSIVE);
	return true;
}

#define		NOT_ASSIGNED_IDENTITY			-1

/*
 * can be enhanced access/hash.h
 */
static orafce_pipe*
find_pipe(text* pipe_name,
		  bool* created,
		  bool only_check,
		  long *expected_identity,
		  bool *identity_alarm)
{
	int			i;
	orafce_pipe *result = NULL;

	*created = false;

	Assert(!expected_identity || identity_alarm);

	if (identity_alarm)
		*identity_alarm = false;

	for (i = 0; i < MAX_PIPES; i++)
	{
		if (pipes[i].is_valid &&
			strncmp((char*)VARDATA(pipe_name), pipes[i].pipe_name, VARSIZE(pipe_name) - VARHDRSZ) == 0
			&& (strlen(pipes[i].pipe_name) == (VARSIZE(pipe_name) - VARHDRSZ)))
		{
			if (expected_identity && *expected_identity >= 0
				&& pipes[i].identity != *expected_identity)
			{
				*identity_alarm = true;
				return result;
			}

			/* check owner if non public pipe */
			if (pipes[i].creator != NULL && pipes[i].uid != GetUserId())
			{
				ereport(ERROR,
						(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
						 errmsg("insufficient privilege"),
						 errdetail("Insufficient privilege to access pipe")));
			}

			if (expected_identity)
				*expected_identity = pipes[i].identity;

			return &pipes[i];
		}
	}

	if (only_check)
		return result;

	if (expected_identity && *expected_identity >= 0)
	{
		*identity_alarm = true;
		return result;
	}

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

				if (expected_identity)
					*expected_identity = pipes[i].identity = *identity_seq++;
			}
			break;
		}

	return result;
}

static bool
new_last(orafce_pipe *p, void *ptr, size_t size)
{
	queue_item *aux_q;

	if (p->count >= p->limit && p->limit != -1)
		return false;

	if (p->limit == -1 &&
		p->count > 0 &&
		(p->size + size + sizeof(queue_item) > 8 * 1024))
		return false;

	if (p->items == NULL)
	{
		if (NULL == (p->items = ora_salloc(sizeof(queue_item))))
			return false;
		p->items->next_item = NULL;
		p->items->ptr = ptr;
		p->last_item = p->items;
		p->count = 1;
		return true;
	}

	if (NULL == (aux_q = ora_salloc(sizeof(queue_item))))
		return false;

	p->last_item->next_item = aux_q;
	p->last_item = aux_q;

	aux_q->next_item = NULL;
	aux_q->ptr = ptr;

	p->count += 1;

	return true;
}

static void*
remove_first(orafce_pipe *p, bool *found)
{
	struct _queue_item *q;
	void	   *ptr = NULL;

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

			if (p->creator)
			{
				ora_sfree(p->creator);
				p->creator = NULL;
			}

			p->is_valid = false;
		}

	}

	return ptr;
}

/* copy message to local memory, if exists */
static message_buffer*
get_from_pipe(text *pipe_name,
			  bool *found,
			  long *identity, bool *identity_alarm)
{
	orafce_pipe *p;
	bool		created;
	message_buffer *result = NULL;

	if (!ora_lock_shmem(SHMEMMSGSZ, MAX_PIPES, MAX_EVENTS, MAX_LOCKS, false))
		return NULL;

	if (NULL != (p = find_pipe(pipe_name, &created, false, identity, identity_alarm)))
	{
		if (!created)
		{
			message_buffer *shm_msg;

			if (NULL != (shm_msg = remove_first(p, found)))
			{
				p->size -= shm_msg->size;

				result = (message_buffer*) MemoryContextAlloc(TopMemoryContext, shm_msg->size);
				memcpy(result, shm_msg, shm_msg->size);
				ora_sfree(shm_msg);
			}
		}
	}

	LWLockRelease(shmem_lockid);

	return result;
}

/*
 * if ptr is null, then only register pipe
 */
static bool
add_to_pipe(text *pipe_name,
			message_buffer *ptr,
			int limit, bool limit_is_valid,
			long *identity, bool *identity_alarm)
{
	bool		created;
	bool		result = false;
	message_buffer *sh_ptr;

	if (!ora_lock_shmem(SHMEMMSGSZ, MAX_PIPES, MAX_EVENTS, MAX_LOCKS,false))
		return false;

	for (;;)
	{
		orafce_pipe *p;

		if (NULL != (p = find_pipe(pipe_name, &created, false, identity, identity_alarm)))
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
					if (new_last(p, sh_ptr, ptr->size))
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
	LWLockRelease(shmem_lockid);
	return result;
}

static void
remove_pipe(text *pipe_name, bool purge)
{
	orafce_pipe *p;
	bool		created;

	if (NULL != (p = find_pipe(pipe_name, &created, true, NULL, NULL)))
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

			if (p->creator)
			{
				ora_sfree(p->creator);
				p->creator = NULL;
			}
		}
	}
}

Datum
dbms_pipe_next_item_type (PG_FUNCTION_ARGS)
{
	PG_RETURN_INT32(input_buffer != NULL ? input_buffer->next->type : IT_NO_MORE_ITEMS);
}

static void
reset_buffer(message_buffer *buffer, int32 size)
{
	memset(buffer, 0, size);
	buffer->size = message_buffer_size;
	buffer->items_count = 0;
	buffer->next = message_buffer_get_content(buffer);
}

static message_buffer*
check_buffer(message_buffer *buffer, int32 size)
{
	if (buffer == NULL)
	{
		buffer = (message_buffer*) MemoryContextAlloc(TopMemoryContext, size);
		if (buffer == NULL)
			ereport(ERROR,
					(errcode(ERRCODE_OUT_OF_MEMORY),
					 errmsg("out of memory"),
					 errdetail("Failed while allocation block %d bytes in memory.", size)));

		reset_buffer(buffer, size);
	}

	return buffer;
}

Datum
dbms_pipe_pack_message_text(PG_FUNCTION_ARGS)
{
	text	   *str = PG_GETARG_TEXT_PP(0);

	output_buffer = check_buffer(output_buffer, LOCALMSGSZ);
	pack_field(output_buffer, IT_VARCHAR,
		VARSIZE_ANY_EXHDR(str), VARDATA_ANY(str), InvalidOid);

	PG_RETURN_VOID();
}

Datum
dbms_pipe_pack_message_date(PG_FUNCTION_ARGS)
{
	DateADT	dt = PG_GETARG_DATEADT(0);

	output_buffer = check_buffer(output_buffer, LOCALMSGSZ);
	pack_field(output_buffer, IT_DATE,
			   sizeof(dt), &dt, InvalidOid);

	PG_RETURN_VOID();
}

Datum
dbms_pipe_pack_message_timestamp(PG_FUNCTION_ARGS)
{
	TimestampTz dt = PG_GETARG_TIMESTAMPTZ(0);

	output_buffer = check_buffer(output_buffer, LOCALMSGSZ);
	pack_field(output_buffer, IT_TIMESTAMPTZ,
			   sizeof(dt), &dt, InvalidOid);

	PG_RETURN_VOID();
}

Datum
dbms_pipe_pack_message_number(PG_FUNCTION_ARGS)
{
	Numeric	num = PG_GETARG_NUMERIC(0);

	output_buffer = check_buffer(output_buffer, LOCALMSGSZ);
	pack_field(output_buffer, IT_NUMBER,
			   VARSIZE(num) - VARHDRSZ, VARDATA(num), InvalidOid);

	PG_RETURN_VOID();
}

Datum
dbms_pipe_pack_message_bytea(PG_FUNCTION_ARGS)
{
	bytea	   *data = PG_GETARG_BYTEA_P(0);

	output_buffer = check_buffer(output_buffer, LOCALMSGSZ);
	pack_field(output_buffer, IT_BYTEA,
		VARSIZE_ANY_EXHDR(data), VARDATA_ANY(data), InvalidOid);

	PG_RETURN_VOID();
}

static void
init_args_3(FunctionCallInfo info, Datum arg0, Datum arg1, Datum arg2)
{
#if PG_VERSION_NUM >= 120000

	info->args[0].value = arg0;
	info->args[1].value = arg1;
	info->args[2].value = arg2;
	info->args[0].isnull = false;
	info->args[1].isnull = false;
	info->args[2].isnull = false;

#else

	info->arg[0] = arg0;
	info->arg[1] = arg1;
	info->arg[2] = arg2;
	info->argnull[0] = false;
	info->argnull[1] = false;
	info->argnull[2] = false;

#endif
}

/*
 *  We can serialize only typed record
 */
Datum
dbms_pipe_pack_message_record(PG_FUNCTION_ARGS)
{
	HeapTupleHeader rec = PG_GETARG_HEAPTUPLEHEADER(0);
	Oid			tupType;
	bytea	   *data;

#if PG_VERSION_NUM >= 120000

	LOCAL_FCINFO(info, 3);

#else

	FunctionCallInfoData info_data;
	FunctionCallInfo info = &info_data;

#endif

	tupType = HeapTupleHeaderGetTypeId(rec);

	/*
	 * Normally one would call record_send() using DirectFunctionCall3,
	 * but that does not work since record_send wants to cache some data
	 * using fcinfo->flinfo->fn_extra.  So we need to pass it our own
	 * flinfo parameter.
	 */
	InitFunctionCallInfoData(*info, fcinfo->flinfo, 3, InvalidOid, NULL, NULL);
	init_args_3(info, PointerGetDatum(rec), ObjectIdGetDatum(tupType), Int32GetDatum(-1));

	data = (bytea*) DatumGetPointer(record_send(info));

	output_buffer = check_buffer(output_buffer, LOCALMSGSZ);
	pack_field(output_buffer, IT_RECORD,
			   VARSIZE(data), VARDATA(data), tupType);

	PG_RETURN_VOID();
}

static Datum
dbms_pipe_unpack_message(PG_FUNCTION_ARGS, message_data_type dtype)
{
	Oid			tupType;
	void	   *ptr;
	int32		size;
	Datum		result;
	message_data_type next_type;
	message_data_type type;

	if (input_buffer == NULL ||
		input_buffer->items_count <= 0 ||
		input_buffer->next == NULL ||
		input_buffer->next->type == IT_NO_MORE_ITEMS)
		PG_RETURN_NULL();

	next_type = input_buffer->next->type;
	if (next_type != dtype)
		ereport(ERROR,
				(errcode(ERRCODE_DATATYPE_MISMATCH),
				 errmsg("datatype mismatch"),
				 errdetail("unpack unexpected type: %d", next_type)));

	ptr = unpack_field(input_buffer, &type, &size, &tupType);
	Assert(ptr != NULL);

	switch (type)
	{
		case IT_TIMESTAMPTZ:
			result = TimestampTzGetDatum(*(TimestampTz*)ptr);
			break;
		case IT_DATE:
			result = DateADTGetDatum(*(DateADT*)ptr);
			break;
		case IT_VARCHAR:
		case IT_NUMBER:
		case IT_BYTEA:
			result = PointerGetDatum(cstring_to_text_with_len(ptr, size));
			break;
		case IT_RECORD:
		{
#if PG_VERSION_NUM >= 120000

			LOCAL_FCINFO(info, 3);

#else

			FunctionCallInfoData info_data;
			FunctionCallInfo info = &info_data;

#endif

			StringInfoData	buf;
			text		   *data = cstring_to_text_with_len(ptr, size);

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
			InitFunctionCallInfoData(*info, fcinfo->flinfo, 3, InvalidOid, NULL, NULL);
			init_args_3(info, PointerGetDatum(&buf), ObjectIdGetDatum(tupType), Int32GetDatum(-1));

			result = record_recv(info);
			break;
		}
		default:
			elog(ERROR, "unexpected type: %d", type);
			result = (Datum) 0;	/* keep compiler quiet */
	}

	if (input_buffer->items_count == 0)
	{
		pfree(input_buffer);
		input_buffer = NULL;
	}

	PG_RETURN_DATUM(result);
}

Datum
dbms_pipe_unpack_message_text(PG_FUNCTION_ARGS)
{
	return dbms_pipe_unpack_message(fcinfo, IT_VARCHAR);
}

Datum
dbms_pipe_unpack_message_date(PG_FUNCTION_ARGS)
{
	return dbms_pipe_unpack_message(fcinfo, IT_DATE);
}

Datum
dbms_pipe_unpack_message_timestamp(PG_FUNCTION_ARGS)
{
	return dbms_pipe_unpack_message(fcinfo, IT_TIMESTAMPTZ);
}

Datum
dbms_pipe_unpack_message_number(PG_FUNCTION_ARGS)
{
	return dbms_pipe_unpack_message(fcinfo, IT_NUMBER);
}

Datum
dbms_pipe_unpack_message_bytea(PG_FUNCTION_ARGS)
{
	return dbms_pipe_unpack_message(fcinfo, IT_BYTEA);
}

Datum
dbms_pipe_unpack_message_record(PG_FUNCTION_ARGS)
{
	return dbms_pipe_unpack_message(fcinfo, IT_RECORD);
}


#define WATCH_PRE(t, et, c) \
et = GetNowFloat() + (float8)t; c = 0; (void) c;\
do \
{

#define WATCH_TM_POST(t,et,c) \
if (GetNowFloat() >= et) \
PG_RETURN_INT32(RESULT_TIMEOUT); \
if (cycle++ % 100 == 0) \
CHECK_FOR_INTERRUPTS(); \
pg_usleep(10000L); \
} while(true && t != 0);


Datum
dbms_pipe_receive_message(PG_FUNCTION_ARGS)
{
	text	   *pipe_name = NULL;
	int			timeout;
	bool		found = false;
	instr_time	start_time;
	int32		result = RESULT_TIMEOUT;
	long		identity = NOT_ASSIGNED_IDENTITY;
	bool		identity_alarm;

#if PG_VERSION_NUM < 130000

	long		cycle = 0;

#endif

	if (PG_ARGISNULL(0))
		ereport(ERROR,
				(errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
				 errmsg("pipe name is NULL"),
				 errdetail("Pipename may not be NULL.")));
	else
		pipe_name = PG_GETARG_TEXT_P(0);

	if (!PG_ARGISNULL(1))
	{
		timeout = PG_GETARG_INT32(1);
		if (timeout < 0)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("negative timeout is not allowed")));

		if (timeout > MAXWAIT)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("timeout is too large (maximum: %d)", MAXWAIT)));
	}
	else
		timeout = MAXWAIT;

	if (input_buffer)
	{
		pfree(input_buffer);
		input_buffer = NULL;
	}

	INSTR_TIME_SET_CURRENT(start_time);

	for (;;)
	{
		input_buffer = get_from_pipe(pipe_name, &found, &identity, &identity_alarm);
		if (found)
		{
			if (input_buffer)
				input_buffer->next = message_buffer_get_content(input_buffer);

			result = RESULT_DATA;
			break;
		}

		if (identity_alarm)
			break;

		if (timeout > 0)
		{
			instr_time	cur_time;
			long		cur_timeout;

			INSTR_TIME_SET_CURRENT(cur_time);
			INSTR_TIME_SUBTRACT(cur_time, start_time);

			cur_timeout = timeout * 1000L - (long) INSTR_TIME_GET_MILLISEC(cur_time);
			if (cur_timeout <= 0)
				break;

#if PG_VERSION_NUM >= 130000

			/*
			 * Timeout should be less than INT_MAX, but we set 1 sec as protection
			 * against deadlocks.
			 */
			if (cur_timeout > 1000)
				cur_timeout = 1000;

			if (ConditionVariableTimedSleep(pipe_cv, cur_timeout, PG_WAIT_EXTENSION))
			{
				/* exit on timeout */
				INSTR_TIME_SET_CURRENT(cur_time);
				INSTR_TIME_SUBTRACT(cur_time, start_time);

				cur_timeout = timeout * 1000L - (long) INSTR_TIME_GET_MILLISEC(cur_time);
				if (cur_timeout <= 0)
					break;
			}

#else

			if (cycle++ % 10)
				CHECK_FOR_INTERRUPTS();

			pg_usleep(10000L);

			/* exit on timeout */
			INSTR_TIME_SET_CURRENT(cur_time);
			INSTR_TIME_SUBTRACT(cur_time, start_time);

			cur_timeout = timeout * 1000L - (long) INSTR_TIME_GET_MILLISEC(cur_time);
			if (cur_timeout <= 0)
				break;

#endif

		}
		else
			break;
	}

#if PG_VERSION_NUM >= 130000

	ConditionVariableCancelSleep();

	if (result == RESULT_DATA)
		ConditionVariableBroadcast(pipe_cv);

#endif

	PG_RETURN_INT32(result);
}

Datum
dbms_pipe_send_message(PG_FUNCTION_ARGS)
{
	text	   *pipe_name = NULL;
	int			timeout;
	int			limit = 0;
	bool		valid_limit;
	instr_time	start_time;
	int32		result = RESULT_TIMEOUT;
	long		identity = NOT_ASSIGNED_IDENTITY;
	bool		identity_alarm;

#if PG_VERSION_NUM < 130000

	long		cycle = 0;

#endif

	if (PG_ARGISNULL(0))
		ereport(ERROR,
				(errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
				 errmsg("pipe name is NULL"),
				 errdetail("Pipename may not be NULL.")));
	else
		pipe_name = PG_GETARG_TEXT_P(0);

	output_buffer = check_buffer(output_buffer, LOCALMSGSZ);

	if (!PG_ARGISNULL(1))
	{
		timeout = PG_GETARG_INT32(1);
		if (timeout < 0)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("negative timeout is not allowed")));

		if (timeout > MAXWAIT)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("timeout is too large (maximum: %d)", MAXWAIT)));
	}
	else
		timeout = MAXWAIT;

	if (PG_ARGISNULL(2))
		valid_limit = false;
	else
	{
		limit = PG_GETARG_INT32(2);
		valid_limit = true;
	}

	INSTR_TIME_SET_CURRENT(start_time);

	for (;;)
	{
		if (add_to_pipe(pipe_name, output_buffer,
						limit, valid_limit,
						&identity, &identity_alarm))
		{
			result = RESULT_DATA;
			break;
		}

		if (identity_alarm)
			break;

		if (timeout > 0)
		{
			instr_time	cur_time;
			long		cur_timeout;

			INSTR_TIME_SET_CURRENT(cur_time);
			INSTR_TIME_SUBTRACT(cur_time, start_time);

			cur_timeout = timeout * 1000L - (long) INSTR_TIME_GET_MILLISEC(cur_time);
			if (cur_timeout <= 0)
				break;

#if PG_VERSION_NUM >= 130000

			if (cur_timeout > 1000)
				cur_timeout = 1000;

			if (ConditionVariableTimedSleep(pipe_cv, cur_timeout, PG_WAIT_EXTENSION))
			{
				/* exit on timeout */
				INSTR_TIME_SET_CURRENT(cur_time);
				INSTR_TIME_SUBTRACT(cur_time, start_time);

				cur_timeout = timeout * 1000L - (long) INSTR_TIME_GET_MILLISEC(cur_time);
				if (cur_timeout <= 0)
					break;
			}

#else

			if (cycle++ % 10)
				CHECK_FOR_INTERRUPTS();

			pg_usleep(10000L);

			/* exit on timeout */
			INSTR_TIME_SET_CURRENT(cur_time);
			INSTR_TIME_SUBTRACT(cur_time, start_time);

			cur_timeout = timeout * 1000L - (long) INSTR_TIME_GET_MILLISEC(cur_time);
			if (cur_timeout <= 0)
				break;

#endif

		}
		else
			break;
	}

#if PG_VERSION_NUM >= 130000

	ConditionVariableCancelSleep();

	if (result == RESULT_DATA)
		ConditionVariableBroadcast(pipe_cv);

#endif

	reset_buffer(output_buffer, LOCALMSGSZ);

	PG_RETURN_INT32(result);
}

Datum
dbms_pipe_unique_session_name(PG_FUNCTION_ARGS)
{
	StringInfoData strbuf;
	float8		endtime;
	int			cycle = 0;
	int			timeout = 10;

	WATCH_PRE(timeout, endtime, cycle);
	if (ora_lock_shmem(SHMEMMSGSZ, MAX_PIPES,MAX_EVENTS,MAX_LOCKS,false))
	{
		text	   *result;

		initStringInfo(&strbuf);
		appendStringInfo(&strbuf,"PG$PIPE$%d$%d",sid, MyProcPid);

		result = cstring_to_text_with_len(strbuf.data, strbuf.len);
		pfree(strbuf.data);
		LWLockRelease(shmem_lockid);

		PG_RETURN_TEXT_P(result);
	}
	WATCH_TM_POST(timeout, endtime, cycle);
	LOCK_ERROR();

	PG_RETURN_NULL();
}

#define DB_PIPES_COLS		6

Datum
dbms_pipe_list_pipes(PG_FUNCTION_ARGS)
{
	FuncCallContext *funcctx;
	TupleDesc	tupdesc;
	AttInMetadata *attinmeta;
	PipesFctx  *fctx;
	float8		endtime;
	int			cycle;
	int			timeout = 10;

	if (SRF_IS_FIRSTCALL())
	{
		int		i;
		MemoryContext  oldcontext;
		bool has_lock = false;

		WATCH_PRE(timeout, endtime, cycle);
		if (ora_lock_shmem(SHMEMMSGSZ, MAX_PIPES, MAX_EVENTS, MAX_LOCKS, false))
		{
			has_lock = true;
			break;
		}
		WATCH_TM_POST(timeout, endtime, cycle);
		if (!has_lock)
			LOCK_ERROR();

		funcctx = SRF_FIRSTCALL_INIT();
		oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

		fctx = palloc(sizeof(PipesFctx));
		funcctx->user_fctx = fctx;
		fctx->pipe_nth = 0;

#if PG_VERSION_NUM >= 120000

		tupdesc = CreateTemplateTupleDesc(DB_PIPES_COLS);

#else

		tupdesc = CreateTemplateTupleDesc(DB_PIPES_COLS, false);

#endif

		i = 0;
		TupleDescInitEntry(tupdesc, ++i, "name",    VARCHAROID, -1, 0);
		TupleDescInitEntry(tupdesc, ++i, "items",   INT4OID,    -1, 0);
		TupleDescInitEntry(tupdesc, ++i, "size",    INT4OID,    -1, 0);
		TupleDescInitEntry(tupdesc, ++i, "limit",   INT4OID,    -1, 0);
		TupleDescInitEntry(tupdesc, ++i, "private", BOOLOID,    -1, 0);
		TupleDescInitEntry(tupdesc, ++i, "owner",   VARCHAROID, -1, 0);
		Assert(i == DB_PIPES_COLS);

		attinmeta = TupleDescGetAttInMetadata(tupdesc);
		funcctx->attinmeta = attinmeta;

		MemoryContextSwitchTo(oldcontext);
	}

	funcctx = SRF_PERCALL_SETUP();
	fctx = (PipesFctx *) funcctx->user_fctx;

	while (fctx->pipe_nth < MAX_PIPES)
	{
		if (pipes[fctx->pipe_nth].is_valid)
		{
			Datum		result;
			HeapTuple	tuple;
			char	   *values[DB_PIPES_COLS];
			char		items[16];
			char		size[16];
			char		limit[16];

			/* name */
			values[0] = pipes[fctx->pipe_nth].pipe_name;
			/* items */
			snprintf(items, lengthof(items), "%d", pipes[fctx->pipe_nth].count);
			values[1] = items;
			/* items */
			snprintf(size, lengthof(size), "%d", pipes[fctx->pipe_nth].size);
			values[2] = size;
			/* limit */
			if (pipes[fctx->pipe_nth].limit != -1)
			{
				snprintf(limit, lengthof(limit), "%d", pipes[fctx->pipe_nth].limit);
				values[3] = limit;
			}
			else
				values[3] = NULL;
			/* private */
			values[4] = (pipes[fctx->pipe_nth].creator ? "true" : "false");
			/* owner */
			values[5] = pipes[fctx->pipe_nth].creator;

			tuple = BuildTupleFromCStrings(funcctx->attinmeta, values);
			result = HeapTupleGetDatum(tuple);

			fctx->pipe_nth += 1;
			SRF_RETURN_NEXT(funcctx, result);
		}
		fctx->pipe_nth += 1;
	}

	LWLockRelease(shmem_lockid);
	SRF_RETURN_DONE(funcctx);
}

/*
 * secondary functions
 */

/*
 * Registration explicit pipes
 *   dbms_pipe.create_pipe(pipe_name varchar, limit := -1 int, private := false bool);
 */
Datum
dbms_pipe_create_pipe(PG_FUNCTION_ARGS)
{
	text	   *pipe_name = NULL;
	int			limit = 0;
	bool		is_private;
	bool		limit_is_valid = false;
	bool		created;
	float8		endtime;
	int			cycle;
	int			timeout = 10;

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
		orafce_pipe *p;
		if (NULL != (p = find_pipe(pipe_name, &created, false, NULL, NULL)))
		{
			if (!created)
			{
				ereport(ERROR,
						(errcode(ERRCODE_DUPLICATE_OBJECT),
						 errmsg("pipe creation error"),
						 errdetail("Pipe is registered.")));
			}
			if (is_private)
			{
				char	   *user;

				p->uid = GetUserId();

				user = (char*)DirectFunctionCall1(namein,
					    CStringGetDatum(GetUserNameFromId(p->uid, false)));

				p->creator = ora_sstrcpy(user);
				pfree(user);
			}
			p->limit = limit_is_valid ? limit : -1;
			p->registered = true;

			LWLockRelease(shmem_lockid);
			PG_RETURN_VOID();
		}
	}
	WATCH_TM_POST(timeout, endtime, cycle);
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

	PG_RETURN_VOID();
}

/*
 * Remove all stored messages in pipe. Remove implicit created
 * pipe.
 */
Datum
dbms_pipe_purge(PG_FUNCTION_ARGS)
{
	text	   *pipe_name = PG_GETARG_TEXT_P(0);
	float8		endtime;
	int			cycle = 0;
	int			timeout = 10;

	WATCH_PRE(timeout, endtime, cycle);
	if (ora_lock_shmem(SHMEMMSGSZ, MAX_PIPES,MAX_EVENTS,MAX_LOCKS,false))
	{

		remove_pipe(pipe_name, true);
		LWLockRelease(shmem_lockid);

		PG_RETURN_VOID();
	}
	WATCH_TM_POST(timeout, endtime, cycle);
	LOCK_ERROR();

#if PG_VERSION_NUM >= 130000

	ConditionVariableBroadcast(pipe_cv);

#endif

	PG_RETURN_VOID();
}

/*
 * Remove pipe if exists
 */
Datum
dbms_pipe_remove_pipe(PG_FUNCTION_ARGS)
{
	text	   *pipe_name = PG_GETARG_TEXT_P(0);
	float8		endtime;
	int			cycle = 0;
	int			timeout = 10;

	WATCH_PRE(timeout, endtime, cycle);
	if (ora_lock_shmem(SHMEMMSGSZ, MAX_PIPES,MAX_EVENTS,MAX_LOCKS,false))
	{
		remove_pipe(pipe_name, false);
		LWLockRelease(shmem_lockid);

		PG_RETURN_VOID();
	}
	WATCH_TM_POST(timeout, endtime, cycle);
	LOCK_ERROR();

#if PG_VERSION_NUM >= 130000

	ConditionVariableBroadcast(pipe_cv);

#endif

	PG_RETURN_VOID();
}

/*
 * Some void udf which I can't wrap in sql
 */
Datum
dbms_pipe_create_pipe_2(PG_FUNCTION_ARGS)
{
	Datum	arg1;
	int		limit = -1;

	if (PG_ARGISNULL(0))
		ereport(ERROR,
				(errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
				 errmsg("pipe name is NULL"),
				 errdetail("Pipename may not be NULL.")));

	arg1 = PG_GETARG_DATUM(0);

	if (!PG_ARGISNULL(1))
		limit = PG_GETARG_INT32(1);

	DirectFunctionCall3(dbms_pipe_create_pipe,
						arg1,
						Int32GetDatum(limit),
						BoolGetDatum(false));

	PG_RETURN_VOID();
}

Datum
dbms_pipe_create_pipe_1(PG_FUNCTION_ARGS)
{
	Datum	arg1;

	if (PG_ARGISNULL(0))
		ereport(ERROR,
				(errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
				 errmsg("pipe name is NULL"),
				 errdetail("Pipename may not be NULL.")));

	arg1 = PG_GETARG_DATUM(0);

	DirectFunctionCall3(dbms_pipe_create_pipe,
						arg1,
						(Datum) -1,
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
