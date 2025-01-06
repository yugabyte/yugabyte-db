/*-------------------------------------------------------------------------
 *
 * elog.c
 *	  error logging and reporting
 *
 * Because of the extremely high rate at which log messages can be generated,
 * we need to be mindful of the performance cost of obtaining any information
 * that may be logged.  Also, it's important to keep in mind that this code may
 * get called from within an aborted transaction, in which case operations
 * such as syscache lookups are unsafe.
 *
 * Some notes about recursion and errors during error processing:
 *
 * We need to be robust about recursive-error scenarios --- for example,
 * if we run out of memory, it's important to be able to report that fact.
 * There are a number of considerations that go into this.
 *
 * First, distinguish between re-entrant use and actual recursion.  It
 * is possible for an error or warning message to be emitted while the
 * parameters for an error message are being computed.  In this case
 * errstart has been called for the outer message, and some field values
 * may have already been saved, but we are not actually recursing.  We handle
 * this by providing a (small) stack of ErrorData records.  The inner message
 * can be computed and sent without disturbing the state of the outer message.
 * (If the inner message is actually an error, this isn't very interesting
 * because control won't come back to the outer message generator ... but
 * if the inner message is only debug or log data, this is critical.)
 *
 * Second, actual recursion will occur if an error is reported by one of
 * the elog.c routines or something they call.  By far the most probable
 * scenario of this sort is "out of memory"; and it's also the nastiest
 * to handle because we'd likely also run out of memory while trying to
 * report this error!  Our escape hatch for this case is to reset the
 * ErrorContext to empty before trying to process the inner error.  Since
 * ErrorContext is guaranteed to have at least 8K of space in it (see mcxt.c),
 * we should be able to process an "out of memory" message successfully.
 * Since we lose the prior error state due to the reset, we won't be able
 * to return to processing the original error, but we wouldn't have anyway.
 * (NOTE: the escape hatch is not used for recursive situations where the
 * inner message is of less than ERROR severity; in that case we just
 * try to process it and return normally.  Usually this will work, but if
 * it ends up in infinite recursion, we will PANIC due to error stack
 * overflow.)
 *
 * YB notes:
 * When Postgres code is executed in DocDB environment (what is commonly
 * referenced as "pushdown"), the logging and error handling do not work
 * properly for a number of reasons. First and foremost, Postgres uses global
 * variables to keep error state, and the DocDB environment is multi-threaded,
 * where global variables are accessible from multiple threads, which may
 * conflict. Other is that the errors have to pass through DocDB to be presented
 * to the client, and DocDB has incompatible error handling subsystem.
 *
 * We mitigate many of those problems by replacing Postgres implementation of
 * the error handling API. That implementation detects DocDB environment and
 * uses alternate functions and data structures, designed to work in
 * multi-threaded environment and interface with DocDB logging and error
 * handling.
 *
 * Similar to Postgres, our implementation maintains a stack of error handlers,
 * and error raising functions make long jumps to the next handler. While
 * Postgres sets up top level handlers in its processes' main routines before
 * entering the event loop, DocDB has to set up and tear down one as needed,
 * when a request needs to execute Postgres code. See PG_SETUP_ERROR_REPORTING
 * macro for detail. The PG_TRY macro sets secondary handler, this API works
 * the same in any mode, while internal implementation is different. In
 * multi-threaded mode a thread local variable to hold error state. The
 * variable is of type YbgStatus, which is designed to easily interface with
 * Status, representing DocDB error state. The YbgStatus instance contains
 * "private" data, similar to ErrorData, in multi-threaded mode functions like
 * errmsg() update these "private" data instead, and info is transferred to the
 * YbgStatus at the end. The PG_STATUS_OK macro is used to tear down the top
 * level handler and return YbgStatus if there is any error to deliver to the
 * client.
 *
 * The returned YbgStatus instance is converted to a Status, and it is currently
 * a big limitation that Status is conventionally handled as an error by DocDB,
 * that is, it interrupt execution flow. Therefore there's no way to return
 * non-error messages, such as warnings and notices from "pushed down" code to
 * the client. Though in multi-threaded mode all messages are logged, regardless
 * of the Postgres configuration.
 *
 * The Postgres processes serving PG-compatible clients in Yugabyte are
 * prepared to handle Status instances when communicating to DocDB. Those
 * Statuses may originate from the "pushed down" code, as well as from DocDB
 * itself. The handler raises a Postgres error using information in the
 * Status. While the implementation of the Status-to-PG error conversion
 * matches the opposite conversion, we do not try to distinguish by Status
 * origin, and with backward compatibility in mind can not be strict with
 * regards to the status format. In the cases when DocDB reimplements some
 * Postgres functionality, instead of invoking Postgres code it is recommended
 * to refer YbgStatus-to-Status conversion to make Status instances that would
 * look like original postgres errors.
 *
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/backend/utils/error/elog.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <ctype.h>
#ifdef HAVE_SYSLOG
#include <syslog.h>
#endif
#ifdef HAVE_EXECINFO_H
#include <execinfo.h>
#endif

#include "access/transam.h"
#include "access/xact.h"
#include "common/pg_yb_common.h"
#include "libpq/libpq.h"
#include "libpq/pqformat.h"
#include "mb/pg_wchar.h"
#include "miscadmin.h"
#include "pgstat.h"
#include "postmaster/bgworker.h"
#include "postmaster/postmaster.h"
#include "postmaster/syslogger.h"
#include "storage/ipc.h"
#include "storage/proc.h"
#include "tcop/tcopprot.h"
#include "utils/guc.h"
#include "utils/memutils.h"
#include "utils/ps_status.h"

// YB includes.
#include "pg_yb_utils.h"
#include "ybgate/ybgate_status.h"

/* In this module, access gettext() via err_gettext() */
#undef _
#define _(x) err_gettext(x)


/* Global variables */
ErrorContextCallback *error_context_stack = NULL;

sigjmp_buf *PG_exception_stack = NULL;

extern bool redirection_done;

/*
 * Hook for intercepting messages before they are sent to the server log.
 * Note that the hook will not get called for messages that are suppressed
 * by log_min_messages.  Also note that logging hooks implemented in preload
 * libraries will miss any log messages that are generated before the
 * library is loaded.
 */
emit_log_hook_type emit_log_hook = NULL;

/* GUC parameters */
int			Log_error_verbosity = PGERROR_VERBOSE;
char	   *Log_line_prefix = NULL; /* format for extra log line info */
int			Log_destination = LOG_DESTINATION_STDERR;
char	   *Log_destination_string = NULL;
bool		syslog_sequence_numbers = true;
bool		syslog_split_messages = true;

#ifdef HAVE_SYSLOG

/*
 * Max string length to send to syslog().  Note that this doesn't count the
 * sequence-number prefix we add, and of course it doesn't count the prefix
 * added by syslog itself.  Solaris and sysklogd truncate the final message
 * at 1024 bytes, so this value leaves 124 bytes for those prefixes.  (Most
 * other syslog implementations seem to have limits of 2KB or so.)
 */
#ifndef PG_SYSLOG_LIMIT
#define PG_SYSLOG_LIMIT 900
#endif

static bool openlog_done = false;
static char *syslog_ident = NULL;
static int	syslog_facility = LOG_LOCAL0;

static void write_syslog(int level, const char *line);
#endif

#ifdef WIN32
extern char *event_source;

static void write_eventlog(int level, const char *line, int len);
#endif

/* We provide a small stack of ErrorData records for re-entrant cases */
#define ERRORDATA_STACK_SIZE  5

static ErrorData errordata[ERRORDATA_STACK_SIZE];

static int	errordata_stack_depth = -1; /* index of topmost active frame */

static int	recursion_depth = 0;	/* to detect actual recursion */

/*
 * Equivalent of ErrorData for YbGate.
 * Instead of having global variable to store instances of this structure, they
 * are attached to the YbgStatus instance associated with the thread as an
 * untyped pointer.
 * In multithreaded mode err... functions populate the fields of this structure,
 * and at the end of the ereport/elog sequence the data are transferred to the
 * YbgStatus instance to be used later construct DocDB Status.
 * In a case when another error is raised during error handling, multiple
 * YbgErrorData instances may form a stack. Similar to Postgres we limit stack
 * depth.
 */
typedef struct YbgErrorData
{
	int			elevel;
	const char *errmsg;
	const char **errargs;
	int			nargs;
	int			argsize;
	int			sqlerrcode; /* encoded ERRSTATE */
	uint16_t yb_txn_errcode; /* YB transaction error cast to uint16, as returned
							  * by static_cast of TransactionErrorTag::Decode of
							  * Status::ErrorData(TransactionErrorTag::kCategory)
							  */
	const char *filename;	/* __FILE__ of ereport() call */
	int			lineno;		/* __LINE__ of ereport() call */
	const char *funcname;	/* __func__ of ereport() call */
	int			saved_errno; /* errno at entry */
	int			errordata_stack_depth;
	struct YbgErrorData *previous; /* next stack frame */
} YbgErrorData;
typedef struct YbgErrorData *YbgError;

static void yb_additional_errmsg(const char* fmt,...);
static void yb_errmsg_va(const char* fmt, va_list args);
static void yb_format_and_append(StringInfo buf, const char *fmt,
								 const size_t nargs, const char **args);

/*
 * yb_errstart - YbGate equivalent of errstart
 *
 * Create a YbgErrorData instance and put the parameters into it.
 * Lasily creates YbgStatus instance. If YbgStatus instance already exists, and
 * has a YbgErrorData it is referenced by the new YbgErrorData instance, forming
 * a stack.
 */
pg_attribute_cold bool
yb_errstart_cold(int elevel)
{
	return yb_errstart(elevel);
}

bool
yb_errstart(int elevel)
{
	YbgStatus		status;
	MemoryContext	error_context;
	MemoryContext	old_context;
	YbgError		edata;
	YbgError		previous;

	Assert(IsMultiThreadedMode());
	status = YBCPgGetThreadLocalErrStatus();
	if (status == NULL)
	{
		/* Lazily create YbgStatus */
		status = YbgStatusCreate();
		YBCPgSetThreadLocalErrStatus(status);
	}
	error_context = (MemoryContext) YbgStatusGetContext(status);
	if (error_context == NULL)
	{
		/*
		 * Static constants of YbgStatus type have NULL context. They are read
		 * only, and can not be used with ereport/elog
		 */
		YBCLogImpl(/* severity (3=FATAL) */ 3, NULL /* filename */, 0 /* lineno */,
				   YBShouldLogStackTraceOnError(),
				   "PG error state is missing memory context");
		pg_unreachable();
	}
	old_context = MemoryContextSwitchTo(error_context);
	/* Initialize data for this error frame */
	previous = (YbgError) YbgStatusGetEdata(status);
	edata = (YbgError) palloc0(sizeof(YbgErrorData));
	edata->elevel = elevel;
	edata->previous = previous;
	edata->errordata_stack_depth =
		previous ? previous->errordata_stack_depth + 1 : 1;
	if (edata->errordata_stack_depth >= ERRORDATA_STACK_SIZE)
	{
		/* Too deep stack typically indicates recursive error. */
		YBCPgSetThreadLocalErrStatus(NULL);
		MemoryContextSwitchTo(old_context);
		MemoryContextReset(error_context);
		YBCLogImpl(/* severity (3=FATAL) */ 3, NULL /* filename */, 0 /* lineno */,
				   YBShouldLogStackTraceOnError(),
				   "Error data stack is too deep");
		pg_unreachable();
	}
	/* Select default errcode based on elevel */
	if (elevel >= ERROR)
		edata->sqlerrcode = ERRCODE_INTERNAL_ERROR;
	else
		edata->sqlerrcode = ERRCODE_SUCCESSFUL_COMPLETION;
	/* errno is saved here so that error parameter eval can't change it */
	edata->saved_errno = errno;
	YbgStatusSetEdata(status, edata);
	MemoryContextSwitchTo(old_context);
	return true;
}

/*
 * yb_copy_edata_fields_to_status - transfer data from the YbgError frame to
 * the YbgStatus
 *
 * The data is transferred when composing of ereport/elog with severity ERROR or
 * higher is finalized. Since YbgError is private to elog.c, YbGate can not use
 * it directly to construct the Status instance, hence copy. The fields being
 * copied here should match those cleaned up in yb_reset_error_status.
 */
static void
yb_copy_edata_fields_to_status(YbgStatus status, YbgError edata)
{
	YbgStatusSetLevel(status, edata->elevel);
	YbgStatusSetMessageAndArgs(status, edata->errmsg, edata->nargs,
							   edata->errargs);
	YbgStatusSetSqlError(status, edata->sqlerrcode);
	YbgStatusSetFilename(status, edata->filename);
	YbgStatusSetLineNumber(status, edata->lineno);
	YbgStatusSetFuncname(status, edata->funcname);
}

/*
 * yb_write_status_to_server_log - write the status to the tserver log.
 *
 * Must be called from yb_errfinish because only then are filename and lineno
 * populated.
 */
static void
yb_write_status_to_server_log()
{
	YbgStatus		status = YBCPgGetThreadLocalErrStatus();
	YbgError		edata = (YbgError) YbgStatusGetEdata(status);
	StringInfoData	buf;
	MemoryContext	error_context = (MemoryContext) YbgStatusGetContext(status);
	MemoryContext	oldcontext = MemoryContextSwitchTo(error_context);

	initStringInfo(&buf);
	yb_format_and_append(&buf, edata->errmsg, edata->nargs,
						 edata->errargs);
	bool is_error = YbgStatusIsError(status);
	YBCLogImpl(is_error ? 2 : 0 /* severity */,
			   edata->filename,
			   edata->lineno,
			   is_error ? YBShouldLogStackTraceOnError() : false,
			   "%s", buf.data);
	pfree(buf.data);
	MemoryContextSwitchTo(oldcontext);
}

/*
 * yb_errfinish - YbGate equivalent of errfinish
 *
 * Move error data from the current YbgError frame to the status, and if error
 * level is severe enough, jump to the current error handler.
 */
void
yb_errfinish(const char *filename, int lineno, const char *funcname)
{
	YbgStatus		status = YBCPgGetThreadLocalErrStatus();
	YbgError		edata = (YbgError) YbgStatusGetEdata(status);

	if (filename)
	{
		const char *slash;

		/* keep only base name, useful especially for vpath builds */
		slash = strrchr(filename, '/');
		if (slash)
			filename = slash + 1;
	}
	edata->filename = filename;
	edata->lineno = lineno;
	edata->funcname = funcname;

	yb_write_status_to_server_log();

	/*
	 * Pop current edata frame by setting previous as current.
	 * Even if we are going to make sigjmp from here, we may end up in a
	 * PG_CATCH block where the error may be discarded and composing of the
	 * previous error continued.
	 */
	YbgStatusSetEdata(status, edata->previous);
	if (edata->elevel >= ERROR)
	{
		yb_copy_edata_fields_to_status(status, edata);
		sigjmp_buf *buffer = YBCPgGetThreadLocalJumpBuffer();
		if (buffer == NULL)
		{
			/*
			 * There is no jump destination, most likely YbGate function was
			 * invoked without PG_SETUP_ERROR_REPORTING()
			 * Raise fatal error and provide the filename and line number set
			 * by the yb_errstart. Note, status is not NULL here, as NULL
			 * status is not an error.
			 */
			const char *filename = YbgStatusGetFilename(status);
			int lineno = YbgStatusGetLineNumber(status);
			YBCLogImpl(3 /* severity (3=FATAL) */ , filename, lineno,
					   YBShouldLogStackTraceOnError(),
					   "PG error reporting has not been set up");
		}
		siglongjmp(*buffer, 1);
		pg_unreachable();
	}
}

/*
 * Saved timeval and buffers for formatted timestamps that might be used by
 * both log_line_prefix and csv logs.
 */
static struct timeval saved_timeval;
static bool saved_timeval_set = false;

#define MAX_HOSTNAME_LENGTH 128

#define FORMATTED_TS_LEN 128
static char formatted_start_time[FORMATTED_TS_LEN];
static char formatted_log_time[FORMATTED_TS_LEN];


/* Macro for checking errordata_stack_depth is reasonable */
#define CHECK_STACK_DEPTH() \
	do { \
		if (errordata_stack_depth < 0) \
		{ \
			errordata_stack_depth = -1; \
			ereport(ERROR, (errmsg_internal("errstart was not called"))); \
		} \
	} while (0)

#define RETURN_IF_MULTITHREADED_MODE() \
	do { \
		if (IsMultiThreadedMode()) \
		{ \
			return 0; \
		} \
	} while(0)

static const char *err_gettext(const char *str) pg_attribute_format_arg(1);
static pg_noinline void set_backtrace(ErrorData *edata, int num_skip);
static void set_errdata_field(MemoryContextData *cxt, char **ptr, const char *str);
static void write_console(const char *line, int len);
static const char *process_log_prefix_padding(const char *p, int *padding);
static void log_line_prefix(StringInfo buf, ErrorData *edata);
static void send_message_to_server_log(ErrorData *edata);
static void send_message_to_frontend(ErrorData *edata);
static void append_with_tabs(StringInfo buf, const char *str);


/*
 * is_log_level_output -- is elevel logically >= log_min_level?
 *
 * We use this for tests that should consider LOG to sort out-of-order,
 * between ERROR and FATAL.  Generally this is the right thing for testing
 * whether a message should go to the postmaster log, whereas a simple >=
 * test is correct for testing whether the message should go to the client.
 */
