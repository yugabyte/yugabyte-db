#include "postgres.h"
#include "access/xact.h"
#include "executor/spi.h"

#include "access/htup_details.h"
#include "catalog/pg_type.h"
#include "commands/trigger.h"
#include "funcapi.h"
#include "miscadmin.h"
#include "storage/lwlock.h"
#include "storage/proc.h"
#include "storage/procarray.h"
#include "utils/builtins.h"
#include "utils/timestamp.h"

#if PG_VERSION_NUM >= 140000

#include "utils/wait_event.h"

#elif PG_VERSION_NUM >= 130000

#include "pgstat.h"

#endif

#include "orafce.h"
#include "builtins.h"
#include "pipe.h"
#include "shmmc.h"
#include "utils/rel.h"

PG_FUNCTION_INFO_V1(dbms_alert_register);
PG_FUNCTION_INFO_V1(dbms_alert_remove);
PG_FUNCTION_INFO_V1(dbms_alert_removeall);
PG_FUNCTION_INFO_V1(dbms_alert_set_defaults);
PG_FUNCTION_INFO_V1(dbms_alert_signal);
PG_FUNCTION_INFO_V1(dbms_alert_waitany);
PG_FUNCTION_INFO_V1(dbms_alert_waitone);
PG_FUNCTION_INFO_V1(dbms_alert_waitany_maxwait);
PG_FUNCTION_INFO_V1(dbms_alert_waitone_maxwait);

extern int sid;
extern LWLockId shmem_lockid;

#if PG_VERSION_NUM >= 130000

extern ConditionVariable *alert_cv;

#endif

typedef struct alert_signal_data
{
	text	   *event;
	text	   *message;
	struct alert_signal_data *next;
} alert_signal_data;

MemoryContext local_buf_cxt;
LocalTransactionId local_buf_lxid = InvalidTransactionId;
alert_signal_data *signals;

#ifndef _GetCurrentTimestamp
#define _GetCurrentTimestamp()		GetCurrentTimestamp()
#endif

#ifndef GetNowFloat
#ifdef HAVE_INT64_TIMESTAMP
#define GetNowFloat()   ((float8) _GetCurrentTimestamp() / 1000000.0)
#else
#define GetNowFloat()   _GetCurrentTimestamp()
#endif
#endif

/* in sec 1000 days */
#define MAXWAIT		86400000

#if PG_VERSION_NUM >= 170000

#define CURRENT_LXID	(MyProc->vxid.lxid)

#else

#define CURRENT_LXID	(MyProc->lxid)

#endif


static void unregister_event(int event_id, int sid);
static char* find_and_remove_message_item(int message_id, int sid,
							 bool all, bool remove_all,
							 bool filter_message,
							 int *sleep, char **event_name);

/*
 * There are maximum 30 events and 255 collaborating sessions
 *
 */
alert_event *events;
alert_lock  *locks;

alert_lock *session_lock = NULL;

#define NOT_FOUND  -1
#define NOT_USED -1

/*
 * Compare text and cstr
 */

static int
textcmpm(text *txt, char *str)
{
	char	   *p;
	int			len;

	len = VARSIZE(txt) - VARHDRSZ;
	p = VARDATA(txt);

	while (len-- && *p != '\0')
	{
		int			retval;

		if (0 != (retval = *p++ - *str++))
			return retval;
	}

	if (len > 0)
		return 1;

	if (*str != '\0')
		return -1;

	return 0;
}

/*
 * this function is called when we know so session with sid is not valid
 * anymore.
 */
static void
purge_shared_alert_mem()
{
	int			i;

	LWLockAcquire(ProcArrayLock, LW_SHARED);

	for (i = 0; i < MAX_LOCKS; i++)
	{
		PGPROC	   *proc;

		if (locks[i].sid == NOT_USED)
			continue;

		proc = BackendPidGetProcWithLock(locks[i].pid);

		if (proc == NULL)
		{
			int		j;
			int		invalid_sid = locks[i].sid;

			for (j = 0; j < MAX_EVENTS; j++)
			{
				if (events[j].event_name != NULL)
				{
					find_and_remove_message_item(j, invalid_sid,
											false, true, true, NULL, NULL);
					unregister_event(j, invalid_sid);
				}
			}

			locks[i].sid = NOT_USED;
		}
	}

	LWLockRelease(ProcArrayLock);
}

