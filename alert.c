#include "postgres.h"


dbms_alert_register(PG_FUNCTION_ARGS);
dbms_alert_remove(PG_FUNCTION_ARGS);
dbms_alert_removeall(PG_FUNCTION_ARGS);
dbms_alert_set_defaults(PG_FUNCTION_ARGS);
dbms_alert_signal(PG_FUNCTION_ARGS);
dbms_alert_waitany(PG_FUNCTION_ARGS);
dbms_alert_waitone(PG_FUNCTION_ARGS);


PG_FUNCTION_INFO_V1(dbms_alert_register);
PG_FUNCTION_INFO_V1(dbms_alert_remove);
PG_FUNCTION_INFO_V1(dbms_alert_removeall);
PG_FUNCTION_INFO_V1(dbms_alert_set_defaults);
PG_FUNCTION_INFO_V1(dbms_alert_signal);
PG_FUNCTION_INFO_V1(dbms_alert_waitany);
PG_FUNCTION_INFO_V1(dbms_alert_waitone);

/*
 *
 *  PROCEDURE DBMS_ALERT.REGISTER (name IN VARCHAR2);
 *
 *  Registers the calling session to receive notification of alert name.
 *
 */

dbms_alert_register(PG_FUNCTION_ARGS)
{
	text *name = PG_GETARG_TEXT_P(0);

	PG_RETURN_VOID();
}


/*
 *
 *  PROCEDURE DBMS_ALERT.REMOVE(name IN VARCHAR2);
 *
 *  Unregisters the calling session from receiving notification of alert name.
 *
 */

dbms_alert_remove(PG_FUNCTION_ARGS)
{
	text *name = PG_GETARG_TEXT_P(0);

	PG_RETURN_VOID();
}


/*
 *
 *  PROCEDURE DBMS_ALERT.REMOVEALL;
 *
 *  Unregisters the calling session from notification of all alerts.
 *
 */

dbms_alert_removeall(PG_FUNCTION_ARGS)
{

	PG_RETURN_VOID();
}


/*
 *
 *  PROCEDURE DBMS_ALERT.SET_DEFAULTS(sensitivity IN NUMBER);
 *
 *  Defines configurable settings for the calling session. (sensitivity defines 
 *  the loop interval sleep time in seconds.)
 *
 */

dbms_alert_set_defaults(PG_FUNCTION_ARGS)
{
	int *sensitivity = PG_GETARG_INT32(0);

	PG_RETURN_VOID();
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

dbms_alert_signal(PG_FUNCTION_ARGS)
{
	text *name = PG_GETARG_TEXT_P(0);
	text *message = PG_GETARG_TEXT_P(1);

	PG_RETURN_VOID();
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

dbms_alert_waitany(PG_FUNCTION_ARGS)
{
	int timeout = PG_GETARG_INT32(0);

	PG_RETURN_VOID();
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

dbms_alert_waitone(PG_FUNCTION_ARGS)
{
	text *name = PG_GETARG_TEXT_P(0);
	int timeout = PG_GETARG_INT32(1);

	PG_RETURN_VOID();
}
