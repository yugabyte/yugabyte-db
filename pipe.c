#include "postgres.h"
#include "fmgr.h"
#include "storage/shmem.h"
#include "utils/memutils.h"
#include "utils/timestamp.h"
#include "storage/lwlock.h"
#include "miscadmin.h"

/*
 * First test version 0.0.1
 * @ Pavel Stehule 2006
 */

#define LOCALMSGSZ (4*1024)
#define SHMEMMSGSZ (8*1024)

#ifndef GetNowFloat
#ifdef HAVE_INT64_TIMESTAMP
#define GetNowFloat()   ((float8) GetCurrentTimestamp() / 1000000.0)
#else
#define GetNowFloat()   GetCurrentTimestamp()
#endif
#endif

Datum dbms_pipe_pack_message(PG_FUNCTION_ARGS);
Datum dbms_pipe_unpack_message(PG_FUNCTION_ARGS);
Datum dbms_pipe_send_message(PG_FUNCTION_ARGS);
Datum dbms_pipe_receive_message(PG_FUNCTION_ARGS);

typedef struct 
{
    int unread;
    int size;
    int free;
    char *carret;
    char data[];
} MultiLineBuffer;

MultiLineBuffer *ibuffer = NULL;
MultiLineBuffer *obuffer = NULL;

typedef struct
{
    LWLockId lock;
    int size;
    int count;
    char data[];
} ShmemBuffer;

ShmemBuffer *sbuffer = NULL;

static ShmemBuffer*
initSharedBuffer(int size)
{
    ShmemBuffer *result;
    bool found;
    
    result = (ShmemBuffer*) ShmemInitStruct("dbms_pipe",size,&found);
    if (!found)
    {
	if (result->lock == 0)
	{
	    result->lock = LWLockAssign();
	    result->size = 0;
	    result->count = 0;
	}
    }

    return result;
}

static int
loc_to_shm(MultiLineBuffer *buf, ShmemBuffer **sbuf)
{
    if (buf != NULL)
    {
	if (*sbuf == NULL)
	    *sbuf = initSharedBuffer(SHMEMMSGSZ);
	LWLockAcquire((*sbuf)->lock, LW_EXCLUSIVE);
	if ((*sbuf)->count > 0)
	{
	    LWLockRelease((*sbuf)->lock);
	    return 1;
	}
	memcpy((*sbuf)->data, buf, buf->size + sizeof(MultiLineBuffer));
	(*sbuf)->count = 1;
	(*sbuf)->size = buf->size + sizeof(MultiLineBuffer);
	buf->unread = 0;
	buf->free = LOCALMSGSZ;
	buf->carret = buf->data;
	buf->size = 0;
	LWLockRelease((*sbuf)->lock);
    }
    return 0;
}

static int
shm_to_loc(MultiLineBuffer **buf, ShmemBuffer **sbuf)
{
    if (*sbuf == NULL)
	*sbuf = initSharedBuffer(SHMEMMSGSZ);

    if (*buf == NULL)
    {
	*buf = (MultiLineBuffer*) MemoryContextAlloc(TopMemoryContext, LOCALMSGSZ+sizeof(MultiLineBuffer));
	(*buf)->unread = 0;
	(*buf)->free = LOCALMSGSZ;
	(*buf)->carret = (*buf)->data;
	(*buf)->size = 0;
    }
    LWLockAcquire((*sbuf)->lock, LW_EXCLUSIVE);
    if ((*sbuf)->count == 0)
    {
	LWLockRelease((*sbuf)->lock);
	return 1;
    }
    
    memcpy((*buf), (*sbuf)->data, (*sbuf)->size);
    (*buf)->carret = (*buf)->data;
    (*sbuf)->count = 0;
    (*sbuf)->size = 0;
    LWLockRelease((*sbuf)->lock);
    return 0;
}

PG_FUNCTION_INFO_V1(dbms_pipe_receive_message);