static inline bool
is_log_level_output(int elevel, int log_min_level)
{
	if (elevel == LOG || elevel == LOG_SERVER_ONLY)
	{
		if (log_min_level == LOG || log_min_level <= ERROR)
			return true;
	}
	else if (elevel == WARNING_CLIENT_ONLY)
	{
		/* never sent to log, regardless of log_min_level */
		return false;
	}
	else if (log_min_level == LOG)
	{
		/* elevel != LOG */
		if (elevel >= FATAL)
			return true;
	}
	/* Neither is LOG */
	else if (elevel >= log_min_level)
		return true;

	return false;
}

/*
 * Policy-setting subroutines.  These are fairly simple, but it seems wise
 * to have the code in just one place.
 */

/*
 * should_output_to_server --- should message of given elevel go to the log?
 */
static inline bool
should_output_to_server(int elevel)
{
	return is_log_level_output(elevel, log_min_messages);
}

/*
 * should_output_to_client --- should message of given elevel go to the client?
 */
static inline bool
should_output_to_client(int elevel)
{
	if (whereToSendOutput == DestRemote && elevel != LOG_SERVER_ONLY)
	{
		/*
		 * client_min_messages is honored only after we complete the
		 * authentication handshake.  This is required both for security
		 * reasons and because many clients can't handle NOTICE messages
		 * during authentication.
		 */
		if (ClientAuthInProgress)
			return (elevel >= ERROR);
		else
			return (elevel >= client_min_messages || elevel == INFO);
	}
	return false;
}


/*
 * message_level_is_interesting --- would ereport/elog do anything?
 *
 * Returns true if ereport/elog with this elevel will not be a no-op.
 * This is useful to short-circuit any expensive preparatory work that
 * might be needed for a logging message.  There is no point in
 * prepending this to a bare ereport/elog call, however.
 */
bool
message_level_is_interesting(int elevel)
{
	/*
	 * Keep this in sync with the decision-making in errstart().
	 */
	if (elevel >= ERROR ||
		should_output_to_server(elevel) ||
		should_output_to_client(elevel))
		return true;
	return false;
}

/*
 * in_error_recursion_trouble --- are we at risk of infinite error recursion?
 *
 * This function exists to provide common control of various fallback steps
 * that we take if we think we are facing infinite error recursion.  See the
 * callers for details.
 */
bool
in_error_recursion_trouble(void)
{
	/* Pull the plug if recurse more than once */
	return (recursion_depth > 2);
}

/*
 * One of those fallback steps is to stop trying to localize the error
 * message, since there's a significant probability that that's exactly
 * what's causing the recursion.
 */
static inline const char *
err_gettext(const char *str)
{
#ifdef ENABLE_NLS
	if (in_error_recursion_trouble())
		return str;
	else
		return gettext(str);
#else
	return str;
#endif
}

/*
 * errstart_cold
 *		A simple wrapper around errstart, but hinted to be "cold".  Supporting
 *		compilers are more likely to move code for branches containing this
 *		function into an area away from the calling function's code.  This can
 *		result in more commonly executed code being more compact and fitting
 *		on fewer cache lines.
 */
pg_attribute_cold bool
errstart_cold(int elevel, const char *domain)
{
	return errstart(elevel, domain);
}

/*
 * errstart --- begin an error-reporting cycle
 *
 * Create and initialize error stack entry.  Subsequently, errmsg() and
 * perhaps other routines will be called to further populate the stack entry.
 * Finally, errfinish() will be called to actually process the error report.
 *
 * Returns true in normal case.  Returns false to short-circuit the error
 * report (if it's a warning or lower and not to be reported anywhere).
 */
bool
errstart(int elevel, const char *domain)
{
	ErrorData  *edata;
	bool		output_to_server;
	bool		output_to_client = false;
	int			i;

	Assert(!IsMultiThreadedMode());
	/*
	 * Check some cases in which we want to promote an error into a more
	 * severe error.  None of this logic applies for non-error messages.
	 */
	if (elevel >= ERROR)
	{
		/*
		 * If we are inside a critical section, all errors become PANIC
		 * errors.  See miscadmin.h.
		 */
		if (CritSectionCount > 0)
			elevel = PANIC;

		/*
		 * Check reasons for treating ERROR as FATAL:
		 *
		 * 1. we have no handler to pass the error to (implies we are in the
		 * postmaster or in backend startup).
		 *
		 * 2. ExitOnAnyError mode switch is set (initdb uses this).
		 *
		 * 3. the error occurred after proc_exit has begun to run.  (It's
		 * proc_exit's responsibility to see that this doesn't turn into
		 * infinite recursion!)
		 */
		if (elevel == ERROR)
		{
			if (PG_exception_stack == NULL ||
				ExitOnAnyError ||
				proc_exit_inprogress)
				elevel = FATAL;
		}

		/*
		 * If the error level is ERROR or more, errfinish is not going to
		 * return to caller; therefore, if there is any stacked error already
		 * in progress it will be lost.  This is more or less okay, except we
		 * do not want to have a FATAL or PANIC error downgraded because the
		 * reporting process was interrupted by a lower-grade error.  So check
		 * the stack and make sure we panic if panic is warranted.
		 */
		for (i = 0; i <= errordata_stack_depth; i++)
			elevel = Max(elevel, errordata[i].elevel);
	}

	/*
	 * Now decide whether we need to process this report at all; if it's
	 * warning or less and not enabled for logging, just return false without
	 * starting up any error logging machinery.
	 */
	output_to_server = should_output_to_server(elevel);
	output_to_client = should_output_to_client(elevel);
	if (elevel < ERROR && !output_to_server && !output_to_client)
		return false;

	/*
	 * We need to do some actual work.  Make sure that memory context
	 * initialization has finished, else we can't do anything useful.
	 */
	if (ErrorContext == NULL)
	{
		/* Oops, hard crash time; very little we can do safely here */
		write_stderr("error occurred before error message processing is available\n");
		exit(2);
	}

	/*
	 * Okay, crank up a stack entry to store the info in.
	 */

	if (recursion_depth++ > 0 && elevel >= ERROR)
	{
		/*
		 * Oops, error during error processing.  Clear ErrorContext as
		 * discussed at top of file.  We will not return to the original
		 * error's reporter or handler, so we don't need it.
		 */
		MemoryContextReset(ErrorContext);

		/*
		 * Infinite error recursion might be due to something broken in a
		 * context traceback routine.  Abandon them too.  We also abandon
		 * attempting to print the error statement (which, if long, could
		 * itself be the source of the recursive failure).
		 */
		if (in_error_recursion_trouble())
		{
			error_context_stack = NULL;
			debug_query_string = NULL;
		}
	}
	if (++errordata_stack_depth >= ERRORDATA_STACK_SIZE)
	{
		/*
		 * Wups, stack not big enough.  We treat this as a PANIC condition
		 * because it suggests an infinite loop of errors during error
		 * recovery.
		 */
		errordata_stack_depth = -1; /* make room on stack */
		ereport(PANIC, (errmsg_internal("ERRORDATA_STACK_SIZE exceeded")));
	}

	/* Initialize data for this error frame */
	edata = &errordata[errordata_stack_depth];
	MemSet(edata, 0, sizeof(ErrorData));
	edata->elevel = elevel;
	edata->output_to_server = output_to_server;
	edata->output_to_client = output_to_client;
	/* the default text domain is the backend's */
	edata->domain = domain ? domain : PG_TEXTDOMAIN("postgres");
	/* initialize context_domain the same way (see set_errcontext_domain()) */
	edata->context_domain = edata->domain;
	/* Select default errcode based on elevel */
	if (elevel >= ERROR)
		edata->sqlerrcode = ERRCODE_INTERNAL_ERROR;
	else if (elevel >= WARNING)
		edata->sqlerrcode = ERRCODE_WARNING;
	else
		edata->sqlerrcode = ERRCODE_SUCCESSFUL_COMPLETION;
	/* errno is saved here so that error parameter eval can't change it */
	edata->saved_errno = errno;

	/*
	 * Any allocations for this error state level should go into ErrorContext
	 */
	edata->assoc_context = ErrorContext;

	recursion_depth--;
	return true;
}

/*
 * Checks whether the given funcname matches backtrace_functions; see
 * check_backtrace_functions.
 */
static bool
matches_backtrace_functions(const char *funcname)
{
	char	   *p;

	if (!backtrace_symbol_list || funcname == NULL || funcname[0] == '\0')
		return false;

	p = backtrace_symbol_list;
	for (;;)
	{
		if (*p == '\0')			/* end of backtrace_symbol_list */
			break;

		if (strcmp(funcname, p) == 0)
			return true;
		p += strlen(p) + 1;
	}

	return false;
}

/*
 * errfinish --- end an error-reporting cycle
 *
 * Produce the appropriate error report(s) and pop the error stack.
 *
 * If elevel, as passed to errstart(), is ERROR or worse, control does not
 * return to the caller.  See elog.h for the error level definitions.
 */
void
errfinish(const char *filename, int lineno, const char *funcname)
{
	ErrorData  *edata = &errordata[errordata_stack_depth];
	int			elevel;
	MemoryContext oldcontext;
	ErrorContextCallback *econtext;

	recursion_depth++;
	CHECK_STACK_DEPTH();

	/*
	 * YB: in case yb_owns_file_and_func, filename and funcname were already
	 * set beforehand.
	 */
	if (IsYugaByteEnabled() && edata->yb_owns_file_and_func)
	{
		filename = edata->filename;
		funcname = edata->funcname;
	}

	/* Save the last few bits of error state into the stack entry */
	if (filename)
	{
		const char *slash;

		/* keep only base name, useful especially for vpath builds */
		slash = strrchr(filename, '/');
		if (slash)
			filename = slash + 1;
		/* Some Windows compilers use backslashes in __FILE__ strings */
		slash = strrchr(filename, '\\');
		if (slash)
			filename = slash + 1;
	}

	edata->filename = filename;
	edata->lineno = lineno;
	edata->funcname = funcname;

	elevel = edata->elevel;

	/*
	 * Do processing in ErrorContext, which we hope has enough reserved space
	 * to report an error.
	 */
	oldcontext = MemoryContextSwitchTo(ErrorContext);

	if (!edata->backtrace &&
		edata->funcname &&
		backtrace_functions &&
		matches_backtrace_functions(edata->funcname))
		set_backtrace(edata, 2);

	/*
	 * Call any context callback functions.  Errors occurring in callback
	 * functions will be treated as recursive errors --- this ensures we will
	 * avoid infinite recursion (see errstart).
	 */
	for (econtext = error_context_stack;
		 econtext != NULL;
		 econtext = econtext->previous)
		econtext->callback(econtext->arg);

	/*
	 * If ERROR (not more nor less) we pass it off to the current handler.
	 * Printing it and popping the stack is the responsibility of the handler.
	 */
	if (elevel == ERROR)
	{
		/*
		 * We do some minimal cleanup before longjmp'ing so that handlers can
		 * execute in a reasonably sane state.
		 *
		 * Reset InterruptHoldoffCount in case we ereport'd from inside an
		 * interrupt holdoff section.  (We assume here that no handler will
		 * itself be inside a holdoff section.  If necessary, such a handler
		 * could save and restore InterruptHoldoffCount for itself, but this
		 * should make life easier for most.)
		 */
		InterruptHoldoffCount = 0;
		QueryCancelHoldoffCount = 0;

		CritSectionCount = 0;	/* should be unnecessary, but... */

		/*
		 * Note that we leave GetCurrentMemoryContext() set to ErrorContext. The
		 * handler should reset it to something else soon.
		 */

		recursion_depth--;
		PG_RE_THROW();
	}

	/* YB_TODO(neil@yugabyte) Check if this is still needed and the right place for reporting */
	if (YBShouldLogStackTraceOnError() && elevel >= ERROR)
	{
		YBCLogImpl(2 /* severity (2=ERROR) */ , filename, lineno,
				   /* stack_trace */ true, "Postgres error: %s",
				   YBPgErrorLevelToString(elevel));
	}

	/* Emit the message to the right places */
	EmitErrorReport();

	/* Now free up subsidiary data attached to stack entry, and release it */
	if (edata->message)
		pfree(edata->message);
	if (edata->detail)
		pfree(edata->detail);
	if (edata->detail_log)
		pfree(edata->detail_log);
	if (edata->hint)
		pfree(edata->hint);
	if (edata->context)
		pfree(edata->context);
	if (edata->backtrace)
		pfree(edata->backtrace);
	if (edata->schema_name)
		pfree(edata->schema_name);
	if (edata->table_name)
		pfree(edata->table_name);
	if (edata->column_name)
		pfree(edata->column_name);
	if (edata->datatype_name)
		pfree(edata->datatype_name);
	if (edata->constraint_name)
		pfree(edata->constraint_name);
	if (edata->internalquery)
		pfree(edata->internalquery);

	errordata_stack_depth--;

	/* Exit error-handling context */
	MemoryContextSwitchTo(oldcontext);
	recursion_depth--;

	/*
	 * Perform error recovery action as specified by elevel.
	 */
	if (elevel == FATAL)
	{
		/*
		 * For a FATAL error, we let proc_exit clean up and exit.
		 *
		 * If we just reported a startup failure, the client will disconnect
		 * on receiving it, so don't send any more to the client.
		 */
		if (PG_exception_stack == NULL && whereToSendOutput == DestRemote)
			whereToSendOutput = DestNone;

		if (IsYugaByteEnabled())
			/* When it's FATAL, the memory context that "debug_query_string" points to might have been
			 * deleted or even corrupted. Set "debug_query_string" to NULL before emitting error.
			 * The variable "debug_query_string" contains the user statement that is currently executed.
			 */
			debug_query_string = NULL;

		/*
		 * fflush here is just to improve the odds that we get to see the
		 * error message, in case things are so hosed that proc_exit crashes.
		 * Any other code you might be tempted to add here should probably be
		 * in an on_proc_exit or on_shmem_exit callback instead.
		 */
		fflush(stdout);
		fflush(stderr);

		/*
		 * Let the cumulative stats system know. Only mark the session as
		 * terminated by fatal error if there is no other known cause.
		 */
		if (pgStatSessionEndCause == DISCONNECT_NORMAL)
			pgStatSessionEndCause = DISCONNECT_FATAL;

		/*
		 * Do normal process-exit cleanup, then return exit code 1 to indicate
		 * FATAL termination.  The postmaster may or may not consider this
		 * worthy of panic, depending on which subprocess returns it.
		 */
		proc_exit(1);
	}

	if (elevel >= PANIC)
	{
		/*
		 * Serious crash time. Postmaster will observe SIGABRT process exit
		 * status and kill the other backends too.
		 *
		 * XXX: what if we are *in* the postmaster?  abort() won't kill our
		 * children...
		 */
		fflush(stdout);
		fflush(stderr);
		abort();
	}

	/*
	 * Check for cancel/die interrupt first --- this is so that the user can
	 * stop a query emitting tons of notice or warning messages, even if it's
	 * in a loop that otherwise fails to check for interrupts.
	 */
	CHECK_FOR_INTERRUPTS();
}


/*
 * errcode --- add SQLSTATE error code to the current error
 *
 * The code is expected to be represented as per MAKE_SQLSTATE().
 * In YbGate environment set the code on the current YbgError frame.
 */
int
errcode(int sqlerrcode)
{
	if (IsMultiThreadedMode())
	{
		YbgStatus		status = YBCPgGetThreadLocalErrStatus();
		YbgError		edata = (YbgError) YbgStatusGetEdata(status);
		edata->sqlerrcode = sqlerrcode;
		return 0;
	}

	ErrorData  *edata = &errordata[errordata_stack_depth];

	/* we don't bother incrementing recursion_depth */
	CHECK_STACK_DEPTH();

	edata->sqlerrcode = sqlerrcode;

	return 0;					/* return value does not matter */
}

int
yb_txn_errcode(uint16_t txn_errcode)
{
	RETURN_IF_MULTITHREADED_MODE();

	ErrorData  *edata = &errordata[errordata_stack_depth];

	/* we don't bother incrementing recursion_depth */
	CHECK_STACK_DEPTH();

	edata->yb_txn_errcode = txn_errcode;

	return 0;					/* return value does not matter */
}


/*
 * errcode_for_file_access --- add SQLSTATE error code to the current error
 *
 * The SQLSTATE code is chosen based on the saved errno value.  We assume
 * that the failing operation was some type of disk file access.
 *
 * NOTE: the primary error message string should generally include %m
 * when this is used.
 */
int
errcode_for_file_access(void)
{
	RETURN_IF_MULTITHREADED_MODE();

	ErrorData  *edata = &errordata[errordata_stack_depth];

	/* we don't bother incrementing recursion_depth */
	CHECK_STACK_DEPTH();

	switch (edata->saved_errno)
	{
			/* Permission-denied failures */
		case EPERM:				/* Not super-user */
		case EACCES:			/* Permission denied */
#ifdef EROFS
		case EROFS:				/* Read only file system */
#endif
			edata->sqlerrcode = ERRCODE_INSUFFICIENT_PRIVILEGE;
			break;

			/* File not found */
		case ENOENT:			/* No such file or directory */
			edata->sqlerrcode = ERRCODE_UNDEFINED_FILE;
			break;

			/* Duplicate file */
		case EEXIST:			/* File exists */
			edata->sqlerrcode = ERRCODE_DUPLICATE_FILE;
			break;

			/* Wrong object type or state */
		case ENOTDIR:			/* Not a directory */
		case EISDIR:			/* Is a directory */
#if defined(ENOTEMPTY) && (ENOTEMPTY != EEXIST) /* same code on AIX */
		case ENOTEMPTY:			/* Directory not empty */
#endif
			edata->sqlerrcode = ERRCODE_WRONG_OBJECT_TYPE;
			break;

			/* Insufficient resources */
		case ENOSPC:			/* No space left on device */
			edata->sqlerrcode = ERRCODE_DISK_FULL;
			break;

		case ENFILE:			/* File table overflow */
		case EMFILE:			/* Too many open files */
			edata->sqlerrcode = ERRCODE_INSUFFICIENT_RESOURCES;
			break;

			/* Hardware failure */
		case EIO:				/* I/O error */
			edata->sqlerrcode = ERRCODE_IO_ERROR;
			break;

			/* All else is classified as internal errors */
		default:
			edata->sqlerrcode = ERRCODE_INTERNAL_ERROR;
			break;
	}

	return 0;					/* return value does not matter */
}