/*
 * find or create event rec
 *
 */
static alert_lock*
find_lock(int sid, bool create)
{
	int			i;
	int			first_free = NOT_FOUND;

	if (session_lock != NULL)
		return session_lock;

	for (i = 0; i < MAX_LOCKS; i++)
	{
		if (locks[i].sid == sid)
			return &locks[i];
		else if (locks[i].sid == NOT_USED && first_free == NOT_FOUND)
			first_free = i;
	}

	if (create)
	{
		if (first_free == NOT_FOUND)
		{
			purge_shared_alert_mem();

			for (i = 0; i < MAX_LOCKS; i++)
			{
				if (locks[i].sid == NOT_USED)
				{
					first_free = i;
					break;
				}
			}
		}

		if (first_free != NOT_FOUND)
		{
			locks[first_free].sid = sid;
			locks[first_free].echo = NULL;
			locks[first_free].pid = MyProcPid;
			session_lock = &locks[first_free];
			return &locks[first_free];
		}
		else
			ereport(ERROR,
				(errcode(ERRCODE_ORA_PACKAGES_LOCK_REQUEST_ERROR),
				 errmsg("lock request error"),
					 errdetail("Failed to create session lock."),
				 errhint("There are too many collaborating sessions. Increase MAX_LOCKS in 'pipe.h'.")));
	}

	return NULL;
}


static alert_event*
find_event(text *event_name, bool create, int *event_id)
{
	int			i;

	for (i = 0; i < MAX_EVENTS;i++)
	{
		if (events[i].event_name != NULL && textcmpm(event_name,events[i].event_name) == 0)
		{
			if (event_id != NULL)
				*event_id = i;
			return &events[i];
		}
	}

	if (create)
	{
		for (i=0; i < MAX_EVENTS; i++)
		{
			if (events[i].event_name == NULL)
			{
				events[i].event_name = ora_scstring(event_name);

				events[i].max_receivers = 0;
				events[i].receivers = NULL;
				events[i].messages = NULL;
				events[i].receivers_number = 0;

				if (event_id != NULL)
					*event_id = i;
				return &events[i];
			}
		}

		ereport(ERROR,
				(errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
				 errmsg("event registration error"),
				 errdetail("Too many registered events."),
				 errhint("There are too many collaborating sessions. Increase MAX_EVENTS in 'pipe.h'.")));
	}

	return NULL;
}


static void
register_event(text *event_name)
{
	alert_event *ev;
	int		   *new_receivers;
	int			first_free;
	int			i;

	find_lock(sid, true);
	ev = find_event(event_name, true, NULL);

	first_free = NOT_FOUND;
	for (i = 0; i < ev->max_receivers; i++)
	{
		if (ev->receivers[i] == sid)
			return;   /* event is registered */
		if (ev->receivers[i] == NOT_USED && first_free == NOT_FOUND)
			first_free = i;
	}

	/*
	 * I can have a maximum of MAX_LOCKS receivers for one event.
	 * Array receivers is increased for 16 fields
	 */
	if (first_free == NOT_FOUND)
	{
		if (ev->max_receivers + 16 > MAX_LOCKS)
			ereport(ERROR,
				(errcode(ERRCODE_ORA_PACKAGES_LOCK_REQUEST_ERROR),
				 errmsg("lock request error"),
					 errdetail("Failed to create session lock."),
				 errhint("There are too many collaborating sessions. Increase MAX_LOCKS in 'pipe.h'.")));

		/* increase receiver's array */

		new_receivers = (int*)salloc((ev->max_receivers + 16)*sizeof(int));

		for (i = 0; i < ev->max_receivers + 16; i++)
		{
			if (i < ev->max_receivers)
				new_receivers[i] = ev->receivers[i];
			else
				new_receivers[i] = NOT_USED;
		}

		ev->max_receivers += 16;
		if (ev->receivers)
			ora_sfree(ev->receivers);

		ev->receivers = new_receivers;

		first_free = ev->max_receivers - 16;
	}

	ev->receivers_number += 1;
	ev->receivers[first_free] = sid;
}

