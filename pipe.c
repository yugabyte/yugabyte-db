#include "postgres.h"
#include "fmgr.h"
#include "storage/shmem.h"
#include "utils/memutils.h"
#include "utils/timestamp.h"
#include "storage/lwlock.h"
#include "miscadmin.h"
#include "string.h"
#include "lib/stringinfo.h"
#include "orafunc.h"

#include "shmmc.h"

/*
 * First test version 0.0.2
 * @ Pavel Stehule 2006
 */

#define LOCALMSGSZ (4*1024)
#define SHMEMMSGSZ (30*1024)
#define MAX_PIPES  30

#ifndef GetNowFloat
#ifdef HAVE_INT64_TIMESTAMP
#define GetNowFloat()   ((float8) GetCurrentTimestamp() / 1000000.0)
#else
#define GetNowFloat()   GetCurrentTimestamp()
#endif
#endif

#define RESULT_DATA	0
#define RESULT_WAIT	1

Datum dbms_pipe_pack_message_text(PG_FUNCTION_ARGS);
Datum dbms_pipe_unpack_message_text(PG_FUNCTION_ARGS);
Datum dbms_pipe_send_message(PG_FUNCTION_ARGS);
Datum dbms_pipe_receive_message(PG_FUNCTION_ARGS);
Datum dbms_pipe_unique_session_name (PG_FUNCTION_ARGS);


typedef struct _queue_item {
	void *ptr;
	struct _queue_item *next_item;
} queue_item;


typedef struct {
	bool is_valid;
	bool registered;
	char *pipe_name;
	struct _queue_item *items;
	int count;
	int limit;
	int size;
} pipe;

#define NOT_INITIALIZED -1


pipe* pipes = NULL;
LWLockId shmem_lock = NOT_INITIALIZED;

typedef struct {
	size_t size;
	int items_count;
	char data;
} message_buffer;


typedef enum {
	IT_NO_MORE_ITEMS = 0,
	IT_NUMBER = 9, 
	IT_VARCHAR = 11,
	IT_DATE = 12,
	IT_BYTEA = 23
} message_data_type;

typedef struct {
	message_data_type type;
	size_t size;
	char data;
} message_data_item;


typedef struct
{
    LWLockId shmem_lock;
	pipe *pipes;
	size_t size;
	char data[];
} sh_memory;

message_buffer *output_buffer = NULL;
message_buffer *input_buffer = NULL;

message_data_item *writer = NULL;
message_data_item *reader = NULL;

static void 
pack_field(message_buffer *message, message_data_item **writer,
		   message_data_type type, int size, void *ptr)
{
	int l;
	message_data_item *_wr = *writer;

	l = size + sizeof(message_data_item);
	if (message->size + l > LOCALMSGSZ-sizeof(message_buffer))
		elog(ERROR, "Full output buffer");

	if (_wr == NULL)
		_wr = (message_data_item*)&message->data;

	_wr->size = l;
	_wr->type = type;
	memcpy(&_wr->data, ptr, size);

	message->size += l;
	message->items_count +=1;

	_wr = (message_data_item*)((char*)_wr + l);
	*writer = _wr;
}


static void*
unpack_field(message_buffer *message, message_data_item **reader, 
			 message_data_type *type, size_t *size)
{
	void *ptr;
	message_data_item *_rd = *reader;

	if (_rd == NULL)
		_rd = (message_data_item*)&message->data;
	if ((message->items_count)--)
	{
		*size = _rd->size - sizeof(message_data_type);
		*type = _rd->type;
		ptr  = (void*)&_rd->data;

		_rd += _rd->size;
		*reader = message->items_count > 0 ? _rd : NULL;

		return ptr;
	}

	return NULL;
}

/* na zacatek jedu jednoduse pres pole, predelat do hashe */

/*
 * Add ptr to queue. If pipe doesn't exist, regigister new pipe
 */