/*
 * errcode_for_socket_access --- add SQLSTATE error code to the current error
 *
 * The SQLSTATE code is chosen based on the saved errno value.  We assume
 * that the failing operation was some type of socket access.
 *
 * NOTE: the primary error message string should generally include %m
 * when this is used.
 */
int
errcode_for_socket_access(void)
{
	RETURN_IF_MULTITHREADED_MODE();

	ErrorData  *edata = &errordata[errordata_stack_depth];

	/* we don't bother incrementing recursion_depth */
	CHECK_STACK_DEPTH();

	switch (edata->saved_errno)
	{
			/* Loss of connection */
		case ALL_CONNECTION_FAILURE_ERRNOS:
			edata->sqlerrcode = ERRCODE_CONNECTION_FAILURE;
			break;

			/* All else is classified as internal errors */
		default:
			edata->sqlerrcode = ERRCODE_INTERNAL_ERROR;
			break;
	}

	return 0;					/* return value does not matter */
}


/*
 * This macro handles expansion of a format string and associated parameters;
 * it's common code for errmsg(), errdetail(), etc.  Must be called inside
 * a routine that is declared like "const char *fmt, ..." and has an edata
 * pointer set up.  The message is assigned to edata->targetfield, or
 * appended to it if appendval is true.  The message is subject to translation
 * if translateit is true.
 *
 * Note: we pstrdup the buffer rather than just transferring its storage
 * to the edata field because the buffer might be considerably larger than
 * really necessary.
 */
#define EVALUATE_MESSAGE(domain, targetfield, appendval, translateit)	\
	{ \
		StringInfoData	buf; \
		/* Internationalize the error format string */ \
		if ((translateit) && !in_error_recursion_trouble()) \
			fmt = dgettext((domain), fmt);				  \
		initStringInfo(&buf); \
		if ((appendval) && edata->targetfield) { \
			appendStringInfoString(&buf, edata->targetfield); \
			appendStringInfoChar(&buf, '\n'); \
		} \
		/* Generate actual output --- have to use appendStringInfoVA */ \
		for (;;) \
		{ \
			va_list		args; \
			int			needed; \
			errno = edata->saved_errno; \
			va_start(args, fmt); \
			needed = appendStringInfoVA(&buf, fmt, args); \
			va_end(args); \
			if (needed == 0) \
				break; \
			enlargeStringInfo(&buf, needed); \
		} \
		/* Save the completed message into the stack item */ \
		if (edata->targetfield) \
			pfree(edata->targetfield); \
		edata->targetfield = pstrdup(buf.data); \
		pfree(buf.data); \
	}

/*
 * Same as above, except for pluralized error messages.  The calling routine
 * must be declared like "const char *fmt_singular, const char *fmt_plural,
 * unsigned long n, ...".  Translation is assumed always wanted.
 */
#define EVALUATE_MESSAGE_PLURAL(domain, targetfield, appendval)  \
	{ \
		const char	   *fmt; \
		StringInfoData	buf; \
		/* Internationalize the error format string */ \
		if (!in_error_recursion_trouble()) \
			fmt = dngettext((domain), fmt_singular, fmt_plural, n); \
		else \
			fmt = (n == 1 ? fmt_singular : fmt_plural); \
		initStringInfo(&buf); \
		if ((appendval) && edata->targetfield) { \
			appendStringInfoString(&buf, edata->targetfield); \
			appendStringInfoChar(&buf, '\n'); \
		} \
		/* Generate actual output --- have to use appendStringInfoVA */ \
		for (;;) \
		{ \
			va_list		args; \
			int			needed; \
			errno = edata->saved_errno; \
			va_start(args, n); \
			needed = appendStringInfoVA(&buf, fmt, args); \
			va_end(args); \
			if (needed == 0) \
				break; \
			enlargeStringInfo(&buf, needed); \
		} \
		/* Save the completed message into the stack item */ \
		if (edata->targetfield) \
			pfree(edata->targetfield); \
		edata->targetfield = pstrdup(buf.data); \
		pfree(buf.data); \
	}


/*
 * errmsg --- add a primary error message text to the current error
 *
 * In addition to the usual %-escapes recognized by printf, "%m" in
 * fmt is replaced by the error message for the caller's value of errno.
 *
 * Note: no newline is needed at the end of the fmt string, since
 * ereport will provide one for the output methods that need it.
 */
int
errmsg(const char *fmt,...)
{
	if (IsMultiThreadedMode())
	{
		va_list args;
		/*
		 * Update current error message.
		 * Will also be logged to tserver log as part of yb_errfinish.
		 */
		va_start(args, fmt);
		yb_errmsg_va(fmt, args);
		va_end(args);
		return 0;
	}

	ErrorData  *edata = &errordata[errordata_stack_depth];
	MemoryContext oldcontext;

	recursion_depth++;
	CHECK_STACK_DEPTH();
	oldcontext = MemoryContextSwitchTo(edata->assoc_context);

	edata->message_id = fmt;
	EVALUATE_MESSAGE(edata->domain, message, false, true);

	if (IsYugaByteEnabled() && yb_debug_report_error_stacktrace)
		yb_additional_errmsg("%s", YBCGetStackTrace());

	MemoryContextSwitchTo(oldcontext);
	recursion_depth--;
	return 0;					/* return value does not matter */
}

/*
 * Add a backtrace to the containing ereport() call.  This is intended to be
 * added temporarily during debugging.
 */
int
errbacktrace(void)
{
	ErrorData  *edata = &errordata[errordata_stack_depth];
	MemoryContext oldcontext;

	recursion_depth++;
	CHECK_STACK_DEPTH();
	oldcontext = MemoryContextSwitchTo(edata->assoc_context);

	set_backtrace(edata, 1);

	MemoryContextSwitchTo(oldcontext);
	recursion_depth--;

	return 0;
}

/*
 * Compute backtrace data and add it to the supplied ErrorData.  num_skip
 * specifies how many inner frames to skip.  Use this to avoid showing the
 * internal backtrace support functions in the backtrace.  This requires that
 * this and related functions are not inlined.
 */
static void
set_backtrace(ErrorData *edata, int num_skip)
{
	StringInfoData errtrace;

	initStringInfo(&errtrace);

#ifdef HAVE_BACKTRACE_SYMBOLS
	{
		void	   *buf[100];
		int			nframes;
		char	  **strfrms;

		nframes = backtrace(buf, lengthof(buf));
		strfrms = backtrace_symbols(buf, nframes);
		if (strfrms == NULL)
			return;

		for (int i = num_skip; i < nframes; i++)
			appendStringInfo(&errtrace, "\n%s", strfrms[i]);
		free(strfrms);
	}
#else
	appendStringInfoString(&errtrace,
						   "backtrace generation is not supported by this installation");
#endif

	edata->backtrace = errtrace.data;
}

/*
 * errmsg_internal --- add a primary error message text to the current error
 *
 * This is exactly like errmsg() except that strings passed to errmsg_internal
 * are not translated, and are customarily left out of the
 * internationalization message dictionary.  This should be used for "can't
 * happen" cases that are probably not worth spending translation effort on.
 * We also use this for certain cases where we *must* not try to translate
 * the message because the translation would fail and result in infinite
 * error recursion.
 */
int
errmsg_internal(const char *fmt,...)
{
	if (IsMultiThreadedMode())
	{
		va_list args;
		/*
		 * Update current error message.
		 * Will also be logged to tserver log as part of yb_errfinish.
		 */
		va_start(args, fmt);
		yb_errmsg_va(fmt, args);
		va_end(args);
		return 0;
	}

	ErrorData  *edata = &errordata[errordata_stack_depth];
	MemoryContext oldcontext;

	recursion_depth++;
	CHECK_STACK_DEPTH();
	oldcontext = MemoryContextSwitchTo(edata->assoc_context);

	edata->message_id = fmt;
	EVALUATE_MESSAGE(edata->domain, message, false, false);

	if (IsYugaByteEnabled() && yb_debug_report_error_stacktrace)
		yb_additional_errmsg("%s", YBCGetStackTrace());

	MemoryContextSwitchTo(oldcontext);
	recursion_depth--;
	return 0;					/* return value does not matter */
}


/*
 * errmsg_plural --- add a primary error message text to the current error,
 * with support for pluralization of the message text
 *
 * TODO: There is a correctness problem in the YbGate environment. National
 * languages may have multiple plural forms, depending on n. Therefore n
 * should be stored and sent to the client with the Status.
 *
 * However, errmsg_plural is rare, and the "if n = 1 then singular, else plural"
 * logic works for English, hence low priority.
 */
int
errmsg_plural(const char *fmt_singular, const char *fmt_plural,
			  unsigned long n,...)
{
	if (IsMultiThreadedMode())
	{
		const char *fmt = n == 1 ? fmt_singular : fmt_plural;
		va_list args;
		/*
		 * Update current error message.
		 * Will also be logged to tserver log in yb_errfinish.
		 */
		va_start(args, n);
		yb_errmsg_va(fmt, args);
		va_end(args);
		return 0;
	}

	ErrorData  *edata = &errordata[errordata_stack_depth];
	MemoryContext oldcontext;

	recursion_depth++;
	CHECK_STACK_DEPTH();
	oldcontext = MemoryContextSwitchTo(edata->assoc_context);

	edata->message_id = fmt_singular;
	EVALUATE_MESSAGE_PLURAL(edata->domain, message, false);

	if (IsYugaByteEnabled() && yb_debug_report_error_stacktrace)
		yb_additional_errmsg("%s", YBCGetStackTrace());

	MemoryContextSwitchTo(oldcontext);
	recursion_depth--;
	return 0;					/* return value does not matter */
}


/*
 * errdetail --- add a detail error message text to the current error
 */
int
errdetail(const char *fmt,...)
{
	RETURN_IF_MULTITHREADED_MODE();

	ErrorData  *edata = &errordata[errordata_stack_depth];
	MemoryContext oldcontext;

	recursion_depth++;
	CHECK_STACK_DEPTH();
	oldcontext = MemoryContextSwitchTo(edata->assoc_context);

	EVALUATE_MESSAGE(edata->domain, detail, false, true);

	MemoryContextSwitchTo(oldcontext);
	recursion_depth--;
	return 0;					/* return value does not matter */
}


/*
 * errdetail_internal --- add a detail error message text to the current error
 *
 * This is exactly like errdetail() except that strings passed to
 * errdetail_internal are not translated, and are customarily left out of the
 * internationalization message dictionary.  This should be used for detail
 * messages that seem not worth translating for one reason or another
 * (typically, that they don't seem to be useful to average users).
 */
int
errdetail_internal(const char *fmt,...)
{
	RETURN_IF_MULTITHREADED_MODE();

	ErrorData  *edata = &errordata[errordata_stack_depth];
	MemoryContext oldcontext;

	recursion_depth++;
	CHECK_STACK_DEPTH();
	oldcontext = MemoryContextSwitchTo(edata->assoc_context);

	EVALUATE_MESSAGE(edata->domain, detail, false, false);

	MemoryContextSwitchTo(oldcontext);
	recursion_depth--;
	return 0;					/* return value does not matter */
}


/*
 * errdetail_log --- add a detail_log error message text to the current error
 */
int
errdetail_log(const char *fmt,...)
{
	RETURN_IF_MULTITHREADED_MODE();

	ErrorData  *edata = &errordata[errordata_stack_depth];
	MemoryContext oldcontext;

	recursion_depth++;
	CHECK_STACK_DEPTH();
	oldcontext = MemoryContextSwitchTo(edata->assoc_context);

	EVALUATE_MESSAGE(edata->domain, detail_log, false, true);

	MemoryContextSwitchTo(oldcontext);
	recursion_depth--;
	return 0;					/* return value does not matter */
}

/*
 * errdetail_log_plural --- add a detail_log error message text to the current error
 * with support for pluralization of the message text
 */
int
errdetail_log_plural(const char *fmt_singular, const char *fmt_plural,
					 unsigned long n,...)
{
	RETURN_IF_MULTITHREADED_MODE();

	ErrorData  *edata = &errordata[errordata_stack_depth];
	MemoryContext oldcontext;

	recursion_depth++;
	CHECK_STACK_DEPTH();
	oldcontext = MemoryContextSwitchTo(edata->assoc_context);

	EVALUATE_MESSAGE_PLURAL(edata->domain, detail_log, false);

	MemoryContextSwitchTo(oldcontext);
	recursion_depth--;
	return 0;					/* return value does not matter */
}


/*
 * errdetail_plural --- add a detail error message text to the current error,
 * with support for pluralization of the message text
 */
int
errdetail_plural(const char *fmt_singular, const char *fmt_plural,
				 unsigned long n,...)
{
	RETURN_IF_MULTITHREADED_MODE();

	ErrorData  *edata = &errordata[errordata_stack_depth];
	MemoryContext oldcontext;

	recursion_depth++;
	CHECK_STACK_DEPTH();
	oldcontext = MemoryContextSwitchTo(edata->assoc_context);

	EVALUATE_MESSAGE_PLURAL(edata->domain, detail, false);

	MemoryContextSwitchTo(oldcontext);
	recursion_depth--;
	return 0;					/* return value does not matter */
}


/*
 * errhint --- add a hint error message text to the current error
 */
int
errhint(const char *fmt,...)
{
	RETURN_IF_MULTITHREADED_MODE();

	ErrorData  *edata = &errordata[errordata_stack_depth];
	MemoryContext oldcontext;

	recursion_depth++;
	CHECK_STACK_DEPTH();
	oldcontext = MemoryContextSwitchTo(edata->assoc_context);

	EVALUATE_MESSAGE(edata->domain, hint, false, true);

	MemoryContextSwitchTo(oldcontext);
	recursion_depth--;
	return 0;					/* return value does not matter */
}

/*
 * errhint_plural --- add a hint error message text to the current error,
 * with support for pluralization of the message text
 */
int
errhint_plural(const char *fmt_singular, const char *fmt_plural,
			   unsigned long n,...)
{
	ErrorData  *edata = &errordata[errordata_stack_depth];
	MemoryContext oldcontext;

	recursion_depth++;
	CHECK_STACK_DEPTH();
	oldcontext = MemoryContextSwitchTo(edata->assoc_context);

	EVALUATE_MESSAGE_PLURAL(edata->domain, hint, false);

	MemoryContextSwitchTo(oldcontext);
	recursion_depth--;
	return 0;					/* return value does not matter */
}

/*
 * errcontext_msg --- add a context error message text to the current error
 *
 * Unlike other cases, multiple calls are allowed to build up a stack of
 * context information.  We assume earlier calls represent more-closely-nested
 * states.
 *
 * Ignored in YbGate environment.
 */
int
errcontext_msg(const char *fmt, ...)
{
	RETURN_IF_MULTITHREADED_MODE();

	ErrorData  *edata = &errordata[errordata_stack_depth];
	MemoryContext oldcontext;

	recursion_depth++;
	CHECK_STACK_DEPTH();
	oldcontext = MemoryContextSwitchTo(edata->assoc_context);

	EVALUATE_MESSAGE(edata->context_domain, context, true, true);

	MemoryContextSwitchTo(oldcontext);
	recursion_depth--;
	return 0;					/* return value does not matter */
}

/*
 * set_errcontext_domain --- set message domain to be used by errcontext()
 *
 * errcontext_msg() can be called from a different module than the original
 * ereport(), so we cannot use the message domain passed in errstart() to
 * translate it.  Instead, each errcontext_msg() call should be preceded by
 * a set_errcontext_domain() call to specify the domain.  This is usually
 * done transparently by the errcontext() macro.
 *
 * Ignored in YbGate environment.
 */
int
set_errcontext_domain(const char *domain)
{
	RETURN_IF_MULTITHREADED_MODE();

	ErrorData  *edata = &errordata[errordata_stack_depth];

	/* we don't bother incrementing recursion_depth */
	CHECK_STACK_DEPTH();

	/* the default text domain is the backend's */
	edata->context_domain = domain ? domain : PG_TEXTDOMAIN("postgres");

	return 0;					/* return value does not matter */
}

/*
 * errhidestmt --- optionally suppress STATEMENT: field of log entry
 *
 * This should be called if the message text already includes the statement.
 *
 * Ignored in YbGate environment.
 */
int
errhidestmt(bool hide_stmt)
{
	RETURN_IF_MULTITHREADED_MODE();

	ErrorData  *edata = &errordata[errordata_stack_depth];

	/* we don't bother incrementing recursion_depth */
	CHECK_STACK_DEPTH();

	edata->hide_stmt = hide_stmt;

	return 0;					/* return value does not matter */
}

/*
 * errhidecontext --- optionally suppress CONTEXT: field of log entry
 *
 * This should only be used for verbose debugging messages where the repeated
 * inclusion of context would bloat the log volume too much.
 *
 * Ignored in YbGate environment.
 */