/*
 * Remove receiver from default receivers of message,
 * I expect clean all message_items
 */
static void
unregister_event(int event_id, int sid)
{
	alert_event *ev;

	ev = &events[event_id];

	if (ev->receivers_number > 0)
	{
		int			i;

		for (i = 0; i < ev->max_receivers; i++)
		{
			if (ev->receivers[i] == sid)
			{
				ev->receivers[i] = NOT_USED;
				ev->receivers_number -= 1;
				break;
			}
		}
		if (ev->receivers_number == 0)
		{
			ora_sfree(ev->receivers);
			ora_sfree(ev->event_name);
			ev->receivers = NULL;
			ev->event_name = NULL;
		}
	}
}


/*
 * remove receiver from list of receivers.
 * Message has always minimal one receiver
 * Return true, if exist other receiver
 */
static bool
remove_receiver(message_item *msg, int sid)
{
	int			i;
	bool		find_other = false;
	bool		found = false;

	for (i = 0; i < msg->receivers_number; i++)
	{
		if (msg->receivers[i] == sid)
		{
			msg->receivers[i] = NOT_USED;
			found = true;
		}
		else if (msg->receivers[i] != NOT_USED)
		{
			find_other = true;
		}
		if (found && find_other)
			break;
	}

	return find_other;
}

/*
 *
 * Reads message message_id for user sid. If arg:all is true,
 * then get any message. If arg:remove_all then remove all
 * signaled messages for sid. If arg:filter_message then
 * skip other messages than message_id, else read and remove
 * all others messages than message_id.
 *
 */
static char*
find_and_remove_message_item(int message_id, int sid,
							 bool all, bool remove_all,
							 bool filter_message,
							 int *sleep, char **event_name)
{
	alert_lock *alck;

	char *result = NULL;
	if (sleep != NULL)
		*sleep = 0;

	alck = find_lock(sid, false);

	if (event_name)
		*event_name = NULL;

	if (alck != NULL && alck->echo != NULL)
	{
		/* if I have registered and created item */
		struct _message_echo *echo, *last_echo;

		echo = alck->echo;
		last_echo = NULL;

		while (echo != NULL)
		{
			char	   *message_text;
			int			_message_id;

			bool destroy_msg_item = false;

			if (filter_message && echo->message_id != message_id)
			{
				last_echo = echo;
				echo = echo->next_echo;
				continue;
			}

			message_text = echo->message->message;
			_message_id = echo->message_id;

			if (!remove_receiver(echo->message, sid))
			{
				destroy_msg_item = true;
				if (echo->message->prev_message != NULL)
					echo->message->prev_message->next_message =
						echo->message->next_message;
				else
					events[echo->message_id].messages =
						echo->message->next_message;
				if (echo->message->next_message != NULL)
					echo->message->next_message->prev_message =
						echo->message->prev_message;
				ora_sfree(echo->message->receivers);
				ora_sfree(echo->message);
			}

			if (last_echo == NULL)
			{
				alck->echo = echo->next_echo;
				ora_sfree(echo);
				echo = alck->echo;
			}
			else
			{
				last_echo->next_echo = echo->next_echo;
				ora_sfree(echo);
				echo = last_echo;
			}

			if (remove_all)
			{
				if (message_text != NULL && destroy_msg_item)
					ora_sfree(message_text);

				continue;
			}
			else if (_message_id == message_id || all)
			{
				/* I have to do local copy */
				if (message_text)
				{
					result = pstrdup(message_text);
					if (destroy_msg_item)
						ora_sfree(message_text);
				}

				if (event_name != NULL)
					*event_name = pstrdup(events[_message_id].event_name);

				break;
			}
		}
	}

	return result;
}