Datum
dbms_pipe_receive_message(PG_FUNCTION_ARGS)
{
    //char *pipe_name = PG_GETARG_CSTRING(0); 
    int timeout = PG_GETARG_INT32(1);
    int cycle = 0;
    float8 endtime;
    int result;
    
    endtime = GetNowFloat() + (float8)(timeout);
    for(;;)
    {
	if (GetNowFloat() > endtime)
	    PG_RETURN_INT32(1);
	if (cycle++ % 100 == 0)
	    CHECK_FOR_INTERRUPTS();
	result = shm_to_loc(&ibuffer,&sbuffer);
	if (result == 0)
	    break;
	pg_usleep(10000L);
    }
    PG_RETURN_INT32(0);
}

PG_FUNCTION_INFO_V1(dbms_pipe_send_message);

Datum
dbms_pipe_send_message(PG_FUNCTION_ARGS)
{
    //char *pipe_name = PG_GETARG_CSTRING(0); 
    int timeout = PG_GETARG_INT32(1);
    int cycle = 0;
    float8 endtime;
    int result;
    
    endtime = GetNowFloat() + (float8)(timeout);
    for(;;)
    {
	if (GetNowFloat() > endtime)
	    PG_RETURN_INT32(1);
	if (cycle++ % 100 == 0)
	    CHECK_FOR_INTERRUPTS();
	result = loc_to_shm(obuffer,&sbuffer);
	if (result == 0)
	    break;
	pg_usleep(10000L);
    }
    PG_RETURN_INT32(0);
}


static void
loc_msg_add(MultiLineBuffer **buf, char *str)
{
    int l;
    if (*buf == NULL)
    {
	*buf = (MultiLineBuffer*) MemoryContextAlloc(TopMemoryContext, LOCALMSGSZ+sizeof(MultiLineBuffer));
	(*buf)->unread = 0;
	(*buf)->free = LOCALMSGSZ;
	(*buf)->carret = (*buf)->data;
	(*buf)->size = 0;
    }
   
    l = strlen(str) + 1;
    if ((*buf)->free < l)
	elog(ERROR, "Local buffer is full");
    memcpy((*buf)->data+(*buf)->size, str, l);

    (*buf)->size += l;
    (*buf)->unread += l;
    (*buf)->free -= l;
}

static char*
loc_msg_mv(MultiLineBuffer *buf)
{
    if (buf != NULL)
    {
	if (buf->unread > 0)
	{
	    char *rv = buf->carret;
	    int l = strlen(buf->carret) + 1;
	    buf->carret += l;
	    if ((buf->unread -= l) == 0)
	    {
		buf->carret = buf->data;
		buf->size = 0;
		buf->free = LOCALMSGSZ;
		buf->unread = 0;
	    } 
//	    else if (((buf->carret += l) - buf->data) > LOCALMSGSZ/4)
//	    {
//		memcpy(buf->data,buf->carret,buf->unread);
//		buf->carret = buf->data;
//		buf->size = buf->unread;
//	    }

	    return rv;
	}
    }
    return NULL;
}

PG_FUNCTION_INFO_V1(dbms_pipe_unpack_message);

Datum
dbms_pipe_unpack_message(PG_FUNCTION_ARGS)
{
    char *str;
    text *result;
    int l;
    
    str = loc_msg_mv(ibuffer);
    if (str != NULL)
    {
	l = strlen(str);
	result = (text*) palloc(l + VARHDRSZ);
	memcpy(VARDATA(result), str, l);
	VARATT_SIZEP(result) = l + VARHDRSZ;
	PG_RETURN_TEXT_P(result);
    }
    PG_RETURN_NULL();
}

PG_FUNCTION_INFO_V1(dbms_pipe_pack_message);

Datum
dbms_pipe_pack_message(PG_FUNCTION_ARGS)
{
    int l;
    char *str;
    
    text *txt = PG_GETARG_TEXT_P(0);
    l = VARSIZE(txt) - VARHDRSZ;
    str = (char*) palloc(l + 1);
    memcpy(str, VARDATA(txt), l);
    str[l] = '\0';
    
    loc_msg_add(&obuffer, str);
    PG_RETURN_VOID();
}