int
errhidecontext(bool hide_ctx)
{
	RETURN_IF_MULTITHREADED_MODE();

	ErrorData  *edata = &errordata[errordata_stack_depth];

	/* we don't bother incrementing recursion_depth */
	CHECK_STACK_DEPTH();

	edata->hide_ctx = hide_ctx;

	return 0;					/* return value does not matter */
}

/*
 * errposition --- add cursor position to the current error
 *
 * Ignored in YbGate environment.
 */
int
errposition(int cursorpos)
{
	RETURN_IF_MULTITHREADED_MODE();

	ErrorData  *edata = &errordata[errordata_stack_depth];

	/* we don't bother incrementing recursion_depth */
	CHECK_STACK_DEPTH();

	edata->cursorpos = cursorpos;

	return 0;					/* return value does not matter */
}

/*
 * internalerrposition --- add internal cursor position to the current error
 *
 * Ignored in YbGate environment.
 */
int
internalerrposition(int cursorpos)
{
	RETURN_IF_MULTITHREADED_MODE();

	ErrorData  *edata = &errordata[errordata_stack_depth];

	/* we don't bother incrementing recursion_depth */
	CHECK_STACK_DEPTH();

	edata->internalpos = cursorpos;

	return 0;					/* return value does not matter */
}

/*
 * internalerrquery --- add internal query text to the current error
 *
 * Can also pass NULL to drop the internal query text entry.  This case
 * is intended for use in error callback subroutines that are editorializing
 * on the layout of the error report.
 *
 * Ignored in YbGate environment.
 */
int
internalerrquery(const char *query)
{
	RETURN_IF_MULTITHREADED_MODE();

	ErrorData  *edata = &errordata[errordata_stack_depth];

	/* we don't bother incrementing recursion_depth */
	CHECK_STACK_DEPTH();

	if (edata->internalquery)
	{
		pfree(edata->internalquery);
		edata->internalquery = NULL;
	}

	if (query)
		edata->internalquery = MemoryContextStrdup(edata->assoc_context, query);

	return 0;					/* return value does not matter */
}

/*
 * err_generic_string -- used to set individual ErrorData string fields
 * identified by PG_DIAG_xxx codes.
 *
 * This intentionally only supports fields that don't use localized strings,
 * so that there are no translation considerations.
 *
 * Most potential callers should not use this directly, but instead prefer
 * higher-level abstractions, such as errtablecol() (see relcache.c).
 *
 * Ignored in YbGate environment.
 */
int
err_generic_string(int field, const char *str)
{
	RETURN_IF_MULTITHREADED_MODE();

	ErrorData  *edata = &errordata[errordata_stack_depth];

	/* we don't bother incrementing recursion_depth */
	CHECK_STACK_DEPTH();

	switch (field)
	{
		case PG_DIAG_SCHEMA_NAME:
			set_errdata_field(edata->assoc_context, &edata->schema_name, str);
			break;
		case PG_DIAG_TABLE_NAME:
			set_errdata_field(edata->assoc_context, &edata->table_name, str);
			break;
		case PG_DIAG_COLUMN_NAME:
			set_errdata_field(edata->assoc_context, &edata->column_name, str);
			break;
		case PG_DIAG_DATATYPE_NAME:
			set_errdata_field(edata->assoc_context, &edata->datatype_name, str);
			break;
		case PG_DIAG_CONSTRAINT_NAME:
			set_errdata_field(edata->assoc_context, &edata->constraint_name, str);
			break;
		default:
			elog(ERROR, "unsupported ErrorData field id: %d", field);
			break;
	}

	return 0;					/* return value does not matter */
}

/*
 * set_errdata_field --- set an ErrorData string field
 */
static void
set_errdata_field(MemoryContextData *cxt, char **ptr, const char *str)
{
	Assert(*ptr == NULL);
	*ptr = MemoryContextStrdup(cxt, str);
}

/*
 * geterrcode --- return the currently set SQLSTATE error code
 *
 * This is only intended for use in error callback subroutines, since there
 * is no other place outside elog.c where the concept is meaningful.
 */
int
geterrcode(void)
{
	ErrorData  *edata = &errordata[errordata_stack_depth];

	/* we don't bother incrementing recursion_depth */
	CHECK_STACK_DEPTH();

	return edata->sqlerrcode;
}

/*
 * geterrposition --- return the currently set error position (0 if none)
 *
 * This is only intended for use in error callback subroutines, since there
 * is no other place outside elog.c where the concept is meaningful.
 */
int
geterrposition(void)
{
	ErrorData  *edata = &errordata[errordata_stack_depth];

	/* we don't bother incrementing recursion_depth */
	CHECK_STACK_DEPTH();

	return edata->cursorpos;
}

/*
 * getinternalerrposition --- same for internal error position
 *
 * This is only intended for use in error callback subroutines, since there
 * is no other place outside elog.c where the concept is meaningful.
 */
int
getinternalerrposition(void)
{
	ErrorData  *edata = &errordata[errordata_stack_depth];

	/* we don't bother incrementing recursion_depth */
	CHECK_STACK_DEPTH();

	return edata->internalpos;
}

/* YB_TODO(neil)
 *  - Check if the following assert needs to be called in Pg15 elog functions.
 *  - It was called in pg11 elog function for Yugabyte.
 *	Assert(!IsMultiThreadedMode());
 */
/*
 * Functions to allow construction of error message strings separately from
 * the ereport() call itself.
 *
 * The expected calling convention is
 *
 *	pre_format_elog_string(errno, domain), var = format_elog_string(format,...)
 *
 * which can be hidden behind a macro such as GUC_check_errdetail().  We
 * assume that any functions called in the arguments of format_elog_string()
 * cannot result in re-entrant use of these functions --- otherwise the wrong
 * text domain might be used, or the wrong errno substituted for %m.  This is
 * okay for the current usage with GUC check hooks, but might need further
 * effort someday.
 *
 * The result of format_elog_string() is stored in ErrorContext, and will
 * therefore survive until FlushErrorState() is called.
 */
static int	save_format_errnumber;
static const char *save_format_domain;

void
pre_format_elog_string(int errnumber, const char *domain)
{
	/* Save errno before evaluation of argument functions can change it */
	save_format_errnumber = errnumber;
	/* Save caller's text domain */
	save_format_domain = domain;
}

char *
format_elog_string(const char *fmt,...)
{
	ErrorData	errdata;
	ErrorData  *edata;
	MemoryContext oldcontext;

	/* Initialize a mostly-dummy error frame */
	edata = &errdata;
	MemSet(edata, 0, sizeof(ErrorData));
	/* the default text domain is the backend's */
	edata->domain = save_format_domain ? save_format_domain : PG_TEXTDOMAIN("postgres");
	/* set the errno to be used to interpret %m */
	edata->saved_errno = save_format_errnumber;

	oldcontext = MemoryContextSwitchTo(ErrorContext);

	edata->message_id = fmt;
	EVALUATE_MESSAGE(edata->domain, message, false, true);

	if (IsYugaByteEnabled() && yb_debug_report_error_stacktrace)
		yb_additional_errmsg("%s", YBCGetStackTrace());

	MemoryContextSwitchTo(oldcontext);

	return edata->message;
}

/*
 * yb_get_exception_stack
 * Return current PG_exception stack value, or equivalent thread local value in
 * YbGate environment.
 */
sigjmp_buf *
yb_get_exception_stack(void)
{
	if (IsMultiThreadedMode())
		return (sigjmp_buf *) YBCPgGetThreadLocalJumpBuffer();
	else
		return PG_exception_stack;
}

/*
 * yb_set_exception_stack
 * Set current PG_exception stack value, or equivalent thread local value in
 * YbGate environment.
 */
void
yb_set_exception_stack(sigjmp_buf *new_sigjmp_buf)
{
	if (IsMultiThreadedMode())
		YBCPgSetThreadLocalJumpBuffer(new_sigjmp_buf);
	else
		PG_exception_stack = new_sigjmp_buf;
}

/*
 * yb_reset_error_status - clean error details from the YbgStatus
 * In YbGate environment clean up YbgStatus instance after error is
 * successfully handled.
 *
 * Current error status could be nested, in that case just reset values
 * transferred from the last YbgError and let the current YbgError frame be
 * populated.
 *
 * If last error was outmost, destroy the YbgError and deallocate memory.
 */
void
yb_reset_error_status(void)
{
	if (IsMultiThreadedMode())
	{
		YbgStatus ybg_status = YBCPgSetThreadLocalErrStatus(NULL);
		if (ybg_status && YbgStatusGetEdata(ybg_status))
		{
			/*
			 * Fields cleaned up here should match those set in
			 * the yb_copy_edata_fields_to_status function
			 */
			YbgStatusSetLevel(ybg_status, 0);
			YbgStatusSetMessageAndArgs(ybg_status, NULL, 0, NULL);
			YbgStatusSetSqlError(ybg_status, ERRCODE_SUCCESSFUL_COMPLETION);
			YbgStatusSetFilename(ybg_status, NULL);
			YbgStatusSetLineNumber(ybg_status, 0);
			YbgStatusSetFuncname(ybg_status, NULL);
			/* Store back the cleaned status */
			YBCPgSetThreadLocalErrStatus(ybg_status);
		}
		else
			YbgStatusDestroy(ybg_status);
	}
}

/*
 * Actual output of the top-of-stack error message
 *
 * In the ereport(ERROR) case this is called from PostgresMain (or not at all,
 * if the error is caught by somebody).  For all other severity levels this
 * is called by errfinish.
 */
void
EmitErrorReport(void)
{
	ErrorData  *edata = &errordata[errordata_stack_depth];
	MemoryContext oldcontext;

	recursion_depth++;
	CHECK_STACK_DEPTH();
	oldcontext = MemoryContextSwitchTo(edata->assoc_context);

	/*
	 * Call hook before sending message to log.  The hook function is allowed
	 * to turn off edata->output_to_server, so we must recheck that afterward.
	 * Making any other change in the content of edata is not considered
	 * supported.
	 *
	 * Note: the reason why the hook can only turn off output_to_server, and
	 * not turn it on, is that it'd be unreliable: we will never get here at
	 * all if errstart() deems the message uninteresting.  A hook that could
	 * make decisions in that direction would have to hook into errstart(),
	 * where it would have much less information available.  emit_log_hook is
	 * intended for custom log filtering and custom log message transmission
	 * mechanisms.
	 *
	 * The log hook has access to both the translated and original English
	 * error message text, which is passed through to allow it to be used as a
	 * message identifier. Note that the original text is not available for
	 * detail, detail_log, hint and context text elements.
	 */
	if (edata->output_to_server && emit_log_hook)
		(*emit_log_hook) (edata);

	/* Send to server log, if enabled */
	if (edata->output_to_server)
		send_message_to_server_log(edata);

	/* Send to client, if enabled */
	if (edata->output_to_client)
		send_message_to_frontend(edata);

	MemoryContextSwitchTo(oldcontext);
	recursion_depth--;
}

/*
 * ybg_status_to_edata - export info from YbgStatus as ErrorData
 *
 * Equivalent of CopyErrorData for multi-thread mode.
 */
static ErrorData *
ybg_status_to_edata(void)
{
	YbgStatus ybg_status = YBCPgGetThreadLocalErrStatus();
	MemoryContext mctx = GetCurrentMemoryContext();
	/*
	 * In multi-thread mode current memory context may be null, as well as the
	 * error context. Here we require they both are set and are different, since
	 * we are allocating, and expect the error context to be deleted soon.
	 */
	Assert(mctx && YbgStatusGetContext(ybg_status) != mctx);
	/* We do not set most of the field, so nullify the allocated struct */
	ErrorData *newedata = (ErrorData *) palloc0(sizeof(ErrorData));
	/* Assuming we are in PG_CATCH, it only can be ERROR */
	newedata->elevel = ERROR;
	/* Copy error message. Have to format it now. */
	const char *fmt = YbgStatusGetMessage(ybg_status);
	if (fmt)
	{
		StringInfoData buf;
		int nargs;
		const char **args = YbgStatusGetMessageArgs(ybg_status, &nargs);
		initStringInfo(&buf);
		yb_format_and_append(&buf, fmt, nargs, args);
		newedata->message_id = pstrdup(fmt);
		newedata->message = buf.data;
	}
	/* Copy SQL code */
	newedata->sqlerrcode = YbgStatusGetSqlError(ybg_status);
	/* Copy location */
	newedata->filename = YbgStatusGetFilename(ybg_status);
	newedata->lineno = YbgStatusGetLineNumber(ybg_status);
	newedata->funcname = YbgStatusGetFuncname(ybg_status);
	/* Associate current memory context with this edata */
	newedata->assoc_context = mctx;
	return newedata;
}

/*
 * ybg_status_from_edata - import info from ErrorData into YbgStatus
 *
 * Raise error using the information, previously exported as an ErrorData
 * structure in multi-thread mode.
 */
static void
ybg_status_from_edata(ErrorData *edata)
{
	/* Start error processing */
	if (!yb_errstart(edata->elevel))
		return;
	/* Successful error start makes sure we have memory context and YbgError */
	YbgStatus ybg_status = YBCPgGetThreadLocalErrStatus();
	YbgError newedata = (YbgError) YbgStatusGetEdata(ybg_status);
	MemoryContext error_context =
		(MemoryContext) YbgStatusGetContext(ybg_status);
	MemoryContext old_context = MemoryContextSwitchTo(error_context);
	/* Error message raised that way is not localizable unfortunately */
	newedata->errmsg = pstrdup(edata->message);
	newedata->sqlerrcode = edata->sqlerrcode;
	MemoryContextSwitchTo(old_context);
	/* Process the error */
	yb_errfinish(edata->filename, edata->lineno, edata->funcname);
}

/*
 * CopyErrorData --- obtain a copy of the topmost error stack entry
 *
 * This is only for use in error handler code.  The data is copied into the
 * current memory context, so callers should always switch away from
 * ErrorContext first; otherwise it will be lost when FlushErrorState is done.
 *
 * In multi-thread mode we expect to have a YbgStatus instance which we can
 * use to populate ErrorData structure to possibly make Postgres code happy.
 * Though we do not have everything that may be needed, we do our best.
 */
ErrorData *
CopyErrorData(void)
{
	if (IsMultiThreadedMode())
		return ybg_status_to_edata();

	ErrorData  *edata = &errordata[errordata_stack_depth];
	ErrorData  *newedata;

	/*
	 * we don't increment recursion_depth because out-of-memory here does not
	 * indicate a problem within the error subsystem.
	 */
	CHECK_STACK_DEPTH();

	Assert(GetCurrentMemoryContext() != ErrorContext);

	/* Copy the struct itself */
	newedata = (ErrorData *) palloc(sizeof(ErrorData));
	memcpy(newedata, edata, sizeof(ErrorData));

	/* Make copies of separately-allocated fields */
	if (newedata->message)
		newedata->message = pstrdup(newedata->message);
	if (newedata->detail)
		newedata->detail = pstrdup(newedata->detail);
	if (newedata->detail_log)
		newedata->detail_log = pstrdup(newedata->detail_log);
	if (newedata->hint)
		newedata->hint = pstrdup(newedata->hint);
	if (newedata->context)
		newedata->context = pstrdup(newedata->context);
	if (newedata->backtrace)
		newedata->backtrace = pstrdup(newedata->backtrace);
	if (newedata->schema_name)
		newedata->schema_name = pstrdup(newedata->schema_name);
	if (newedata->table_name)
		newedata->table_name = pstrdup(newedata->table_name);
	if (newedata->column_name)
		newedata->column_name = pstrdup(newedata->column_name);
	if (newedata->datatype_name)
		newedata->datatype_name = pstrdup(newedata->datatype_name);
	if (newedata->constraint_name)
		newedata->constraint_name = pstrdup(newedata->constraint_name);
	if (newedata->internalquery)
		newedata->internalquery = pstrdup(newedata->internalquery);

	if (newedata->yb_owns_file_and_func)
	{
		if (newedata->filename)
			newedata->filename = pstrdup(newedata->filename);
		if (newedata->funcname)
			newedata->funcname = pstrdup(newedata->funcname);
	}

	/* Use the calling context for string allocation */
	newedata->assoc_context = GetCurrentMemoryContext();

	return newedata;
}

/*
 * FreeErrorData --- free the structure returned by CopyErrorData.
 *
 * Error handlers should use this in preference to assuming they know all
 * the separately-allocated fields.
 */
void
FreeErrorData(ErrorData *edata)
{
	if (edata->message)
		pfree(edata->message);
	if (edata->detail)
		pfree(edata->detail);
	if (edata->detail_log)
		pfree(edata->detail_log);
	if (edata->hint)
		pfree(edata->hint);
	if (edata->context)
		pfree(edata->context);
	if (edata->backtrace)
		pfree(edata->backtrace);
	if (edata->schema_name)
		pfree(edata->schema_name);
	if (edata->table_name)
		pfree(edata->table_name);
	if (edata->column_name)
		pfree(edata->column_name);
	if (edata->datatype_name)
		pfree(edata->datatype_name);
	if (edata->constraint_name)
		pfree(edata->constraint_name);
	if (edata->internalquery)
		pfree(edata->internalquery);
	if (edata->yb_owns_file_and_func)
	{
		if (edata->filename)
			pfree((char*) edata->filename);
		if (edata->funcname)
			pfree((char*) edata->funcname);
	}
	pfree(edata);
}