/*
 * Queue mustn't to contain duplicate messages
 */
static void
create_message(text *event_name, text *message)
{
	int			event_id;
	alert_event *ev;
	message_item *msg_item = NULL;

	find_event(event_name, false, &event_id);

	/* process event only when any recipient exitsts */
	if (NULL != (ev = find_event(event_name, false, &event_id)))
	{
		if (ev->receivers_number > 0)
		{
			int			i,j,k;

			msg_item = ev->messages;
			while (msg_item != NULL)
			{
				if (msg_item->message == NULL && message == NULL)
					return;

				if (msg_item->message != NULL && message != NULL)
					if (0 == textcmpm(message,msg_item->message))
						return;

				msg_item = msg_item->next_message;
			}

			msg_item = salloc(sizeof(message_item));

			msg_item->receivers = salloc(ev->receivers_number*sizeof(int));
			msg_item->receivers_number = ev->receivers_number;

			if (message != NULL)
				msg_item->message = ora_scstring(message);
			else
				msg_item->message = NULL;

			msg_item->message_id = event_id;
			for (i = j = 0; j < ev->max_receivers; j++)
			{
				if (ev->receivers[j] != NOT_USED)
				{
					msg_item->receivers[i++] = ev->receivers[j];
					for (k = 0; k < MAX_LOCKS; k++)
						if (locks[k].sid == ev->receivers[j])
						{
							/* create echo */

							message_echo *echo = salloc(sizeof(message_echo));
							echo->message = msg_item;
							echo->message_id = event_id;
							echo->next_echo = NULL;

							if (locks[k].echo == NULL)
								locks[k].echo = echo;
							else
							{
								message_echo *p;
								p = locks[k].echo;

								while (p->next_echo != NULL)
									p = p->next_echo;

								p->next_echo = echo;
							}
						}
				}
			}

			msg_item->next_message = NULL;
			if (ev->messages == NULL)
			{
				msg_item->prev_message = NULL;
				ev->messages = msg_item;
			}
			else
			{
				message_item *p;

				p = ev->messages;
				while (p->next_message != NULL)
					p = p->next_message;

				p->next_message = msg_item;
				msg_item->prev_message = p;
			}

		}
	}
}

#define WATCH_PRE(t, et, c) \
et = GetNowFloat() + (float8)t; c = 0; \
do \
{ \

#define WATCH_POST(t,et,c) \
if (GetNowFloat() >= et) \
break; \
if (cycle++ % 100 == 0) \
CHECK_FOR_INTERRUPTS(); \
pg_usleep(10000L); \
} while(t != 0);

/*
 *
 *  PROCEDURE DBMS_ALERT.REGISTER (name IN VARCHAR2);
 *
 *  Registers the calling session to receive notification of alert name.
 *
 */
Datum
dbms_alert_register(PG_FUNCTION_ARGS)
{
	text	   *name = PG_GETARG_TEXT_P(0);
	int			cycle = 0;
	float8		endtime;
	float8		timeout = 2;

	WATCH_PRE(timeout, endtime, cycle);
	if (ora_lock_shmem(SHMEMMSGSZ, MAX_PIPES, MAX_EVENTS, MAX_LOCKS, false))
	{
		register_event(name);
		LWLockRelease(shmem_lockid);
		PG_RETURN_VOID();
	}
	WATCH_POST(timeout, endtime, cycle);
	LOCK_ERROR();
	PG_RETURN_VOID();
}

/*
 *
 *  PROCEDURE DBMS_ALERT.REMOVE(name IN VARCHAR2);
 *
 *  Unregisters the calling session from receiving notification of alert name.
 *  Don't raise any exceptions.
 *
 */