static bool
lock_shmem(size_t size, int max_pipes, bool reset)
{
	int i;
	bool found;

	sh_memory *sh_mem;

	if (pipes == NULL)
	{
		sh_mem = ShmemInitStruct("dbms_pipe",size,&found);
		if (sh_mem == NULL)
			elog(ERROR, "Can't to access shared memory");

		if (!found)
		{
			shmem_lock = sh_mem->shmem_lock = LWLockAssign();
			LWLockAcquire(sh_mem->shmem_lock, LW_EXCLUSIVE);
			sh_mem->size = size - sizeof(sh_memory);
			ora_sinit(&sh_mem->data, size, true);
			pipes = sh_mem->pipes = ora_salloc(max_pipes*sizeof(pipe));
			
			for(i = 0; i < max_pipes; i++)
				pipes[i].is_valid = false;
		}
		else if (sh_mem->shmem_lock != 0)
		{
			pipes = sh_mem->pipes;
			shmem_lock = sh_mem->shmem_lock;
			LWLockAcquire(sh_mem->shmem_lock, LW_EXCLUSIVE);
			ora_sinit(&sh_mem->data, sh_mem->size, reset); 
		}
	}
	else
		LWLockAcquire(shmem_lock, LW_EXCLUSIVE);
	
	if (reset && pipes == NULL)
		elog(ERROR, "Can't purge memory");

	return pipes != NULL;	
}

static pipe*
find_pipe(char* pipe_name, bool* created, bool only_check)
{
	int i;
	for (i = 0; i < MAX_PIPES; i++)
		if (strcmp(pipe_name, pipes[i].pipe_name) == 0 && pipes[i].is_valid)
			return &pipes[i];

	if (only_check)
		return NULL;

	for (i = 0; i < MAX_PIPES; i++)
		if (!pipes[i].is_valid)
		{
			if (NULL != (pipes[i].pipe_name = ora_sstrcpy(pipe_name)))
			{
				pipes[i].is_valid = true;
				pipes[i].registered = false;
				pipes[i].count = 0;
				pipes[i].limit = -1;

				return &pipes[i];
			}
			else
				return NULL;
		}
	
	return NULL;	
}