/*
 * FlushErrorState --- flush the error state after error recovery
 *
 * This should be called by an error handler after it's done processing
 * the error; or as soon as it's done CopyErrorData, if it intends to
 * do stuff that is likely to provoke another error.  You are not "out" of
 * the error subsystem until you have done this.
 */
void
FlushErrorState(void)
{
	/*
	 * Teoretically if error is raised and caught during construction of
	 * another message should leave something in the stack to continue the
	 * construction. However, it seems like expectation is that FlushErrorState
	 * should empty out everything, so in multi-thread mode too, remove and
	 * destroy the status, if set.
	 */
	if (IsMultiThreadedMode())
	{
		YbgStatusDestroy(YBCPgSetThreadLocalErrStatus(NULL));
		return;
	}

	/*
	 * Reset stack to empty.  The only case where it would be more than one
	 * deep is if we serviced an error that interrupted construction of
	 * another message.  We assume control escaped out of that message
	 * construction and won't ever go back.
	 */
	errordata_stack_depth = -1;
	recursion_depth = 0;
	/* Delete all data in ErrorContext */
	MemoryContextResetAndDeleteChildren(ErrorContext);
}

/*
 * ThrowErrorData --- report an error described by an ErrorData structure
 *
 * This is somewhat like ReThrowError, but it allows elevels besides ERROR,
 * and the boolean flags such as output_to_server are computed via the
 * default rules rather than being copied from the given ErrorData.
 * This is primarily used to re-report errors originally reported by
 * background worker processes and then propagated (with or without
 * modification) to the backend responsible for them.
 */
void
ThrowErrorData(ErrorData *edata)
{
	if (IsMultiThreadedMode())
	{
		ybg_status_from_edata(edata);
		/* If error level is lower than ERROR ybg_status_from_edata returns */
		return;
	}
	ErrorData  *newedata;
	MemoryContext oldcontext;

	if (!errstart(edata->elevel, edata->domain))
		return;					/* error is not to be reported at all */

	newedata = &errordata[errordata_stack_depth];
	recursion_depth++;
	oldcontext = MemoryContextSwitchTo(newedata->assoc_context);

	/* Copy the supplied fields to the error stack entry. */
	if (edata->sqlerrcode != 0)
		newedata->sqlerrcode = edata->sqlerrcode;
	if (edata->message)
		newedata->message = pstrdup(edata->message);
	if (edata->detail)
		newedata->detail = pstrdup(edata->detail);
	if (edata->detail_log)
		newedata->detail_log = pstrdup(edata->detail_log);
	if (edata->hint)
		newedata->hint = pstrdup(edata->hint);
	if (edata->context)
		newedata->context = pstrdup(edata->context);
	if (edata->backtrace)
		newedata->backtrace = pstrdup(edata->backtrace);
	/* assume message_id is not available */
	if (edata->schema_name)
		newedata->schema_name = pstrdup(edata->schema_name);
	if (edata->table_name)
		newedata->table_name = pstrdup(edata->table_name);
	if (edata->column_name)
		newedata->column_name = pstrdup(edata->column_name);
	if (edata->datatype_name)
		newedata->datatype_name = pstrdup(edata->datatype_name);
	if (edata->constraint_name)
		newedata->constraint_name = pstrdup(edata->constraint_name);
	newedata->cursorpos = edata->cursorpos;
	newedata->internalpos = edata->internalpos;
	if (edata->internalquery)
		newedata->internalquery = pstrdup(edata->internalquery);

	MemoryContextSwitchTo(oldcontext);
	recursion_depth--;

	/* Process the error. */
	errfinish(edata->filename, edata->lineno, edata->funcname);
}

/*
 * ReThrowError --- re-throw a previously copied error
 *
 * A handler can do CopyErrorData/FlushErrorState to get out of the error
 * subsystem, then do some processing, and finally ReThrowError to re-throw
 * the original error.  This is slower than just PG_RE_THROW() but should
 * be used if the "some processing" is likely to incur another error.
 */
void
ReThrowError(ErrorData *edata)
{
	ErrorData  *newedata;

	Assert(edata->elevel == ERROR);

	if (IsMultiThreadedMode())
	{
		/* This call should not return because elevel is ERROR */
		ybg_status_from_edata(edata);
		YBCLogImpl(3 /* severity (3=FATAL) */ , edata->filename, edata->lineno,
				   YBShouldLogStackTraceOnError(),
				   "Unexpected return from ybg_status_from_edata()");
		pg_unreachable();
	}

	/* Push the data back into the error context */
	recursion_depth++;
	MemoryContextSwitchTo(ErrorContext);

	if (++errordata_stack_depth >= ERRORDATA_STACK_SIZE)
	{
		/*
		 * Wups, stack not big enough.  We treat this as a PANIC condition
		 * because it suggests an infinite loop of errors during error
		 * recovery.
		 */
		errordata_stack_depth = -1; /* make room on stack */
		ereport(PANIC, (errmsg_internal("ERRORDATA_STACK_SIZE exceeded")));
	}

	newedata = &errordata[errordata_stack_depth];
	memcpy(newedata, edata, sizeof(ErrorData));

	/* Make copies of separately-allocated fields */
	if (newedata->message)
		newedata->message = pstrdup(newedata->message);
	if (newedata->detail)
		newedata->detail = pstrdup(newedata->detail);
	if (newedata->detail_log)
		newedata->detail_log = pstrdup(newedata->detail_log);
	if (newedata->hint)
		newedata->hint = pstrdup(newedata->hint);
	if (newedata->context)
		newedata->context = pstrdup(newedata->context);
	if (newedata->backtrace)
		newedata->backtrace = pstrdup(newedata->backtrace);
	if (newedata->schema_name)
		newedata->schema_name = pstrdup(newedata->schema_name);
	if (newedata->table_name)
		newedata->table_name = pstrdup(newedata->table_name);
	if (newedata->column_name)
		newedata->column_name = pstrdup(newedata->column_name);
	if (newedata->datatype_name)
		newedata->datatype_name = pstrdup(newedata->datatype_name);
	if (newedata->constraint_name)
		newedata->constraint_name = pstrdup(newedata->constraint_name);
	if (newedata->internalquery)
		newedata->internalquery = pstrdup(newedata->internalquery);

	/* Reset the assoc_context to be ErrorContext */
	newedata->assoc_context = ErrorContext;

	recursion_depth--;
	PG_RE_THROW();
}

/*
 * pg_re_throw --- out-of-line implementation of PG_RE_THROW() macro
 */
void
pg_re_throw(void)
{
	sigjmp_buf *exception_stack = yb_get_exception_stack();
	/* If possible, throw the error to the next outer setjmp handler */
	if (exception_stack != NULL)
		siglongjmp(*exception_stack, 1);
	else
	{
		/*
		 * If we get here, elog(ERROR) was thrown inside a PG_TRY block, which
		 * we have now exited only to discover that there is no outer setjmp
		 * handler to pass the error to.  Had the error been thrown outside
		 * the block to begin with, we'd have promoted the error to FATAL, so
		 * the correct behavior is to make it FATAL now; that is, emit it and
		 * then call proc_exit.
		 */
		if (IsMultiThreadedMode())
		{
			/*
			 * In YbGate environment that basically means that the
			 * PG_SETUP_ERROR_REPORTING() has not been invoked.
			 * Check YbgStatus to see if there is filename/linenumber
			 * information to include into the fatal error report, as it would
			 * point to the offending ereport/elog.
			 */
			YbgStatus status = YBCPgGetThreadLocalErrStatus();
			const char *filename = NULL;
			int lineno = 0;
			if (status)
			{
				filename = YbgStatusGetFilename(status);
				lineno = YbgStatusGetLineNumber(status);
			}
			YBCLogImpl(3 /* severity (3=FATAL) */ , filename, lineno,
					   YBShouldLogStackTraceOnError(),
					   "PG error reporting has not been set up");
			pg_unreachable();
		}

		ErrorData  *edata = &errordata[errordata_stack_depth];

		Assert(errordata_stack_depth >= 0);
		Assert(edata->elevel == ERROR);
		edata->elevel = FATAL;

		/*
		 * At least in principle, the increase in severity could have changed
		 * where-to-output decisions, so recalculate.
		 */
		edata->output_to_server = should_output_to_server(FATAL);
		edata->output_to_client = should_output_to_client(FATAL);

		/*
		 * We can use errfinish() for the rest, but we don't want it to call
		 * any error context routines a second time.  Since we know we are
		 * about to exit, it should be OK to just clear the context stack.
		 */
		error_context_stack = NULL;

		errfinish(edata->filename, edata->lineno, edata->funcname);
	}

	/* Doesn't return ... */
	ExceptionalCondition("pg_re_throw tried to return", "FailedAssertion",
						 __FILE__, __LINE__);
}


/*
 * GetErrorContextStack - Return the context stack, for display/diags
 *
 * Returns a pstrdup'd string in the caller's context which includes the PG
 * error call stack.  It is the caller's responsibility to ensure this string
 * is pfree'd (or its context cleaned up) when done.
 *
 * This information is collected by traversing the error contexts and calling
 * each context's callback function, each of which is expected to call
 * errcontext() to return a string which can be presented to the user.
 */
char *
GetErrorContextStack(void)
{
	ErrorData  *edata;
	ErrorContextCallback *econtext;

	/*
	 * Okay, crank up a stack entry to store the info in.
	 */
	recursion_depth++;

	if (++errordata_stack_depth >= ERRORDATA_STACK_SIZE)
	{
		/*
		 * Wups, stack not big enough.  We treat this as a PANIC condition
		 * because it suggests an infinite loop of errors during error
		 * recovery.
		 */
		errordata_stack_depth = -1; /* make room on stack */
		ereport(PANIC, (errmsg_internal("ERRORDATA_STACK_SIZE exceeded")));
	}

	/*
	 * Things look good so far, so initialize our error frame
	 */
	edata = &errordata[errordata_stack_depth];
	MemSet(edata, 0, sizeof(ErrorData));

	/*
	 * Set up assoc_context to be the caller's context, so any allocations
	 * done (which will include edata->context) will use their context.
	 */
	edata->assoc_context = GetCurrentMemoryContext();

	/*
	 * Call any context callback functions to collect the context information
	 * into edata->context.
	 *
	 * Errors occurring in callback functions should go through the regular
	 * error handling code which should handle any recursive errors, though we
	 * double-check above, just in case.
	 */
	for (econtext = error_context_stack;
		 econtext != NULL;
		 econtext = econtext->previous)
		econtext->callback(econtext->arg);

	/*
	 * Clean ourselves off the stack, any allocations done should have been
	 * using edata->assoc_context, which we set up earlier to be the caller's
	 * context, so we're free to just remove our entry off the stack and
	 * decrement recursion depth and exit.
	 */
	errordata_stack_depth--;
	recursion_depth--;

	/*
	 * Return a pointer to the string the caller asked for, which should have
	 * been allocated in their context.
	 */
	return edata->context;
}


/*
 * Initialization of error output file
 */
void
DebugFileOpen(void)
{
	int			fd,
				istty;

	if (OutputFileName[0])
	{
		/*
		 * A debug-output file name was given.
		 *
		 * Make sure we can write the file, and find out if it's a tty.
		 */
		if ((fd = open(OutputFileName, O_CREAT | O_APPEND | O_WRONLY,
					   0666)) < 0)
			ereport(FATAL,
					(errcode_for_file_access(),
					 errmsg("could not open file \"%s\": %m", OutputFileName)));
		istty = isatty(fd);
		close(fd);

		/*
		 * Redirect our stderr to the debug output file.
		 */
		if (!freopen(OutputFileName, "a", stderr))
			ereport(FATAL,
					(errcode_for_file_access(),
					 errmsg("could not reopen file \"%s\" as stderr: %m",
							OutputFileName)));

		/*
		 * If the file is a tty and we're running under the postmaster, try to
		 * send stdout there as well (if it isn't a tty then stderr will block
		 * out stdout, so we may as well let stdout go wherever it was going
		 * before).
		 */
		if (istty && IsUnderPostmaster)
			if (!freopen(OutputFileName, "a", stdout))
				ereport(FATAL,
						(errcode_for_file_access(),
						 errmsg("could not reopen file \"%s\" as stdout: %m",
								OutputFileName)));
	}
}


#ifdef HAVE_SYSLOG

/*
 * Set or update the parameters for syslog logging
 */
void
set_syslog_parameters(const char *ident, int facility)
{
	/*
	 * guc.c is likely to call us repeatedly with same parameters, so don't
	 * thrash the syslog connection unnecessarily.  Also, we do not re-open
	 * the connection until needed, since this routine will get called whether
	 * or not Log_destination actually mentions syslog.
	 *
	 * Note that we make our own copy of the ident string rather than relying
	 * on guc.c's.  This may be overly paranoid, but it ensures that we cannot
	 * accidentally free a string that syslog is still using.
	 */
	if (syslog_ident == NULL || strcmp(syslog_ident, ident) != 0 ||
		syslog_facility != facility)
	{
		if (openlog_done)
		{
			closelog();
			openlog_done = false;
		}
		if (syslog_ident)
			free(syslog_ident);
		syslog_ident = strdup(ident);
		/* if the strdup fails, we will cope in write_syslog() */
		syslog_facility = facility;
	}
}


/*
 * Write a message line to syslog
 */
static void
write_syslog(int level, const char *line)
{
	static unsigned long seq = 0;

	int			len;
	const char *nlpos;

	/* Open syslog connection if not done yet */
	if (!openlog_done)
	{
		openlog(syslog_ident ? syslog_ident : "postgres",
				LOG_PID | LOG_NDELAY | LOG_NOWAIT,
				syslog_facility);
		openlog_done = true;
	}

	/*
	 * We add a sequence number to each log message to suppress "same"
	 * messages.
	 */
	seq++;

	/*
	 * Our problem here is that many syslog implementations don't handle long
	 * messages in an acceptable manner. While this function doesn't help that
	 * fact, it does work around by splitting up messages into smaller pieces.
	 *
	 * We divide into multiple syslog() calls if message is too long or if the
	 * message contains embedded newline(s).
	 */
	len = strlen(line);
	nlpos = strchr(line, '\n');
	if (syslog_split_messages && (len > PG_SYSLOG_LIMIT || nlpos != NULL))
	{
		int			chunk_nr = 0;

		while (len > 0)
		{
			char		buf[PG_SYSLOG_LIMIT + 1];
			int			buflen;
			int			i;

			/* if we start at a newline, move ahead one char */
			if (line[0] == '\n')
			{
				line++;
				len--;
				/* we need to recompute the next newline's position, too */
				nlpos = strchr(line, '\n');
				continue;
			}

			/* copy one line, or as much as will fit, to buf */
			if (nlpos != NULL)
				buflen = nlpos - line;
			else
				buflen = len;
			buflen = Min(buflen, PG_SYSLOG_LIMIT);
			memcpy(buf, line, buflen);
			buf[buflen] = '\0';

			/* trim to multibyte letter boundary */
			buflen = pg_mbcliplen(buf, buflen, buflen);
			if (buflen <= 0)
				return;
			buf[buflen] = '\0';

			/* already word boundary? */
			if (line[buflen] != '\0' &&
				!isspace((unsigned char) line[buflen]))
			{
				/* try to divide at word boundary */
				i = buflen - 1;
				while (i > 0 && !isspace((unsigned char) buf[i]))
					i--;

				if (i > 0)		/* else couldn't divide word boundary */
				{
					buflen = i;
					buf[i] = '\0';
				}
			}

			chunk_nr++;

			if (syslog_sequence_numbers)
				syslog(level, "[%lu-%d] %s", seq, chunk_nr, buf);
			else
				syslog(level, "[%d] %s", chunk_nr, buf);

			line += buflen;
			len -= buflen;
		}
	}
	else
	{
		/* message short enough */
		if (syslog_sequence_numbers)
			syslog(level, "[%lu] %s", seq, line);
		else
			syslog(level, "%s", line);
	}
}
#endif							/* HAVE_SYSLOG */

#ifdef WIN32
/*
 * Get the PostgreSQL equivalent of the Windows ANSI code page.  "ANSI" system
 * interfaces (e.g. CreateFileA()) expect string arguments in this encoding.
 * Every process in a given system will find the same value at all times.
 */
static int
GetACPEncoding(void)
{
	static int	encoding = -2;

	if (encoding == -2)
		encoding = pg_codepage_to_encoding(GetACP());

	return encoding;
}

/*
 * Write a message line to the windows event log
 */