Datum
dbms_alert_remove(PG_FUNCTION_ARGS)
{
	text	   *name = PG_GETARG_TEXT_P(0);
	int			ev_id;
	int			cycle = 0;
	float8		endtime;
	float8		timeout = 2;

	WATCH_PRE(timeout, endtime, cycle);
	if (ora_lock_shmem(SHMEMMSGSZ, MAX_PIPES,MAX_EVENTS,MAX_LOCKS,false))
	{
		alert_event *ev;

		ev = find_event(name, false, &ev_id);
		if (NULL != ev)
		{
			find_and_remove_message_item(ev_id, sid,
							 false, true, true, NULL, NULL);
			unregister_event(ev_id, sid);
		}
		LWLockRelease(shmem_lockid);
		PG_RETURN_VOID();
	}
	WATCH_POST(timeout, endtime, cycle);
	LOCK_ERROR();
	PG_RETURN_VOID();
}

/*
 *
 *  PROCEDURE DBMS_ALERT.REMOVEALL;
 *
 *  Unregisters the calling session from notification of all alerts.
 *
 */
Datum
dbms_alert_removeall(PG_FUNCTION_ARGS)
{
	int			cycle = 0;
	float8		endtime;
	float8		timeout = 2;

	WATCH_PRE(timeout, endtime, cycle);
	if (ora_lock_shmem(SHMEMMSGSZ, MAX_PIPES,MAX_EVENTS,MAX_LOCKS,false))
	{
		alert_lock *alck;
		int			i;

		for (i = 0; i < MAX_EVENTS; i++)
		{
			if (events[i].event_name != NULL)
			{
				find_and_remove_message_item(i, sid,
								 false, true, true, NULL, NULL);
				unregister_event(i, sid);
			}
		}

		alck = find_lock(sid, false);
		if (alck)
		{
			/* After all events unregistration, an echo field should NULL */
			Assert(alck->echo == NULL);

			alck->sid = NOT_USED;
			session_lock = NULL;
		}

		LWLockRelease(shmem_lockid);
		PG_RETURN_VOID();
	}
	WATCH_POST(timeout, endtime, cycle);
	LOCK_ERROR();
	PG_RETURN_VOID();
}

/*
 * workhorse for dbms_alert_waitany and dbms_alert_waitany_maxwait
 */