static bool
new_last(pipe *p, void *ptr)
{
	queue_item *q, *aux_q;

	if (p->count >= p->limit)
		return false;

	if (p->items == NULL)
	{
		if (NULL == (p->items = ora_salloc(sizeof(queue_item))))
			return false;
		p->items->next_item = NULL;
		p->items->ptr = ptr;
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

	p->items += 1;
	
	return true;
}


static void*
remove_first(pipe *p)
{
	struct _queue_item *q;
	void *ptr;

	if (NULL != (q = p->items))
	{
		p->items -= 1;
		ptr = q->ptr;
		p->items = q->next_item;
		
		ora_sfree(p);
		if (p->items == NULL && !p->registered)
		{
			ora_sfree(p->pipe_name);
			p->is_valid = false;
		}
		return ptr;
	}

	return NULL;
}

/* copy message to local memory, if exists */

static message_buffer*
get_from_pipe(char *name)
{
	pipe *p;
	bool created;
	message_buffer *shm_msg;
	message_buffer *result = NULL;

	if (!lock_shmem(SHMEMMSGSZ, MAX_PIPES,false))
		return NULL;

	if (NULL != (p = find_pipe(name, &created,false)))
	{
		if (!created)
		{
			if (NULL != (shm_msg = remove_first(p)))
				p->size -= result->size;
		

			if (shm_msg != NULL)
			{
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
add_to_pipe(char *name, message_buffer *ptr, int limit, bool limit_is_valid)
{
	pipe *p;
	bool created;
	bool result = false;
	message_buffer *sh_ptr;
	
	if (!lock_shmem(SHMEMMSGSZ, MAX_PIPES,false))
		return false;
		
	for (;;)
	{
		if (NULL != (p = find_pipe(name, &created, false)))
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
					else
						
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
remove_pipe(char *pipe_name, bool purge)
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
		}
		p->items = NULL;
		p->size = 0;
		p->count = 0;
		if (!purge)
		{
			ora_sfree(p->pipe_name);
			p->is_valid = false;
		}
	}
}


PG_FUNCTION_INFO_V1 (dbms_pipe_pack_message);

Datum
dbms_pipe_pack_message_text(PG_FUNCTION_ARGS)
{
	text *str = PG_GETARG_TEXT_P(0);
   
	if (output_buffer == NULL)
	{
		output_buffer = (message_buffer*) MemoryContextAlloc(TopMemoryContext, LOCALMSGSZ);
		output_buffer->size = 0;
		output_buffer->items_count = 0;
		writer = (message_data_item*) &output_buffer->data;
	}	

	pack_field(output_buffer, &writer, IT_VARCHAR, 
			   VARSIZE(str) - VARHDRSZ, VARDATA(str)); 
	PG_RETURN_VOID();
}


Datum
dbms_pipe_unpack_message_text(PG_FUNCTION_ARGS)
{
	text *result;
	void *ptr; 
	message_data_type type; 
	size_t size;

	if (input_buffer == NULL)
		PG_RETURN_NULL();

	result = NULL;
	if (NULL != (ptr = unpack_field(input_buffer, &reader, 
									&type, &size)))
	{
		switch (type)
		{
			case IT_VARCHAR:
				result = ora_make_text_fix((char*)ptr, size);
				break;
			default:
				result = NULL;
		}
	}
	if (input_buffer->items_count == 0)
	{
		pfree(input_buffer);
		input_buffer = NULL;
	}
	if (result != NULL)
		PG_RETURN_TEXT_P(result);
	else
		PG_RETURN_NULL();			
}


#define WATCH_PRE(t, et, c) \
et = GetNowFloat() + (float8)t; c = 0; \
for (;;) \
{ \
if (GetNowFloat() > et) \
PG_RETURN_INT32(RESULT_WAIT); \
if (cycle++ % 100 == 0) \
   CHECK_FOR_INTERRUPTS(); 

#define WATCH_POST() \
     pg_usleep(10000L); \
   }


PG_FUNCTION_INFO_V1 (dbms_pipe_receive_message);

Datum
dbms_pipe_receive_message(PG_FUNCTION_ARGS)
{
    char *pipe_name = PG_GETARG_CSTRING(0); 
    int timeout = PG_GETARG_INT32(1);
    int cycle = 0;
    float8 endtime;

	if (input_buffer != NULL)
		pfree(input_buffer);
	input_buffer = NULL;
	reader = NULL;
  
    WATCH_PRE(timeout, endtime, cycle);	
	if (NULL != (input_buffer = get_from_pipe(pipe_name)))
	{
		reader = (message_data_item*)&input_buffer->data;
		break;
	}
	WATCH_POST();
	PG_RETURN_INT32(RESULT_DATA);
}


Datum
dbms_pipe_send_message(PG_FUNCTION_ARGS)
{
    char *pipe_name = PG_GETARG_CSTRING(0); 
    int timeout = PG_GETARG_INT32(1);
    int limit = PG_GETARG_INT32(2);
	bool valid_limit = true;

    int cycle = 0;
    float8 endtime;

	if (PG_ARGISNULL(2))
		valid_limit = false;

	if (input_buffer != NULL)
		pfree(input_buffer);
	input_buffer = NULL;
	reader = NULL;
  
    WATCH_PRE(timeout, endtime, cycle);	
	if (add_to_pipe(pipe_name, output_buffer, 
					limit, valid_limit))
		break;
	WATCH_POST();

	output_buffer->items_count = 0;
	output_buffer->size = 0;
	writer = (message_data_item*)&output_buffer->data;

	PG_RETURN_INT32(RESULT_DATA);
}


PG_FUNCTION_INFO_V1(dbms_pipe_unique_session_name);

Datum
dbms_pipe_unique_session_name (PG_FUNCTION_ARGS)
{
	StringInfoData strbuf;
	text *result;

	initStrngInfo(&strbuf);

	appendStringInfo(&strbuf,"PG$PIPE$%d",MyProcPid);
	result = ora_make_text_fix(&strbuf.data, strbuf.len);
	pfree(strbuf.data);
	PG_RETURN_TEXT_P(result);
}