static void
write_eventlog(int level, const char *line, int len)
{
	WCHAR	   *utf16;
	int			eventlevel = EVENTLOG_ERROR_TYPE;
	static HANDLE evtHandle = INVALID_HANDLE_VALUE;

	if (evtHandle == INVALID_HANDLE_VALUE)
	{
		evtHandle = RegisterEventSource(NULL,
										event_source ? event_source : DEFAULT_EVENT_SOURCE);
		if (evtHandle == NULL)
		{
			evtHandle = INVALID_HANDLE_VALUE;
			return;
		}
	}

	switch (level)
	{
		case DEBUG5:
		case DEBUG4:
		case DEBUG3:
		case DEBUG2:
		case DEBUG1:
		case LOG:
		case LOG_SERVER_ONLY:
		case INFO:
		case NOTICE:
			eventlevel = EVENTLOG_INFORMATION_TYPE;
			break;
		case WARNING:
		case WARNING_CLIENT_ONLY:
			eventlevel = EVENTLOG_WARNING_TYPE;
			break;
		case ERROR:
		case FATAL:
		case PANIC:
		default:
			eventlevel = EVENTLOG_ERROR_TYPE;
			break;
	}

	/*
	 * If message character encoding matches the encoding expected by
	 * ReportEventA(), call it to avoid the hazards of conversion.  Otherwise,
	 * try to convert the message to UTF16 and write it with ReportEventW().
	 * Fall back on ReportEventA() if conversion failed.
	 *
	 * Since we palloc the structure required for conversion, also fall
	 * through to writing unconverted if we have not yet set up
	 * GetCurrentMemoryContext().
	 *
	 * Also verify that we are not on our way into error recursion trouble due
	 * to error messages thrown deep inside pgwin32_message_to_UTF16().
	 */
	if (!in_error_recursion_trouble() &&
		GetCurrentMemoryContext() != NULL &&
		GetMessageEncoding() != GetACPEncoding())
	{
		utf16 = pgwin32_message_to_UTF16(line, len, NULL);
		if (utf16)
		{
			ReportEventW(evtHandle,
						 eventlevel,
						 0,
						 0,		/* All events are Id 0 */
						 NULL,
						 1,
						 0,
						 (LPCWSTR *) &utf16,
						 NULL);
			/* XXX Try ReportEventA() when ReportEventW() fails? */

			pfree(utf16);
			return;
		}
	}
	ReportEventA(evtHandle,
				 eventlevel,
				 0,
				 0,				/* All events are Id 0 */
				 NULL,
				 1,
				 0,
				 &line,
				 NULL);
}
#endif							/* WIN32 */

static void
write_console(const char *line, int len)
{
	int			rc;

#ifdef WIN32

	/*
	 * Try to convert the message to UTF16 and write it with WriteConsoleW().
	 * Fall back on write() if anything fails.
	 *
	 * In contrast to write_eventlog(), don't skip straight to write() based
	 * on the applicable encodings.  Unlike WriteConsoleW(), write() depends
	 * on the suitability of the console output code page.  Since we put
	 * stderr into binary mode in SubPostmasterMain(), write() skips the
	 * necessary translation anyway.
	 *
	 * WriteConsoleW() will fail if stderr is redirected, so just fall through
	 * to writing unconverted to the logfile in this case.
	 *
	 * Since we palloc the structure required for conversion, also fall
	 * through to writing unconverted if we have not yet set up
	 * GetCurrentMemoryContext().
	 */
	if (!in_error_recursion_trouble() &&
		!redirection_done &&
		GetCurrentMemoryContext() != NULL)
	{
		WCHAR	   *utf16;
		int			utf16len;

		utf16 = pgwin32_message_to_UTF16(line, len, &utf16len);
		if (utf16 != NULL)
		{
			HANDLE		stdHandle;
			DWORD		written;

			stdHandle = GetStdHandle(STD_ERROR_HANDLE);
			if (WriteConsoleW(stdHandle, utf16, utf16len, &written, NULL))
			{
				pfree(utf16);
				return;
			}

			/*
			 * In case WriteConsoleW() failed, fall back to writing the
			 * message unconverted.
			 */
			pfree(utf16);
		}
	}
#else

	/*
	 * Conversion on non-win32 platforms is not implemented yet. It requires
	 * non-throw version of pg_do_encoding_conversion(), that converts
	 * unconvertible characters to '?' without errors.
	 *
	 * XXX: We have a no-throw version now. It doesn't convert to '?' though.
	 */
#endif

	/*
	 * We ignore any error from write() here.  We have no useful way to report
	 * it ... certainly whining on stderr isn't likely to be productive.
	 */
	rc = write(fileno(stderr), line, len);
	(void) rc;
}

/*
 * get_formatted_log_time -- compute and get the log timestamp.
 *
 * The timestamp is computed if not set yet, so as it is kept consistent
 * among all the log destinations that require it to be consistent.  Note
 * that the computed timestamp is returned in a static buffer, not
 * palloc()'d.
 */
char *
get_formatted_log_time(void)
{
	pg_time_t	stamp_time;
	char		msbuf[13];

	/* leave if already computed */
	if (formatted_log_time[0] != '\0')
		return formatted_log_time;

	if (!saved_timeval_set)
	{
		gettimeofday(&saved_timeval, NULL);
		saved_timeval_set = true;
	}

	stamp_time = (pg_time_t) saved_timeval.tv_sec;

	/*
	 * Note: we expect that guc.c will ensure that log_timezone is set up (at
	 * least with a minimal GMT value) before Log_line_prefix can become
	 * nonempty or CSV mode can be selected.
	 */
	pg_strftime(formatted_log_time, FORMATTED_TS_LEN,
	/* leave room for milliseconds... */
				"%Y-%m-%d %H:%M:%S     %Z",
				pg_localtime(&stamp_time, log_timezone));

	/* 'paste' milliseconds into place... */
	sprintf(msbuf, ".%03d", (int) (saved_timeval.tv_usec / 1000));
	memcpy(formatted_log_time + 19, msbuf, 4);

	return formatted_log_time;
}

/*
 * reset_formatted_start_time -- reset the start timestamp
 */
void
reset_formatted_start_time(void)
{
	formatted_start_time[0] = '\0';
}

/*
 * get_formatted_start_time -- compute and get the start timestamp.
 *
 * The timestamp is computed if not set yet.  Note that the computed
 * timestamp is returned in a static buffer, not palloc()'d.
 */
char *
get_formatted_start_time(void)
{
	pg_time_t	stamp_time = (pg_time_t) MyStartTime;

	/* leave if already computed */
	if (formatted_start_time[0] != '\0')
		return formatted_start_time;

	/*
	 * Note: we expect that guc.c will ensure that log_timezone is set up (at
	 * least with a minimal GMT value) before Log_line_prefix can become
	 * nonempty or CSV mode can be selected.
	 */
	pg_strftime(formatted_start_time, FORMATTED_TS_LEN,
				"%Y-%m-%d %H:%M:%S %Z",
				pg_localtime(&stamp_time, log_timezone));

	return formatted_start_time;
}

/*
 * check_log_of_query -- check if a query can be logged
 */
bool
check_log_of_query(ErrorData *edata)
{
	/* log required? */
	if (!is_log_level_output(edata->elevel, log_min_error_statement))
		return false;

	/* query log wanted? */
	if (edata->hide_stmt)
		return false;

	/* query string available? */
	if (debug_query_string == NULL)
		return false;

	return true;
}

/*
 * get_backend_type_for_log -- backend type for log entries
 *
 * Returns a pointer to a static buffer, not palloc()'d.
 */
const char *
get_backend_type_for_log(void)
{
	const char *backend_type_str;

	if (MyProcPid == PostmasterPid)
		backend_type_str = "postmaster";
	else if (MyBackendType == B_BG_WORKER)
		backend_type_str = MyBgworkerEntry->bgw_type;
	else
		backend_type_str = GetBackendTypeDesc(MyBackendType);

	return backend_type_str;
}

/*
 * process_log_prefix_padding --- helper function for processing the format
 * string in log_line_prefix
 *
 * Note: This function returns NULL if it finds something which
 * it deems invalid in the format string.
 */
static const char *
process_log_prefix_padding(const char *p, int *ppadding)
{
	int			paddingsign = 1;
	int			padding = 0;

	if (*p == '-')
	{
		p++;

		if (*p == '\0')			/* Did the buf end in %- ? */
			return NULL;
		paddingsign = -1;
	}

	/* generate an int version of the numerical string */
	while (*p >= '0' && *p <= '9')
		padding = padding * 10 + (*p++ - '0');

	/* format is invalid if it ends with the padding number */
	if (*p == '\0')
		return NULL;

	padding *= paddingsign;
	*ppadding = padding;
	return p;
}

/*
 * Format tag info for log lines; append to the provided buffer.
 */
static void
log_line_prefix(StringInfo buf, ErrorData *edata)
{
	/* static counter for line numbers */
	static long log_line_number = 0;

	/* has counter been reset in current process? */
	static int	log_my_pid = 0;
	int			padding;
	const char *p;

	/*
	 * This is one of the few places where we'd rather not inherit a static
	 * variable's value from the postmaster.  But since we will, reset it when
	 * MyProcPid changes. MyStartTime also changes when MyProcPid does, so
	 * reset the formatted start timestamp too.
	 */
	if (log_my_pid != MyProcPid)
	{
		log_line_number = 0;
		log_my_pid = MyProcPid;
		reset_formatted_start_time();
	}
	log_line_number++;

	if (Log_line_prefix == NULL)
		return;					/* in case guc hasn't run yet */

	for (p = Log_line_prefix; *p != '\0'; p++)
	{
		if (*p != '%')
		{
			/* literal char, just copy */
			appendStringInfoChar(buf, *p);
			continue;
		}

		/* must be a '%', so skip to the next char */
		p++;
		if (*p == '\0')
			break;				/* format error - ignore it */
		else if (*p == '%')
		{
			/* string contains %% */
			appendStringInfoChar(buf, '%');
			continue;
		}


		/*
		 * Process any formatting which may exist after the '%'.  Note that
		 * process_log_prefix_padding moves p past the padding number if it
		 * exists.
		 *
		 * Note: Since only '-', '0' to '9' are valid formatting characters we
		 * can do a quick check here to pre-check for formatting. If the char
		 * is not formatting then we can skip a useless function call.
		 *
		 * Further note: At least on some platforms, passing %*s rather than
		 * %s to appendStringInfo() is substantially slower, so many of the
		 * cases below avoid doing that unless non-zero padding is in fact
		 * specified.
		 */
		if (*p > '9')
			padding = 0;
		else if ((p = process_log_prefix_padding(p, &padding)) == NULL)
			break;

		/* process the option */
		switch (*p)
		{
			case 'a':
				if (MyProcPort)
				{
					const char *appname = application_name;

					if (appname == NULL || *appname == '\0')
						appname = _("[unknown]");
					if (padding != 0)
						appendStringInfo(buf, "%*s", padding, appname);
					else
						appendStringInfoString(buf, appname);
				}
				else if (padding != 0)
					appendStringInfoSpaces(buf,
										   padding > 0 ? padding : -padding);

				break;
			case 'b':
				{
					const char *backend_type_str = get_backend_type_for_log();

					if (padding != 0)
						appendStringInfo(buf, "%*s", padding, backend_type_str);
					else
						appendStringInfoString(buf, backend_type_str);
					break;
				}
			case 'u':
				if (MyProcPort)
				{
					const char *username = MyProcPort->user_name;

					if (username == NULL || *username == '\0')
						username = _("[unknown]");
					if (padding != 0)
						appendStringInfo(buf, "%*s", padding, username);
					else
						appendStringInfoString(buf, username);
				}
				else if (padding != 0)
					appendStringInfoSpaces(buf,
										   padding > 0 ? padding : -padding);
				break;
			case 'd':
				if (MyProcPort)
				{
					const char *dbname = MyProcPort->database_name;

					if (dbname == NULL || *dbname == '\0')
						dbname = _("[unknown]");
					if (padding != 0)
						appendStringInfo(buf, "%*s", padding, dbname);
					else
						appendStringInfoString(buf, dbname);
				}
				else if (padding != 0)
					appendStringInfoSpaces(buf,
										   padding > 0 ? padding : -padding);
				break;
			case 'c':
				if (padding != 0)
				{
					char		strfbuf[128];

					snprintf(strfbuf, sizeof(strfbuf) - 1, "%lx.%x",
							 (long) (MyStartTime), MyProcPid);
					appendStringInfo(buf, "%*s", padding, strfbuf);
				}
				else
					appendStringInfo(buf, "%lx.%x", (long) (MyStartTime), MyProcPid);
				break;
			case 'p':
				if (padding != 0)
					appendStringInfo(buf, "%*d", padding, MyProcPid);
				else
					appendStringInfo(buf, "%d", MyProcPid);
				break;

			case 'P':
				if (MyProc)
				{
					PGPROC	   *leader = MyProc->lockGroupLeader;

					/*
					 * Show the leader only for active parallel workers. This
					 * leaves out the leader of a parallel group.
					 */
					if (leader == NULL || leader->pid == MyProcPid)
						appendStringInfoSpaces(buf,
											   padding > 0 ? padding : -padding);
					else if (padding != 0)
						appendStringInfo(buf, "%*d", padding, leader->pid);
					else
						appendStringInfo(buf, "%d", leader->pid);
				}
				else if (padding != 0)
					appendStringInfoSpaces(buf,
										   padding > 0 ? padding : -padding);
				break;

			case 'l':
				if (padding != 0)
					appendStringInfo(buf, "%*ld", padding, log_line_number);
				else
					appendStringInfo(buf, "%ld", log_line_number);
				break;
			case 'm':
				/* force a log timestamp reset */
				formatted_log_time[0] = '\0';
				(void) get_formatted_log_time();

				if (padding != 0)
					appendStringInfo(buf, "%*s", padding, formatted_log_time);
				else
					appendStringInfoString(buf, formatted_log_time);
				break;
			case 't':
				{
					pg_time_t	stamp_time = (pg_time_t) time(NULL);
					char		strfbuf[128];

					pg_strftime(strfbuf, sizeof(strfbuf),
								"%Y-%m-%d %H:%M:%S %Z",
								pg_localtime(&stamp_time, log_timezone));
					if (padding != 0)
						appendStringInfo(buf, "%*s", padding, strfbuf);
					else
						appendStringInfoString(buf, strfbuf);
				}
				break;
			case 'n':
				{
					char		strfbuf[128];

					if (!saved_timeval_set)
					{
						gettimeofday(&saved_timeval, NULL);
						saved_timeval_set = true;
					}

					snprintf(strfbuf, sizeof(strfbuf), "%ld.%03d",
							 (long) saved_timeval.tv_sec,
							 (int) (saved_timeval.tv_usec / 1000));

					if (padding != 0)
						appendStringInfo(buf, "%*s", padding, strfbuf);
					else
						appendStringInfoString(buf, strfbuf);
				}
				break;
			case 's':
				{
					char	   *start_time = get_formatted_start_time();

					if (padding != 0)
						appendStringInfo(buf, "%*s", padding, start_time);
					else
						appendStringInfoString(buf, start_time);
				}
				break;
			case 'i':
				if (MyProcPort)
				{
					const char *psdisp;
					int			displen;

					psdisp = get_ps_display(&displen);
					if (padding != 0)
						appendStringInfo(buf, "%*s", padding, psdisp);
					else
						appendBinaryStringInfo(buf, psdisp, displen);
				}
				else if (padding != 0)
					appendStringInfoSpaces(buf,
										   padding > 0 ? padding : -padding);
				break;
			case 'r':
				if (MyProcPort && MyProcPort->remote_host)
				{
					if (padding != 0)
					{
						if (MyProcPort->remote_port && MyProcPort->remote_port[0] != '\0')
						{
							/*
							 * This option is slightly special as the port
							 * number may be appended onto the end. Here we
							 * need to build 1 string which contains the
							 * remote_host and optionally the remote_port (if
							 * set) so we can properly align the string.
							 */

							char	   *hostport;

							hostport = psprintf("%s(%s)", MyProcPort->remote_host, MyProcPort->remote_port);
							appendStringInfo(buf, "%*s", padding, hostport);
							pfree(hostport);
						}
						else
							appendStringInfo(buf, "%*s", padding, MyProcPort->remote_host);
					}
					else
					{
						/* padding is 0, so we don't need a temp buffer */
						appendStringInfoString(buf, MyProcPort->remote_host);
						if (MyProcPort->remote_port &&
							MyProcPort->remote_port[0] != '\0')
							appendStringInfo(buf, "(%s)",
											 MyProcPort->remote_port);
					}
				}
				else if (padding != 0)
					appendStringInfoSpaces(buf,
										   padding > 0 ? padding : -padding);
				break;
			case 'h':
				if (MyProcPort && MyProcPort->remote_host)
				{
					if (padding != 0)
						appendStringInfo(buf, "%*s", padding, MyProcPort->remote_host);
					else
						appendStringInfoString(buf, MyProcPort->remote_host);
				}
				else if (padding != 0)
					appendStringInfoSpaces(buf,
										   padding > 0 ? padding : -padding);
				break;
			case 'q':
				/* in postmaster and friends, stop if %q is seen */
				/* in a backend, just ignore */
				if (MyProcPort == NULL)
					return;
				break;
			case 'v':
				/* keep VXID format in sync with lockfuncs.c */
				if (MyProc != NULL && MyProc->backendId != InvalidBackendId)
				{
					if (padding != 0)
					{
						char		strfbuf[128];

						snprintf(strfbuf, sizeof(strfbuf) - 1, "%d/%u",
								 MyProc->backendId, MyProc->lxid);
						appendStringInfo(buf, "%*s", padding, strfbuf);
					}
					else
						appendStringInfo(buf, "%d/%u", MyProc->backendId, MyProc->lxid);
				}
				else if (padding != 0)
					appendStringInfoSpaces(buf,
										   padding > 0 ? padding : -padding);
				break;
			case 'x':
				if (padding != 0)
					appendStringInfo(buf, "%*u", padding, GetTopTransactionIdIfAny());
				else
					appendStringInfo(buf, "%u", GetTopTransactionIdIfAny());
				break;
			case 'e':
				if (padding != 0)
					appendStringInfo(buf, "%*s", padding, unpack_sql_state(edata->sqlerrcode));
				else
					appendStringInfoString(buf, unpack_sql_state(edata->sqlerrcode));
				break;
			case 'C': {
				const char* cloud = YBGetCurrentCloud();
				if (cloud) {
					if (padding != 0)
						appendStringInfo(buf, "%*s", padding, cloud);
					else
						appendStringInfoString(buf, cloud);
				}
				break;
			}
			case 'R': {
				const char* region = YBGetCurrentRegion();
				if (region) {
					if (padding != 0)
						appendStringInfo(buf, "%*s", padding, region);
					else
						appendStringInfoString(buf, region);
				}
				break;
			}
			case 'Z': {
				const char* zone = YBGetCurrentZone();
				if (zone) {
					if (padding != 0)
						appendStringInfo(buf, "%*s", padding, zone);
					else
						appendStringInfoString(buf, zone);
				}
				break;
			}
			case 'U': {
				const char* uuid = YBGetCurrentUUID();
				if (uuid) {
					if (padding != 0)
						appendStringInfo(buf, "%*s", padding, uuid);
					else
						appendStringInfoString(buf, uuid);
				}
				break;
			}
			case 'N': {
				const char* node = YBGetCurrentMetricNodeName();
				if (node) {
					if (padding != 0)
						appendStringInfo(buf, "%*s", padding, node);
					else
						appendStringInfoString(buf, node);
				}
				break;
			}
			case 'H': {
				char name[MAX_HOSTNAME_LENGTH];
				const int ret = gethostname(name, sizeof(name));
				if (!ret) {
					if (padding != 0)
						appendStringInfo(buf, "%*s", padding, name);
					else
						appendStringInfoString(buf, name);
				}
				break;
			}
			case 'Q':
				if (padding != 0)
					appendStringInfo(buf, "%*lld", padding,
									 (long long) pgstat_get_my_query_id());
				else
					appendStringInfo(buf, "%lld",
									 (long long) pgstat_get_my_query_id());
				break;
			default:
				/* format error - ignore it */
				break;
		}
	}
}