static Datum
_dbms_alert_waitany(int timeout, FunctionCallInfo fcinfo)
{
	TupleDesc	tupdesc;
	AttInMetadata *attinmeta;
	HeapTuple	tuple;
	Datum		result;
	char	   *str[3] = {NULL, NULL, "1"};
	instr_time	start_time;
	TupleDesc	btupdesc;

#if PG_VERSION_NUM < 130000

	long		cycle = 0;

#endif


	INSTR_TIME_SET_CURRENT(start_time);

	for (;;)
	{
		if (ora_lock_shmem(SHMEMMSGSZ, MAX_PIPES, MAX_EVENTS, MAX_LOCKS, false))
		{
			str[1] = find_and_remove_message_item(-1, sid,
												  true, false, false, NULL, &str[0]);
			if (str[0])
			{
				str[2] = "0";
				LWLockRelease(shmem_lockid);
				break;
			}

			LWLockRelease(shmem_lockid);
		}

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

			if (ConditionVariableTimedSleep(alert_cv, cur_timeout, PG_WAIT_EXTENSION))
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

#endif

	get_call_result_type(fcinfo, NULL, &tupdesc);

	btupdesc = BlessTupleDesc(tupdesc);
	attinmeta = TupleDescGetAttInMetadata(btupdesc);
	tuple = BuildTupleFromCStrings(attinmeta, str);
	result = HeapTupleGetDatum(tuple);

	if (str[0])
		pfree(str[0]);

	if (str[1])
		pfree(str[1]);

	return result;
}

/*
 *
 *  PROCEDURE DBMS_ALERT.WAITANY(name OUT VARCHAR2 ,message OUT VARCHAR2
 *                              ,status OUT INTEGER
 *                              ,timeout IN NUMBER DEFAULT MAXWAIT);
 *
 *  Waits for up to timeout seconds to be notified of any alerts for which
 *  the session is registered. If status = 0 then name and message contain
 *  alert information. If status = 1 then timeout seconds elapsed without
 *  notification of any alert.
 *
 */
Datum
dbms_alert_waitany(PG_FUNCTION_ARGS)
{
	int			timeout;

	if (!PG_ARGISNULL(0))
	{
		/*
		 * cannot to change SQL API now, so use not well choosed
		 * float.
		 */
		timeout = (int) PG_GETARG_FLOAT8(0);

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


	return _dbms_alert_waitany(timeout, fcinfo);
}

Datum
dbms_alert_waitany_maxwait(PG_FUNCTION_ARGS)
{
	return _dbms_alert_waitany(MAXWAIT, fcinfo);
}

/*
 * common part of dbms_alert_waitone and dbms_alert_waitone_maxwait
 */
static Datum
_dbms_alert_waitone(text *name, int timeout, FunctionCallInfo fcinfo)
{
	TupleDesc	tupdesc;
	AttInMetadata *attinmeta;
	HeapTuple	tuple;
	Datum		result;
	int			message_id;
	char	   *str[2] = {NULL,"1"};
	char	   *event_name;
	instr_time	start_time;
	TupleDesc	btupdesc;

#if PG_VERSION_NUM < 130000

	long		cycle = 0;

#endif

	INSTR_TIME_SET_CURRENT(start_time);

	for (;;)
	{
		if (ora_lock_shmem(SHMEMMSGSZ, MAX_PIPES, MAX_EVENTS, MAX_LOCKS, false))
		{
			if (NULL != find_event(name, false, &message_id))
			{
				str[0] = find_and_remove_message_item(message_id, sid,
													  false, false, false, NULL, &event_name);
				if (event_name != NULL)
				{
					str[1] = "0";
					pfree(event_name);
					LWLockRelease(shmem_lockid);
					break;
				}
			}

			LWLockRelease(shmem_lockid);
		}

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

			if (ConditionVariableTimedSleep(alert_cv, cur_timeout, PG_WAIT_EXTENSION))
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

#endif

	get_call_result_type(fcinfo, NULL, &tupdesc);

	btupdesc = BlessTupleDesc(tupdesc);
	attinmeta = TupleDescGetAttInMetadata(btupdesc);
	tuple = BuildTupleFromCStrings(attinmeta, str);
	result = HeapTupleGetDatum(tuple);

	if (str[0])
		pfree(str[0]);

	return result;
}

/*
 *
 *  PROCEDURE DBMS_ALERT.WAITONE(name IN VARCHAR2, message OUT VARCHAR2
 *                              ,status OUT INTEGER
 *                              ,timeout IN NUMBER DEFAULT MAXWAIT);
 *
 *  Waits for up to timeout seconds for notification of alert name. If status = 0
 *  then message contains alert information. If status = 1 then timeout
 *  seconds elapsed without notification.
 *
 */
Datum
dbms_alert_waitone(PG_FUNCTION_ARGS)
{
	text	   *name;
	int			timeout;

	if (PG_ARGISNULL(0))
		ereport(ERROR,
				(errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
				 errmsg("event name is NULL"),
				 errdetail("Eventname may not be NULL.")));

	if (!PG_ARGISNULL(1))
	{
		/*
		 * cannot to change SQL API now, so use not well choosed
		 * float.
		 */
		timeout = (int) PG_GETARG_FLOAT8(1);

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

	name = PG_GETARG_TEXT_P(0);

	return _dbms_alert_waitone(name, timeout, fcinfo);
}

Datum
dbms_alert_waitone_maxwait(PG_FUNCTION_ARGS)
{
	text	   *name;

	if (PG_ARGISNULL(0))
		ereport(ERROR,
				(errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
				 errmsg("event name is NULL"),
				 errdetail("Eventname may not be NULL.")));

	name = PG_GETARG_TEXT_P(0);

	return _dbms_alert_waitone(name, MAXWAIT, fcinfo);
}

/*
 *
 *  PROCEDURE DBMS_ALERT.SET_DEFAULTS(sensitivity IN NUMBER);
 *
 *  The SET_DEFAULTS procedure is used to set session configurable settings
 *  used by the DBMS_ALERT package. Currently, the polling loop interval sleep time
 *  is the only session setting that can be modified using this procedure. The
 *  header for this procedure is,
 *
 */
Datum
dbms_alert_set_defaults(PG_FUNCTION_ARGS)
{
	ereport(ERROR,
		(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
		 errmsg("feature not supported"),
		 errdetail("Sensitivity isn't supported.")));

	PG_RETURN_VOID();
}

void
orafce_xact_cb(XactEvent event, void *arg)
{
	if (event == XACT_EVENT_PRE_COMMIT)
	{
		/*
		 * In this time we have valid MyProc->lxid, in ACT_EVENT_COMMIT is
		 * MyProc->lxid already invalided. So we need to invalid pointer to
		 * obsolete buffer here.
		 */
		if (local_buf_lxid != CURRENT_LXID)
			signals = NULL;
	}
	else if (event == XACT_EVENT_COMMIT && signals)
	{
		if (ora_lock_shmem(SHMEMMSGSZ, MAX_PIPES, MAX_EVENTS, MAX_LOCKS, false))
		{
			alert_signal_data *signal = signals;

			while (signal)
			{
				create_message(signal->event, signal->message);
				signal = signal->next;
			}

			signals = NULL;

			LWLockRelease(shmem_lockid);

#if PG_VERSION_NUM >= 130000

			ConditionVariableBroadcast(alert_cv);

#endif

		}
	}
}

static bool
text_eq(const text *p1, const text *p2)
{
	int		len1,
			len2;

	Assert(p1 && p2);

	len1 = VARSIZE_ANY_EXHDR(p1);
	len2 = VARSIZE_ANY_EXHDR(p2);

	if (len1 == len2)
	{
		return memcmp(VARDATA_ANY(p1), VARDATA_ANY(p2), len1) == 0;
	}
	else
		return false;
}

/*
 *
 *  PROCEDURE DBMS_ALERT.SIGNAL(name IN VARCHAR2,message IN VARCHAR2);
 *
 *  Signals the occurrence of alert name and attaches message. (Sessions
 *  registered for alert name are notified only when the signaling transaction
 *  commits.)
 *
 */
Datum
dbms_alert_signal(PG_FUNCTION_ARGS)
{
	text	   *event;
	text	   *message;
	alert_signal_data *new_signal;
	alert_signal_data *last_signal = NULL;
	MemoryContext oldcxt;

	if (PG_ARGISNULL(0))
		ereport(ERROR,
			(errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
			 errmsg("event name is NULL"),
			 errdetail("Eventname may not be NULL.")));

	event = PG_GETARG_TEXT_P(0);
	message = (!PG_ARGISNULL(1)) ? PG_GETARG_TEXT_P(1) : NULL;

	if (local_buf_lxid != CURRENT_LXID)
	{
		local_buf_cxt = AllocSetContextCreate(TopTransactionContext,
											  "dbms_alert local buffer",
											  ALLOCSET_START_SMALL_SIZES);
		local_buf_lxid = CURRENT_LXID;

		signals = NULL;
		last_signal = NULL;
	}

	if (signals)
	{
		alert_signal_data *s = signals;

		while (s)
		{
			last_signal = s;

			if (text_eq(s->event, event) == 0)
			{
				if (!message && !s->message)
					PG_RETURN_VOID();

				if (message && s->message &&
					 text_eq(message, s->message) == 0)
					PG_RETURN_VOID();
			}

			s = s->next;
		}
	}

	oldcxt = MemoryContextSwitchTo(local_buf_cxt);

	new_signal = palloc(sizeof(alert_signal_data));
	new_signal->event = TextPCopy(event);
	new_signal->message = message ? TextPCopy(message) : NULL;
	new_signal->next = NULL;

	MemoryContextSwitchTo(oldcxt);

	if (signals)
		last_signal->next = new_signal;
	else
		signals = new_signal;

	PG_RETURN_VOID();
}

/*
 * removed by Orafce 9.6, header is necessary to allow upgrade
 *
 */
Datum
dbms_alert_defered_signal(PG_FUNCTION_ARGS)
{
	PG_RETURN_NULL();
}