/*
 * Unpack MAKE_SQLSTATE code. Note that this returns a pointer to a
 * static buffer.
 */
char *
unpack_sql_state(int sql_state)
{
	static char buf[12];
	int			i;

	for (i = 0; i < 5; i++)
	{
		buf[i] = PGUNSIXBIT(sql_state);
		sql_state >>= 6;
	}

	buf[i] = '\0';
	return buf;
}


/*
 * Write error report to server's log
 */
static void
send_message_to_server_log(ErrorData *edata)
{
	StringInfoData buf;
	bool		fallback_to_stderr = false;

	initStringInfo(&buf);

	saved_timeval_set = false;
	formatted_log_time[0] = '\0';

	log_line_prefix(&buf, edata);
	appendStringInfo(&buf, "%s:  ", _(error_severity(edata->elevel)));

	if (Log_error_verbosity >= PGERROR_VERBOSE)
		appendStringInfo(&buf, "%s: ", unpack_sql_state(edata->sqlerrcode));

	if (edata->message)
		append_with_tabs(&buf, edata->message);
	else
		append_with_tabs(&buf, _("missing error text"));

	if (edata->cursorpos > 0)
		appendStringInfo(&buf, _(" at character %d"),
						 edata->cursorpos);
	else if (edata->internalpos > 0)
		appendStringInfo(&buf, _(" at character %d"),
						 edata->internalpos);

	appendStringInfoChar(&buf, '\n');

	if (Log_error_verbosity >= PGERROR_DEFAULT)
	{
		if (edata->detail_log)
		{
			log_line_prefix(&buf, edata);
			appendStringInfoString(&buf, _("DETAIL:  "));
			append_with_tabs(&buf, edata->detail_log);
			appendStringInfoChar(&buf, '\n');
		}
		else if (edata->detail)
		{
			log_line_prefix(&buf, edata);
			appendStringInfoString(&buf, _("DETAIL:  "));
			append_with_tabs(&buf, edata->detail);
			appendStringInfoChar(&buf, '\n');
		}
		if (edata->hint)
		{
			log_line_prefix(&buf, edata);
			appendStringInfoString(&buf, _("HINT:  "));
			append_with_tabs(&buf, edata->hint);
			appendStringInfoChar(&buf, '\n');
		}
		if (edata->internalquery)
		{
			log_line_prefix(&buf, edata);
			appendStringInfoString(&buf, _("QUERY:  "));
			append_with_tabs(&buf, edata->internalquery);
			appendStringInfoChar(&buf, '\n');
		}
		if (edata->context && !edata->hide_ctx)
		{
			log_line_prefix(&buf, edata);
			appendStringInfoString(&buf, _("CONTEXT:  "));
			append_with_tabs(&buf, edata->context);
			appendStringInfoChar(&buf, '\n');
		}
		if (Log_error_verbosity >= PGERROR_VERBOSE)
		{
			/* assume no newlines in funcname or filename... */
			if (edata->funcname && edata->filename)
			{
				log_line_prefix(&buf, edata);
				appendStringInfo(&buf, _("LOCATION:  %s, %s:%d\n"),
								 edata->funcname, edata->filename,
								 edata->lineno);
			}
			else if (edata->filename)
			{
				log_line_prefix(&buf, edata);
				appendStringInfo(&buf, _("LOCATION:  %s:%d\n"),
								 edata->filename, edata->lineno);
			}
		}
		if (edata->backtrace)
		{
			log_line_prefix(&buf, edata);
			appendStringInfoString(&buf, _("BACKTRACE:  "));
			append_with_tabs(&buf, edata->backtrace);
			appendStringInfoChar(&buf, '\n');
		}
	}

	/*
	 * If the user wants the query that generated this error logged, do it.
	 */
	if (check_log_of_query(edata))
	{
		log_line_prefix(&buf, edata);
		appendStringInfoString(&buf, _("STATEMENT:  "));
		append_with_tabs(&buf, debug_query_string);
		appendStringInfoChar(&buf, '\n');
	}

#ifdef HAVE_SYSLOG
	/* Write to syslog, if enabled */
	if (Log_destination & LOG_DESTINATION_SYSLOG)
	{
		int			syslog_level;

		switch (edata->elevel)
		{
			case DEBUG5:
			case DEBUG4:
			case DEBUG3:
			case DEBUG2:
			case DEBUG1:
				syslog_level = LOG_DEBUG;
				break;
			case LOG:
			case LOG_SERVER_ONLY:
			case INFO:
				syslog_level = LOG_INFO;
				break;
			case NOTICE:
			case WARNING:
			case WARNING_CLIENT_ONLY:
				syslog_level = LOG_NOTICE;
				break;
			case ERROR:
				syslog_level = LOG_WARNING;
				break;
			case FATAL:
				syslog_level = LOG_ERR;
				break;
			case PANIC:
			default:
				syslog_level = LOG_CRIT;
				break;
		}

		write_syslog(syslog_level, buf.data);
	}
#endif							/* HAVE_SYSLOG */

#ifdef WIN32
	/* Write to eventlog, if enabled */
	if (Log_destination & LOG_DESTINATION_EVENTLOG)
	{
		write_eventlog(edata->elevel, buf.data, buf.len);
	}
#endif							/* WIN32 */

	/* Write to csvlog, if enabled */
	if (Log_destination & LOG_DESTINATION_CSVLOG)
	{
		/*
		 * Send CSV data if it's safe to do so (syslogger doesn't need the
		 * pipe).  If this is not possible, fallback to an entry written to
		 * stderr.
		 */
		if (redirection_done || MyBackendType == B_LOGGER)
			write_csvlog(edata);
		else
			fallback_to_stderr = true;
	}

	/* Write to JSON log, if enabled */
	if (Log_destination & LOG_DESTINATION_JSONLOG)
	{
		/*
		 * Send JSON data if it's safe to do so (syslogger doesn't need the
		 * pipe).  If this is not possible, fallback to an entry written to
		 * stderr.
		 */
		if (redirection_done || MyBackendType == B_LOGGER)
		{
			write_jsonlog(edata);
		}
		else
			fallback_to_stderr = true;
	}

	/*
	 * Write to stderr, if enabled or if required because of a previous
	 * limitation.
	 */
	if ((Log_destination & LOG_DESTINATION_STDERR) ||
		whereToSendOutput == DestDebug ||
		fallback_to_stderr)
	{
		/*
		 * Use the chunking protocol if we know the syslogger should be
		 * catching stderr output, and we are not ourselves the syslogger.
		 * Otherwise, just do a vanilla write to stderr.
		 */
		if (redirection_done && MyBackendType != B_LOGGER)
			write_pipe_chunks(buf.data, buf.len, LOG_DESTINATION_STDERR);
#ifdef WIN32

		/*
		 * In a win32 service environment, there is no usable stderr. Capture
		 * anything going there and write it to the eventlog instead.
		 *
		 * If stderr redirection is active, it was OK to write to stderr above
		 * because that's really a pipe to the syslogger process.
		 */
		else if (pgwin32_is_service())
			write_eventlog(edata->elevel, buf.data, buf.len);
#endif
		else
			write_console(buf.data, buf.len);
	}

	/* If in the syslogger process, try to write messages direct to file */
	if (MyBackendType == B_LOGGER)
		write_syslogger_file(buf.data, buf.len, LOG_DESTINATION_STDERR);

	/* No more need of the message formatted for stderr */
	pfree(buf.data);
}

/*
 * Send data to the syslogger using the chunked protocol
 *
 * Note: when there are multiple backends writing into the syslogger pipe,
 * it's critical that each write go into the pipe indivisibly, and not
 * get interleaved with data from other processes.  Fortunately, the POSIX
 * spec requires that writes to pipes be atomic so long as they are not
 * more than PIPE_BUF bytes long.  So we divide long messages into chunks
 * that are no more than that length, and send one chunk per write() call.
 * The collector process knows how to reassemble the chunks.
 *
 * Because of the atomic write requirement, there are only two possible
 * results from write() here: -1 for failure, or the requested number of
 * bytes.  There is not really anything we can do about a failure; retry would
 * probably be an infinite loop, and we can't even report the error usefully.
 * (There is noplace else we could send it!)  So we might as well just ignore
 * the result from write().  However, on some platforms you get a compiler
 * warning from ignoring write()'s result, so do a little dance with casting
 * rc to void to shut up the compiler.
 */
void
write_pipe_chunks(char *data, int len, int dest)
{
	PipeProtoChunk p;
	int			fd = fileno(stderr);
	int			rc;

	Assert(len > 0);

	p.proto.nuls[0] = p.proto.nuls[1] = '\0';
	p.proto.pid = MyProcPid;
	p.proto.flags = 0;
	if (dest == LOG_DESTINATION_STDERR)
		p.proto.flags |= PIPE_PROTO_DEST_STDERR;
	else if (dest == LOG_DESTINATION_CSVLOG)
		p.proto.flags |= PIPE_PROTO_DEST_CSVLOG;
	else if (dest == LOG_DESTINATION_JSONLOG)
		p.proto.flags |= PIPE_PROTO_DEST_JSONLOG;

	/* write all but the last chunk */
	while (len > PIPE_MAX_PAYLOAD)
	{
		/* no need to set PIPE_PROTO_IS_LAST yet */
		p.proto.len = PIPE_MAX_PAYLOAD;
		memcpy(p.proto.data, data, PIPE_MAX_PAYLOAD);
		rc = write(fd, &p, PIPE_HEADER_SIZE + PIPE_MAX_PAYLOAD);
		(void) rc;
		data += PIPE_MAX_PAYLOAD;
		len -= PIPE_MAX_PAYLOAD;
	}

	/* write the last chunk */
	p.proto.flags |= PIPE_PROTO_IS_LAST;
	p.proto.len = len;
	memcpy(p.proto.data, data, len);
	rc = write(fd, &p, PIPE_HEADER_SIZE + len);
	(void) rc;
}


/*
 * Append a text string to the error report being built for the client.
 *
 * This is ordinarily identical to pq_sendstring(), but if we are in
 * error recursion trouble we skip encoding conversion, because of the
 * possibility that the problem is a failure in the encoding conversion
 * subsystem itself.  Code elsewhere should ensure that the passed-in
 * strings will be plain 7-bit ASCII, and thus not in need of conversion,
 * in such cases.  (In particular, we disable localization of error messages
 * to help ensure that's true.)
 */
static void
err_sendstring(StringInfo buf, const char *str)
{
	if (in_error_recursion_trouble())
		pq_send_ascii_string(buf, str);
	else
		pq_sendstring(buf, str);
}

/*
 * Write error report to client
 */
static void
send_message_to_frontend(ErrorData *edata)
{
	StringInfoData msgbuf;

	/*
	 * We no longer support pre-3.0 FE/BE protocol, except here.  If a client
	 * tries to connect using an older protocol version, it's nice to send the
	 * "protocol version not supported" error in a format the client
	 * understands.  If protocol hasn't been set yet, early in backend
	 * startup, assume modern protocol.
	 */
	if (PG_PROTOCOL_MAJOR(FrontendProtocol) >= 3 || FrontendProtocol == 0)
	{
		/* New style with separate fields */
		const char *sev;
		char		tbuf[12];

		/* 'N' (Notice) is for nonfatal conditions, 'E' is for errors */
		pq_beginmessage(&msgbuf, (edata->elevel < ERROR) ? 'N' : 'E');

		sev = error_severity(edata->elevel);
		pq_sendbyte(&msgbuf, PG_DIAG_SEVERITY);
		err_sendstring(&msgbuf, _(sev));
		pq_sendbyte(&msgbuf, PG_DIAG_SEVERITY_NONLOCALIZED);
		err_sendstring(&msgbuf, sev);

		pq_sendbyte(&msgbuf, PG_DIAG_SQLSTATE);
		err_sendstring(&msgbuf, unpack_sql_state(edata->sqlerrcode));

		/* M field is required per protocol, so always send something */
		pq_sendbyte(&msgbuf, PG_DIAG_MESSAGE_PRIMARY);
		if (edata->message)
			err_sendstring(&msgbuf, edata->message);
		else
			err_sendstring(&msgbuf, _("missing error text"));

		if (edata->detail)
		{
			pq_sendbyte(&msgbuf, PG_DIAG_MESSAGE_DETAIL);
			err_sendstring(&msgbuf, edata->detail);
		}

		/* detail_log is intentionally not used here */

		if (edata->hint)
		{
			pq_sendbyte(&msgbuf, PG_DIAG_MESSAGE_HINT);
			err_sendstring(&msgbuf, edata->hint);
		}

		if (edata->context && !(IsYugaByteEnabled() && edata->hide_ctx))
		{
			pq_sendbyte(&msgbuf, PG_DIAG_CONTEXT);
			err_sendstring(&msgbuf, edata->context);
		}

		if (edata->schema_name)
		{
			pq_sendbyte(&msgbuf, PG_DIAG_SCHEMA_NAME);
			err_sendstring(&msgbuf, edata->schema_name);
		}

		if (edata->table_name)
		{
			pq_sendbyte(&msgbuf, PG_DIAG_TABLE_NAME);
			err_sendstring(&msgbuf, edata->table_name);
		}

		if (edata->column_name)
		{
			pq_sendbyte(&msgbuf, PG_DIAG_COLUMN_NAME);
			err_sendstring(&msgbuf, edata->column_name);
		}

		if (edata->datatype_name)
		{
			pq_sendbyte(&msgbuf, PG_DIAG_DATATYPE_NAME);
			err_sendstring(&msgbuf, edata->datatype_name);
		}

		if (edata->constraint_name)
		{
			pq_sendbyte(&msgbuf, PG_DIAG_CONSTRAINT_NAME);
			err_sendstring(&msgbuf, edata->constraint_name);
		}

		if (edata->cursorpos > 0)
		{
			snprintf(tbuf, sizeof(tbuf), "%d", edata->cursorpos);
			pq_sendbyte(&msgbuf, PG_DIAG_STATEMENT_POSITION);
			err_sendstring(&msgbuf, tbuf);
		}

		if (edata->internalpos > 0)
		{
			snprintf(tbuf, sizeof(tbuf), "%d", edata->internalpos);
			pq_sendbyte(&msgbuf, PG_DIAG_INTERNAL_POSITION);
			err_sendstring(&msgbuf, tbuf);
		}

		if (edata->internalquery)
		{
			pq_sendbyte(&msgbuf, PG_DIAG_INTERNAL_QUERY);
			err_sendstring(&msgbuf, edata->internalquery);
		}

		if (edata->filename)
		{
			pq_sendbyte(&msgbuf, PG_DIAG_SOURCE_FILE);
			err_sendstring(&msgbuf, edata->filename);
		}

		if (edata->lineno > 0)
		{
			snprintf(tbuf, sizeof(tbuf), "%d", edata->lineno);
			pq_sendbyte(&msgbuf, PG_DIAG_SOURCE_LINE);
			err_sendstring(&msgbuf, tbuf);
		}

		if (edata->funcname)
		{
			pq_sendbyte(&msgbuf, PG_DIAG_SOURCE_FUNCTION);
			err_sendstring(&msgbuf, edata->funcname);
		}

		pq_sendbyte(&msgbuf, '\0'); /* terminator */

		pq_endmessage(&msgbuf);
	}
	else
	{
		/* Old style --- gin up a backwards-compatible message */
		StringInfoData buf;

		initStringInfo(&buf);

		appendStringInfo(&buf, "%s:  ", _(error_severity(edata->elevel)));

		if (edata->message)
			appendStringInfoString(&buf, edata->message);
		else
			appendStringInfoString(&buf, _("missing error text"));

		appendStringInfoChar(&buf, '\n');

		/* 'N' (Notice) is for nonfatal conditions, 'E' is for errors */
		pq_putmessage_v2((edata->elevel < ERROR) ? 'N' : 'E', buf.data, buf.len + 1);

		pfree(buf.data);
	}

	/*
	 * This flush is normally not necessary, since postgres.c will flush out
	 * waiting data when control returns to the main loop. But it seems best
	 * to leave it here, so that the client has some clue what happened if the
	 * backend dies before getting back to the main loop ... error/notice
	 * messages should not be a performance-critical path anyway, so an extra
	 * flush won't hurt much ...
	 */
	pq_flush();
}


/*
 * Support routines for formatting error messages.
 */

/*
 * A multi-thread friendly version of pg_strerror() (in strerror.c)
 *
 * Uses palloc'd buffer instead of static. Defined here so that we don't add a
 * requirement for strerror.c to depend on palloc.
 */
static const char *
yb_strerror(int errnum)
{
	char *errorstr_buf = (char *) palloc(PG_STRERROR_R_BUFLEN);

	return pg_strerror_r(errnum, errorstr_buf, PG_STRERROR_R_BUFLEN);
}

/*
 * error_severity --- get string representing elevel
 *
 * The string is not localized here, but we mark the strings for translation
 * so that callers can invoke _() on the result.
 */
const char *
error_severity(int elevel)
{
	const char *prefix;

	switch (elevel)
	{
		case DEBUG1:
		case DEBUG2:
		case DEBUG3:
		case DEBUG4:
		case DEBUG5:
			prefix = gettext_noop("DEBUG");
			break;
		case LOG:
		case LOG_SERVER_ONLY:
			prefix = gettext_noop("LOG");
			break;
		case INFO:
			prefix = gettext_noop("INFO");
			break;
		case NOTICE:
			prefix = gettext_noop("NOTICE");
			break;
		case WARNING:
		case WARNING_CLIENT_ONLY:
			prefix = gettext_noop("WARNING");
			break;
		case ERROR:
			prefix = gettext_noop("ERROR");
			break;
		case FATAL:
			prefix = gettext_noop("FATAL");
			break;
		case PANIC:
			prefix = gettext_noop("PANIC");
			break;
		default:
			prefix = "???";
			break;
	}

	return prefix;
}


/*
 *	append_with_tabs
 *
 *	Append the string to the StringInfo buffer, inserting a tab after any
 *	newline.
 */
static void
append_with_tabs(StringInfo buf, const char *str)
{
	char		ch;

	while ((ch = *str++) != '\0')
	{
		appendStringInfoCharMacro(buf, ch);
		if (ch == '\n')
			appendStringInfoCharMacro(buf, '\t');
	}
}


/*
 * Write errors to stderr (or by equal means when stderr is
 * not available). Used before ereport/elog can be used
 * safely (memory context, GUC load etc)
 */
void
write_stderr(const char *fmt,...)
{
	va_list		ap;

#ifdef WIN32
	char		errbuf[2048];	/* Arbitrary size? */
#endif

	fmt = _(fmt);

	va_start(ap, fmt);
#ifndef WIN32
	/* On Unix, we just fprintf to stderr */
	vfprintf(stderr, fmt, ap);
	fflush(stderr);
#else
	vsnprintf(errbuf, sizeof(errbuf), fmt, ap);

	/*
	 * On Win32, we print to stderr if running on a console, or write to
	 * eventlog if running as a service
	 */
	if (pgwin32_is_service())	/* Running as a service */
	{
		write_eventlog(ERROR, errbuf, strlen(errbuf));
	}
	else
	{
		/* Not running as service, write to stderr */
		write_console(errbuf, strlen(errbuf));
		fflush(stderr);
	}
#endif
	va_end(ap);
}


/*
 * Adjust the level of a recovery-related message per trace_recovery_messages.
 *
 * The argument is the default log level of the message, eg, DEBUG2.  (This
 * should only be applied to DEBUGn log messages, otherwise it's a no-op.)
 * If the level is >= trace_recovery_messages, we return LOG, causing the
 * message to be logged unconditionally (for most settings of
 * log_min_messages).  Otherwise, we return the argument unchanged.
 * The message will then be shown based on the setting of log_min_messages.
 *
 * Intention is to keep this for at least the whole of the 9.0 production
 * release, so we can more easily diagnose production problems in the field.
 * It should go away eventually, though, because it's an ugly and
 * hard-to-explain kluge.
 */
int
trace_recovery(int trace_level)
{
	if (trace_level < LOG &&
		trace_level >= trace_recovery_messages)
		return LOG;

	return trace_level;
}

/*
 * Custom format string handling
 *
 * The main problem we are trying to solve here is the national laguage support.
 * First of all, we do not want to format message in the YbGate environment.
 * There is no information what is the client's locale setting, and if we
 * format untranslated template, we wouldn't be able translate the formatted
 * message. Therefore we need to transmit the original template and the
 * arguments with the status.
 *
 * Next challenge here is that the arguments may be integers, floats, strings of
 * various sizes, and argument type details are defined by the template.
 * Therefore we parse the template to process the arguments.
 *
 * The Status supports transmission of parameters of various types, however if a
 * group of related parameters is transmitted, it is easier if they all of the
 * same type, so we convert all the arguments to strings and transmit them as a
 * vector of strings. The way how we do it is hacky: we parse out next template
 * from the format, make another format string containing the template alone and
 * use that format string to printf the next argument. The advantage of that
 * approach we do not have to be too thorough in template syntax checking, as
 * printf does that, we just need to identify template boundaries.
 *
 * The fact that we transfer arguments as string, not expected data types,
 * requires another custom formatting procedure on the receiving side. The
 * procedure translates the format string into desired language, finds the
 * templates using the same algorithm, and simply substitutes the argument
 * values.
 */
#define INT_TYPES "cdiouxX"
#define FLOAT_TYPES "aAeEfFgG"
#define STR_TYPES "ms"
#define MODIFIERS " -+*#0123456789.hjlLtz"
#define MAX_ATTR_LEN 16384

/*
 * Append an additional message to edata->message.  This function is expected to
 * be called uniformly across all instances of
 *
 *     ...EVALUATE_MESSAGE...(edata->domain, message, false...
 */
static void
yb_additional_errmsg(const char *fmt,...)
{
	ErrorData  *edata = &errordata[errordata_stack_depth];

	Assert(GetCurrentMemoryContext() == edata->assoc_context ||
		   GetCurrentMemoryContext() == ErrorContext);
	EVALUATE_MESSAGE(edata->domain, message, true /* appendval */,
					 false /* translateit */);
}

/*
 * yb_is_char_in_str - find if specified character exists in the c-string
 */
static bool
yb_is_char_in_str(const char ch, const char *str)
{
	while (*str != '\0')
	{
		if (*str++ == ch)
			return true;
	}
	return false;
}

/*
 * yb_next_template - find next template in the format string
 *
 * Scan the format string to find next argument placeholder
 * Returns the pointer to the beginning of the template, and sets following
 * output parameters:
 * len - total template length
 * nstars - if decimal argument template has length/precision qualifier (*[.*]),
 * that many additional arguments are needed to properly format the template.
 * is_long - numeric type has a "long" qualifier.
 */
static const char *
yb_next_template(const char *fmt, int *len, int *nstars, bool *is_long)
{
	const char *template = NULL;
	while (fmt != NULL && *fmt != '\0')
	{
		if (*fmt == '%')
		{
			if (template == NULL)
				template = fmt; /* potential template start */
			else if (template == fmt - 1)
				template = NULL; /* found double %, not a template */
			else
				break; /* invalid format */
		}
		else if (template) /* template continues */
		{
			if (yb_is_char_in_str(*fmt, STR_TYPES INT_TYPES FLOAT_TYPES))
			{
				/* template end */
				*len = fmt - template + 1;
				return template;
			}
			else if (yb_is_char_in_str(*fmt, MODIFIERS))
			{
				/* template qualifier */
				if (is_long && (*fmt == 'l' || *fmt == 'L'))
					*is_long = true;
				if (nstars && *fmt == '*')
					(*nstars)++;
			}
			else
				break; /* invalid format */
		}
		fmt++;
	}
	return NULL;
}

/*
 * yb_edata_add_arg - append a string argument to the array in YbgError
 *
 * The argument is copied to the current memory context, capacity is increased
 * if necessary.
 */
static void
yb_edata_add_arg(YbgError edata, const char *arg)
{
	/* Check capacity */
	if (edata->nargs >= edata->argsize)
	{
		/*
		 * Four arguments is sufficient in most cases, but it can grow if needed
		 */
		edata->argsize += 4;
		if (edata->errargs)
			/* Extend existing arguments array */
			edata->errargs = (const char **)
				repalloc(edata->errargs, edata->argsize * sizeof(const char *));
		else
			/* Create new arguments array */
			edata->errargs = (const char **)
				palloc(edata->argsize * sizeof(const char *));
	}
	edata->errargs[edata->nargs++] = pstrdup(arg);
}

/*
 * Render one argument using the template found in the format string and add
 * the result to the argument list in edata.
 */
#define FORMAT_ONE_VALUE(edata, one_fmt, _type_, nstars, estimated_size) \
	do \
	{ \
		if ((nstars) < 0 || (nstars) > 2) \
			return; \
		const char *one_fmt_ = (one_fmt); \
		int		star1 = (nstars) > 0 ? va_arg(args, int) : 0; \
		int		star2 = (nstars) > 1 ? va_arg(args, int) : 0; \
		_type_	value = va_arg(args, _type_); \
		int		n = (estimated_size); \
		bool	done = false; \
		while (!done) \
		{ \
			char	outbuf[n]; \
			int		actual; \
			if (nstars == 0) \
				actual = snprintf(outbuf, n, one_fmt_, value); \
			else if (nstars == 1) \
				actual = snprintf(outbuf, n, one_fmt_, star1, value); \
			else \
				actual = snprintf(outbuf, n, one_fmt_, star1, star2, value); \
			if (actual < 0) \
				return; \
			if (actual < n) \
			{ \
				yb_edata_add_arg(edata, outbuf); \
				done = true; \
			} \
			else \
			{ \
				if (n >= MAX_ATTR_LEN - 1) \
				{ \
					yb_edata_add_arg(edata, "[argument is too long]"); \
					done = true; \
				} \
				n = Min(Max(2 * n, actual + 1), MAX_ATTR_LEN - 1); \
			} \
		} \
	} while (0)

/*
 * yb_errmsg_va - add error message format strings and arguments to current
 * YbgError frame.
 */
static void
yb_errmsg_va(const char *fmt, va_list args)
{
	YbgStatus	status = YBCPgGetThreadLocalErrStatus();
	int			len = 0;
	int			nstars = 0;
	bool		is_long = false;
	const char *next;

	YbgError		edata = (YbgError) YbgStatusGetEdata(status);
	MemoryContext	error_context = (MemoryContext) YbgStatusGetContext(status);
	MemoryContext	old_context = MemoryContextSwitchTo(error_context);
	/* Save copy of the format string to edata */
	edata->errmsg = pstrdup(fmt);
	while ((next = yb_next_template(fmt, &len, &nstars, &is_long)) != NULL)
	{
		char *onefmt = pnstrdup(next, len);
		char ttype = onefmt[len - 1];
		if (yb_is_char_in_str(ttype, STR_TYPES))
		{
			/* %m is a Postgres format extension, obtain system error message */
			if (ttype == 'm')
			{
				const char *err = yb_strerror(edata->saved_errno);
				yb_edata_add_arg(edata, err);
			}
			else
			{
				Assert(ttype == 's');
				/* char * parameter, we can get exact size */
				FORMAT_ONE_VALUE(edata, onefmt, char *, nstars,
								 strlen(value) + 1);
			}
		}
		else if (yb_is_char_in_str(ttype, INT_TYPES))
		{
			/* integer parameter */
			if (is_long)
				FORMAT_ONE_VALUE(edata, onefmt, long long, nstars, 24);
			else
				FORMAT_ONE_VALUE(edata, onefmt, int, nstars, 12);
		} else {
			Assert(yb_is_char_in_str(ttype, FLOAT_TYPES));
			/* float parameter */
			if (is_long)
				FORMAT_ONE_VALUE(edata, onefmt, long double, nstars, 32);
			else
				FORMAT_ONE_VALUE(edata, onefmt, double, nstars, 32);
		}
		pfree(onefmt);
		fmt = next + len;
		nstars = 0;
		is_long = false;
	}
	MemoryContextSwitchTo(old_context);
}

/*
 * yb_format_and_append - format a string with provided string arguments,
 * and append to buf.
 *
 * The function takes printf-like format strings and substitute strings from the
 * args array. Unlike printf, function takes strings instead of values of types
 * defined by the template.
 *
 * Function is designed to work with DocDB Status object's data, which carries
 * message arguments as strings. Currently Status only takes the message, in
 * future we may add support for more fields.
 */
static void
yb_format_and_append(StringInfo buf, const char *fmt, const size_t nargs,
					 const char **args)
{
	const char *next;
	int			len = 0;
	int			argno = 0;
	/* If no more arguments left, no need to parse the string */
	while (argno < nargs &&
		   (next = yb_next_template(fmt, &len,
									NULL /* nstars */,
									NULL /* is_long */)) != NULL)
	{
		/* Copy the part of the format string before the template */
		if (next != fmt)
			appendBinaryStringInfo(buf, fmt, next - fmt);
		/* Copy next argument, if present */
		appendStringInfoString(buf, args[argno++]);
		/* move the pointer */
		fmt = next + len;
	}
	/* Copy the rest of the format string to the output buffer */
	appendStringInfoString(buf, fmt);
}

/*
 * Same as EVALUATE_MESSAGE except
 * - assert not multithreaded mode
 * - get args from passed nargs and args rather than VA args
 * - generate actual output using yb_format_and_append
 */
#define YB_EVALUATE_MESSAGE_FROM_STATUS(domain, targetfield, appendval, \
										translateit, nargs, args) \
	{ \
		AssertMacro(!IsMultiThreadedMode()); \
		StringInfoData	buf; \
		/* Internationalize the error format string */ \
		if ((translateit) && !in_error_recursion_trouble()) \
			fmt = dgettext((domain), fmt);				  \
		initStringInfo(&buf); \
		if ((appendval) && edata->targetfield) { \
			appendStringInfoString(&buf, edata->targetfield); \
			appendStringInfoChar(&buf, '\n'); \
		} \
		/* Generate actual output --- have to use appendStringInfoVA */ \
		yb_format_and_append(&buf, fmt, nargs, args); \
		/* Save the completed message into the stack item */ \
		if (edata->targetfield) \
			pfree(edata->targetfield); \
		edata->targetfield = pstrdup(buf.data); \
		pfree(buf.data); \
	}

/*
 * yb_errmsg_from_status - set error message from Status data
 *
 * The yb_errmsg_from_status is equivalent of errmsg to work in
 * HandleYBStatus context, rather then in ereport. The
 * yb_errmsg_from_status sets message field on current ErrorData frame
 * from the info retrieved from DocDB Status object.
 *
 * Status object carries messages as a format string and string arguments for
 * rendering on the client. Therefore the functions parses the format string
 * only to find template locations, and ignores their data types and formatting
 * flags.
 *
 * Please refer yb_errmsg_va() for details on how message and argument are
 * prepared before they are stored into a Status object.
 */
int
yb_errmsg_from_status(const char *fmt, const size_t nargs, const char **args)
{
	Assert(!IsMultiThreadedMode());

	ErrorData  *edata = &errordata[errordata_stack_depth];
	MemoryContext oldcontext;

	recursion_depth++;
	CHECK_STACK_DEPTH();
	oldcontext = MemoryContextSwitchTo(edata->assoc_context);

	edata->message_id = fmt;
	YB_EVALUATE_MESSAGE_FROM_STATUS(edata->domain, message,
									false /* appendval */,
									true /* translateit */, nargs, args);

	Assert(IsYugaByteEnabled());
	if (yb_debug_report_error_stacktrace)
		yb_additional_errmsg("%s", YBCGetStackTrace());

	MemoryContextSwitchTo(oldcontext);
	recursion_depth--;
	return 0;					/* return value does not matter */
}

int
yb_errdetail_from_status(const char *fmt, const size_t nargs, const char **args)
{
	Assert(!IsMultiThreadedMode());

	ErrorData  *edata = &errordata[errordata_stack_depth];
	MemoryContext oldcontext;

	recursion_depth++;
	CHECK_STACK_DEPTH();
	oldcontext = MemoryContextSwitchTo(edata->assoc_context);

	YB_EVALUATE_MESSAGE_FROM_STATUS(edata->domain, detail,
									false /* appendval */,
									true /* translateit */, nargs, args);

	MemoryContextSwitchTo(oldcontext);
	recursion_depth--;
	return 0;					/* return value does not matter */
}

/*
 * Set the given field to the given value (assumed to be palloc-d) or, if the
 * new value is null, pstrdup the existing value of the field to ensure that
 * we end up with a palloc-d or null value in any case.
 */
static void
yb_set_or_pstrdup_err_field(const char** field, const char* new_value)
{
	if (new_value)
	{
		*field = new_value;
		return;
	}
	if (*field)
	{
		/* Assume the existing value originates from __FILE__ or __func__. */
		*field = pstrdup(*field);
	}
}

/*
 * Set palloc-d filename and funcname and makes sure they are pfreed at the end.
 */
void
yb_set_pallocd_error_file_and_func(const char* filename, const char* funcname)
{
	Assert(!IsMultiThreadedMode());
	ErrorData *edata = &errordata[errordata_stack_depth];
	yb_set_or_pstrdup_err_field(&edata->filename, filename);
	yb_set_or_pstrdup_err_field(&edata->funcname, funcname);
	edata->yb_owns_file_and_func = true;
}
