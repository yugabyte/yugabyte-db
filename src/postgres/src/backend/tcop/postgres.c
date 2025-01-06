/*-------------------------------------------------------------------------
 *
 * postgres.c
 *	  POSTGRES C Backend Interface
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/backend/tcop/postgres.c
 *
 * NOTES
 *	  this is the "main" module of the postgres backend and
 *	  hence the main module of the "traffic cop".
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <unistd.h>
#include <sys/socket.h>
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif
#ifdef HAVE_SYS_RESOURCE_H
#include <sys/time.h>
#include <sys/resource.h>
#endif

#ifndef HAVE_GETRUSAGE
#include "rusagestub.h"
#endif

#include "access/parallel.h"
#include "access/printtup.h"
#include "access/xact.h"
#include "catalog/pg_type.h"
#include "commands/async.h"
#include "commands/prepare.h"
#include "common/pg_prng.h"
#include "jit/jit.h"

#include "libpq/auth.h"
#include "libpq/libpq.h"
#include "libpq/pqformat.h"
#include "libpq/pqsignal.h"
#include "mb/pg_wchar.h"
#include "mb/stringinfo_mb.h"
#include "miscadmin.h"
#include "nodes/print.h"
#include "optimizer/optimizer.h"
#include "parser/analyze.h"
#include "parser/parser.h"
#include "pg_getopt.h"
#include "pg_trace.h"
#include "pgstat.h"
#include "postmaster/autovacuum.h"
#include "postmaster/interrupt.h"
#include "postmaster/postmaster.h"
#include "replication/logicallauncher.h"
#include "replication/logicalworker.h"
#include "replication/slot.h"
#include "replication/walsender.h"
#include "rewrite/rewriteHandler.h"
#include "storage/bufmgr.h"
#include "storage/ipc.h"
#include "storage/pmsignal.h"
#include "storage/proc.h"
#include "storage/procsignal.h"
#include "storage/sinval.h"
#include "tcop/fastpath.h"
#include "tcop/pquery.h"
#include "tcop/tcopprot.h"
#include "tcop/utility.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"
#include "utils/ps_status.h"
#include "utils/snapmgr.h"
#include "utils/timeout.h"
#include "utils/timestamp.h"
#include "utils/varlena.h"

#include "catalog/yb_catalog_version.h"
#include "commands/portalcmds.h"
#include "libpq/yb_pqcomm_extensions.h"
#include "pg_yb_utils.h"
#include "utils/builtins.h"
#include "utils/catcache.h"
#include "utils/inval.h"
#include "utils/rel.h"
#include "utils/relcache.h"
#include "utils/syscache.h"

/* YB includes */
#include "replication/walsender_private.h"
#include "utils/guc_tables.h"

/* ----------------
 *		global variables
 * ----------------
 */
const char *debug_query_string; /* client-supplied query string */

/* Note: whereToSendOutput is initialized for the bootstrap/standalone case */
CommandDest whereToSendOutput = DestDebug;

/* flag for logging end of session */
bool		Log_disconnections = false;

int			log_statement = LOGSTMT_NONE;

/* GUC variable for maximum stack depth (measured in kilobytes) */
int			max_stack_depth = 100;

/* wait N seconds to allow attach from a debugger */
int			PostAuthDelay = 0;

/* Time between checks that the client is still connected. */
int			client_connection_check_interval = 0;

/* flags for non-system relation kinds to restrict use */
int			restrict_nonsystem_relation_kind;

/* ----------------
 *		private typedefs etc
 * ----------------
 */

/* type of argument for bind_param_error_callback */
typedef struct BindParamCbData
{
	const char *portalName;
	int			paramno;		/* zero-based param number, or -1 initially */
	const char *paramval;		/* textual input string, if available */
} BindParamCbData;

/* ----------------
 *		private variables
 * ----------------
 */

/* max_stack_depth converted to bytes for speed of checking */
static long max_stack_depth_bytes = 100 * 1024L;

/*
 * Stack base pointer -- initialized by PostmasterMain and inherited by
 * subprocesses (but see also InitPostmasterChild).
 */
static char *stack_base_ptr = NULL;

/*
 * On IA64 we also have to remember the register stack base.
 */
#if defined(__ia64__) || defined(__ia64)
static char *register_stack_base_ptr = NULL;
#endif

/*
 * Flag to keep track of whether we have started a transaction.
 * For extended query protocol this has to be remembered across messages.
 */
static bool xact_started = false;

/*
 * Flag to indicate that we are doing the outer loop's read-from-client,
 * as opposed to any random read from client that might happen within
 * commands like COPY FROM STDIN.
 */
static bool DoingCommandRead = false;

/*
 * Flags to implement skip-till-Sync-after-error behavior for messages of
 * the extended query protocol.
 */
static bool doing_extended_query_message = false;
static bool ignore_till_sync = false;

/*
 * If an unnamed prepared statement exists, it's stored here.
 * We keep it separate from the hashtable kept by commands/prepare.c
 * in order to reduce overhead for short-lived queries.
 */
static CachedPlanSource *unnamed_stmt_psrc = NULL;

/* assorted command-line switches */
static const char *userDoption = NULL;	/* -D switch */
static bool EchoQuery = false;	/* -E switch */
static bool UseSemiNewlineNewline = false;	/* -j switch */

/* whether or not, and why, we were canceled by conflict with recovery */
static bool RecoveryConflictPending = false;
static bool RecoveryConflictRetryable = true;
static ProcSignalReason RecoveryConflictReason;

/* reused buffer to pass to SendRowDescriptionMessage() */
static MemoryContext row_description_context = NULL;
static StringInfoData row_description_buf;

/* Flag to mark cache as invalid if discovered within a txn block. */
static bool yb_need_cache_refresh = false;

/* whether or not we are executing a multi-statement query received via simple query protocol */
static bool yb_is_multi_statement_query = false;

/*
 * String constants used for redacting text after the password token in
 * CREATE/ALTER ROLE commands.
 */
#define TOKEN_PASSWORD "password"
#define TOKEN_REDACTED "<REDACTED>"

/* ----------------------------------------------------------------
 *		decls for routines only used in this file
 * ----------------------------------------------------------------
 */
static int	InteractiveBackend(StringInfo inBuf);
static int	interactive_getc(void);
static int	SocketBackend(StringInfo inBuf);
static int	ReadCommand(StringInfo inBuf);
static void forbidden_in_wal_sender(char firstchar);
static bool check_log_statement(List *stmt_list);
static int	errdetail_execute(List *raw_parsetree_list);
static int	errdetail_params(ParamListInfo params);
static int	errdetail_abort(void);
static int	errdetail_recovery_conflict(void);
static void bind_param_error_callback(void *arg);
static void start_xact_command(void);
static void finish_xact_command(void);
static bool IsTransactionExitStmt(Node *parsetree);
static bool IsTransactionExitStmtList(List *pstmts);
static bool IsTransactionStmtList(List *pstmts);
static void drop_unnamed_stmt(void);
static void log_disconnections(int code, Datum arg);
static void enable_statement_timeout(void);
static void disable_statement_timeout(void);
static void yb_start_xact_command_internal(bool yb_skip_read_committed_internal_savepoint);


/* ----------------------------------------------------------------
 *		routines to obtain user input
 * ----------------------------------------------------------------
 */

/* ----------------
 *	InteractiveBackend() is called for user interactive connections
 *
 *	the string entered by the user is placed in its parameter inBuf,
 *	and we act like a Q message was received.
 *
 *	EOF is returned if end-of-file input is seen; time to shut down.
 * ----------------
 */

static int
InteractiveBackend(StringInfo inBuf)
{
	int			c;				/* character read from getc() */

	/*
	 * display a prompt and obtain input from the user
	 */
	printf("backend> ");
	fflush(stdout);

	resetStringInfo(inBuf);

	/*
	 * Read characters until EOF or the appropriate delimiter is seen.
	 */
	while ((c = interactive_getc()) != EOF)
	{
		if (c == '\n')
		{
			if (UseSemiNewlineNewline)
			{
				/*
				 * In -j mode, semicolon followed by two newlines ends the
				 * command; otherwise treat newline as regular character.
				 */
				if (inBuf->len > 1 &&
					inBuf->data[inBuf->len - 1] == '\n' &&
					inBuf->data[inBuf->len - 2] == ';')
				{
					/* might as well drop the second newline */
					break;
				}
			}
			else
			{
				/*
				 * In plain mode, newline ends the command unless preceded by
				 * backslash.
				 */
				if (inBuf->len > 0 &&
					inBuf->data[inBuf->len - 1] == '\\')
				{
					/* discard backslash from inBuf */
					inBuf->data[--inBuf->len] = '\0';
					/* discard newline too */
					continue;
				}
				else
				{
					/* keep the newline character, but end the command */
					appendStringInfoChar(inBuf, '\n');
					break;
				}
			}
		}

		/* Not newline, or newline treated as regular character */
		appendStringInfoChar(inBuf, (char) c);
	}

	/* No input before EOF signal means time to quit. */
	if (c == EOF && inBuf->len == 0)
		return EOF;

	/*
	 * otherwise we have a user query so process it.
	 */

	/* Add '\0' to make it look the same as message case. */
	appendStringInfoChar(inBuf, (char) '\0');

	/*
	 * if the query echo flag was given, print the query..
	 */
	if (EchoQuery)
		printf("statement: %s\n", inBuf->data);
	fflush(stdout);

	return 'Q';
}

/*
 * interactive_getc -- collect one character from stdin
 *
 * Even though we are not reading from a "client" process, we still want to
 * respond to signals, particularly SIGTERM/SIGQUIT.
 */
static int
interactive_getc(void)
{
	int			c;

	/*
	 * This will not process catchup interrupts or notifications while
	 * reading. But those can't really be relevant for a standalone backend
	 * anyway. To properly handle SIGTERM there's a hack in die() that
	 * directly processes interrupts at this stage...
	 */
	CHECK_FOR_INTERRUPTS();

	c = getc(stdin);

	ProcessClientReadInterrupt(false);

	return c;
}

/* ----------------
 *	SocketBackend()		Is called for frontend-backend connections
 *
 *	Returns the message type code, and loads message body data into inBuf.
 *
 *	EOF is returned if the connection is lost.
 * ----------------
 */
static int
SocketBackend(StringInfo inBuf)
{
	int			qtype;
	int			maxmsglen;

	/*
	 * Get message type code from the frontend.
	 */
	HOLD_CANCEL_INTERRUPTS();
	pq_startmsgread();
	qtype = pq_getbyte();

	if (qtype == EOF)			/* frontend disconnected */
	{
		if (IsTransactionState())
			ereport(COMMERROR,
					(errcode(ERRCODE_CONNECTION_FAILURE),
					 errmsg("unexpected EOF on client connection with an open transaction")));
		else
		{
			/*
			 * Can't send DEBUG log messages to client at this point. Since
			 * we're disconnecting right away, we don't need to restore
			 * whereToSendOutput.
			 */
			whereToSendOutput = DestNone;
			ereport(DEBUG1,
					(errcode(ERRCODE_CONNECTION_DOES_NOT_EXIST),
					 errmsg_internal("unexpected EOF on client connection")));
		}
		return qtype;
	}

	/*
	 * Validate message type code before trying to read body; if we have lost
	 * sync, better to say "command unknown" than to run out of memory because
	 * we used garbage as a length word.  We can also select a type-dependent
	 * limit on what a sane length word could be.  (The limit could be chosen
	 * more granularly, but it's not clear it's worth fussing over.)
	 *
	 * This also gives us a place to set the doing_extended_query_message flag
	 * as soon as possible.
	 */
	switch (qtype)
	{
		case 'Q':				/* simple query */
			maxmsglen = PQ_LARGE_MESSAGE_LIMIT;
			doing_extended_query_message = false;
			break;

		case 'F':				/* fastpath function call */
			maxmsglen = PQ_LARGE_MESSAGE_LIMIT;
			doing_extended_query_message = false;
			break;

		case 'X':				/* terminate */
			maxmsglen = PQ_SMALL_MESSAGE_LIMIT;
			doing_extended_query_message = false;
			ignore_till_sync = false;
			break;

		case 'B':				/* bind */
		case 'P':				/* parse */
			maxmsglen = PQ_LARGE_MESSAGE_LIMIT;
			doing_extended_query_message = true;
			break;

		case 'C':				/* close */
		case 'D':				/* describe */
		case 'E':				/* execute */
		case 'H':				/* flush */
			maxmsglen = PQ_SMALL_MESSAGE_LIMIT;
			doing_extended_query_message = true;
			break;

		case 'S':				/* sync */
			maxmsglen = PQ_SMALL_MESSAGE_LIMIT;
			/* stop any active skip-till-Sync */
			ignore_till_sync = false;
			/* mark not-extended, so that a new error doesn't begin skip */
			doing_extended_query_message = false;
			break;

		case 'd':				/* copy data */
			maxmsglen = PQ_LARGE_MESSAGE_LIMIT;
			doing_extended_query_message = false;
			break;

		case 'c':				/* copy done */
		case 'f':				/* copy fail */
			maxmsglen = PQ_SMALL_MESSAGE_LIMIT;
			doing_extended_query_message = false;
			break;

		case 'A': /* Auth Passthrough Request */
			maxmsglen = PQ_SMALL_MESSAGE_LIMIT;
			if (!YbIsClientYsqlConnMgr())
				ereport(FATAL,
						(errcode(ERRCODE_PROTOCOL_VIOLATION),
						 errmsg("invalid frontend message type %d", qtype)));
			break;

		case 's': /* SET SESSION PARAMETER */
			maxmsglen = PQ_SMALL_MESSAGE_LIMIT;
			if (!YbIsClientYsqlConnMgr())
				ereport(FATAL,
						(errcode(ERRCODE_PROTOCOL_VIOLATION),
						 errmsg("invalid frontend message type %d", qtype)));
			break;

		default:

			/*
			 * Otherwise we got garbage from the frontend.  We treat this as
			 * fatal because we have probably lost message boundary sync, and
			 * there's no good way to recover.
			 */
			ereport(FATAL,
					(errcode(ERRCODE_PROTOCOL_VIOLATION),
					 errmsg("invalid frontend message type %d", qtype)));
			maxmsglen = 0;		/* keep compiler quiet */
			break;
	}

	/*
	 * In protocol version 3, all frontend messages have a length word next
	 * after the type code; we can read the message contents independently of
	 * the type.
	 */
	if (pq_getmessage(inBuf, maxmsglen))
		return EOF;				/* suitable message already logged */

	if (IsYugaByteEnabled())
	{
		switch(qtype)
		{
			case 'E':
				switch (yb_pg_batch_detection_mechanism)
				{
					case ASSUME_ALL_BATCH_EXECUTIONS:
						YbSetIsBatchedExecution(true);
						break;
					case DETECT_BY_PEEKING:
						if (!YbIsBatchedExecution() &&
							yb_pq_peekbyte_no_msg_reading_status_check() != 'S')
							YbSetIsBatchedExecution(true);
						break;
				}
				break;
			case 'S':
				YbSetIsBatchedExecution(false);
				break;
			default:
				break;
		}
	}

	RESUME_CANCEL_INTERRUPTS();

	return qtype;
}

/* ----------------
 *		ReadCommand reads a command from either the frontend or
 *		standard input, places it in inBuf, and returns the
 *		message type code (first byte of the message).
 *		EOF is returned if end of file.
 * ----------------
 */
static int
ReadCommand(StringInfo inBuf)
{
	int			result;

	if (whereToSendOutput == DestRemote)
		result = SocketBackend(inBuf);
	else
	{
		result = InteractiveBackend(inBuf);
	}
	return result;
}

/*
 * ProcessClientReadInterrupt() - Process interrupts specific to client reads
 *
 * This is called just before and after low-level reads.
 * 'blocked' is true if no data was available to read and we plan to retry,
 * false if about to read or done reading.
 *
 * Must preserve errno!
 */
void
ProcessClientReadInterrupt(bool blocked)
{
	int			save_errno = errno;

	if (DoingCommandRead)
	{
		/* Check for general interrupts that arrived before/while reading */
		CHECK_FOR_INTERRUPTS();

		/* Process sinval catchup interrupts, if any */
		if (catchupInterruptPending)
			ProcessCatchupInterrupt();

		/* Process notify interrupts, if any */
		if (notifyInterruptPending)
			ProcessNotifyInterrupt(true);
	}
	else if (ProcDiePending)
	{
		/*
		 * We're dying.  If there is no data available to read, then it's safe
		 * (and sane) to handle that now.  If we haven't tried to read yet,
		 * make sure the process latch is set, so that if there is no data
		 * then we'll come back here and die.  If we're done reading, also
		 * make sure the process latch is set, as we might've undesirably
		 * cleared it while reading.
		 */
		if (blocked)
			CHECK_FOR_INTERRUPTS();
		else
			SetLatch(MyLatch);
	}

	errno = save_errno;
}

/*
 * ProcessClientWriteInterrupt() - Process interrupts specific to client writes
 *
 * This is called just before and after low-level writes.
 * 'blocked' is true if no data could be written and we plan to retry,
 * false if about to write or done writing.
 *
 * Must preserve errno!
 */
void
ProcessClientWriteInterrupt(bool blocked)
{
	int			save_errno = errno;

	if (ProcDiePending)
	{
		/*
		 * We're dying.  If it's not possible to write, then we should handle
		 * that immediately, else a stuck client could indefinitely delay our
		 * response to the signal.  If we haven't tried to write yet, make
		 * sure the process latch is set, so that if the write would block
		 * then we'll come back here and die.  If we're done writing, also
		 * make sure the process latch is set, as we might've undesirably
		 * cleared it while writing.
		 */
		if (blocked)
		{
			/*
			 * Don't mess with whereToSendOutput if ProcessInterrupts wouldn't
			 * service ProcDiePending.
			 */
			if (InterruptHoldoffCount == 0 && CritSectionCount == 0)
			{
				/*
				 * We don't want to send the client the error message, as a)
				 * that would possibly block again, and b) it would likely
				 * lead to loss of protocol sync because we may have already
				 * sent a partial protocol message.
				 */
				if (whereToSendOutput == DestRemote)
					whereToSendOutput = DestNone;

				CHECK_FOR_INTERRUPTS();
			}
		}
		else
			SetLatch(MyLatch);
	}

	errno = save_errno;
}

/*
 * Do raw parsing (only).
 *
 * A list of parsetrees (RawStmt nodes) is returned, since there might be
 * multiple commands in the given string.
 *
 * NOTE: for interactive queries, it is important to keep this routine
 * separate from the analysis & rewrite stages.  Analysis and rewriting
 * cannot be done in an aborted transaction, since they require access to
 * database tables.  So, we rely on the raw parser to determine whether
 * we've seen a COMMIT or ABORT command; when we are in abort state, other
 * commands are not processed any further than the raw parse stage.
 */
List *
pg_parse_query(const char *query_string)
{
	List	   *raw_parsetree_list;

	TRACE_POSTGRESQL_QUERY_PARSE_START(query_string);

	if (log_parser_stats)
		ResetUsage();

	raw_parsetree_list = raw_parser(query_string, RAW_PARSE_DEFAULT);

	if (log_parser_stats)
		ShowUsage("PARSER STATISTICS");

#ifdef COPY_PARSE_PLAN_TREES
	/* Optional debugging check: pass raw parsetrees through copyObject() */
	{
		List	   *new_list = copyObject(raw_parsetree_list);

		/* This checks both copyObject() and the equal() routines... */
		if (!equal(new_list, raw_parsetree_list))
			elog(WARNING, "copyObject() failed to produce an equal raw parse tree");
		else
			raw_parsetree_list = new_list;
	}
#endif

	/*
	 * Currently, outfuncs/readfuncs support is missing for many raw parse
	 * tree nodes, so we don't try to implement WRITE_READ_PARSE_PLAN_TREES
	 * here.
	 */

	TRACE_POSTGRESQL_QUERY_PARSE_DONE(query_string);

	return raw_parsetree_list;
}

static bool
yb_skip_read_committed_internal_savepoint(CommandTag command_tag)
{
	/*
	 * In the common case, when a "BEGIN;" statement is issued, an internal save point is not
	 * registered because we are in the TBLOCK_DEFAULT state (when calling StartTransactionCommand).
	 * However, we explicitly chose to add a check to skip for "BEGIN;" because there can be cases
	 * when a "BEGIN;" is called while a transaction block is already in progress and hence we are not
	 * in TBLOCK_DEFAULT state. We want to skip registering an internal savepoint in such situations
	 * too.
	 */

	bool skip =
		command_tag == CMDTAG_SET ||
		command_tag == CMDTAG_BEGIN ||
		command_tag == CMDTAG_RELEASE ||
		command_tag == CMDTAG_SAVEPOINT;
	elog(DEBUG2, "Skip rc sub-txn: %d, command tag: %s", skip, GetCommandTagName(command_tag));
	return skip;
}

/*
 * Given a raw parsetree (gram.y output), and optionally information about
 * types of parameter symbols ($n), perform parse analysis and rule rewriting.
 *
 * A list of Query nodes is returned, since either the analyzer or the
 * rewriter might expand one query to several.
 *
 * NOTE: for reasons mentioned above, this must be separate from raw parsing.
 */
List *
pg_analyze_and_rewrite_fixedparams(RawStmt *parsetree,
								   const char *query_string,
								   const Oid *paramTypes,
								   int numParams,
								   QueryEnvironment *queryEnv)
{
	Query	   *query;
	List	   *querytree_list;

	TRACE_POSTGRESQL_QUERY_REWRITE_START(query_string);

	/*
	 * (1) Perform parse analysis.
	 */
	if (log_parser_stats)
		ResetUsage();

	query = parse_analyze_fixedparams(parsetree, query_string, paramTypes, numParams,
									  queryEnv);

	if (log_parser_stats)
		ShowUsage("PARSE ANALYSIS STATISTICS");

	/*
	 * (2) Rewrite the queries, as necessary
	 */
	querytree_list = pg_rewrite_query(query);

	TRACE_POSTGRESQL_QUERY_REWRITE_DONE(query_string);

	return querytree_list;
}

/*
 * Do parse analysis and rewriting.  This is the same as
 * pg_analyze_and_rewrite_fixedparams except that it's okay to deduce
 * information about $n symbol datatypes from context.
 */
List *
pg_analyze_and_rewrite_varparams(RawStmt *parsetree,
								 const char *query_string,
								 Oid **paramTypes,
								 int *numParams,
								 QueryEnvironment *queryEnv)
{
	Query	   *query;
	List	   *querytree_list;

	TRACE_POSTGRESQL_QUERY_REWRITE_START(query_string);

	/*
	 * (1) Perform parse analysis.
	 */
	if (log_parser_stats)
		ResetUsage();

	query = parse_analyze_varparams(parsetree, query_string, paramTypes, numParams,
									queryEnv);

	/*
	 * Check all parameter types got determined.
	 */
	for (int i = 0; i < *numParams; i++)
	{
		Oid			ptype = (*paramTypes)[i];

		if (ptype == InvalidOid || ptype == UNKNOWNOID)
			ereport(ERROR,
					(errcode(ERRCODE_INDETERMINATE_DATATYPE),
					 errmsg("could not determine data type of parameter $%d",
							i + 1)));
	}

	if (log_parser_stats)
		ShowUsage("PARSE ANALYSIS STATISTICS");

	/*
	 * (2) Rewrite the queries, as necessary
	 */
	querytree_list = pg_rewrite_query(query);

	TRACE_POSTGRESQL_QUERY_REWRITE_DONE(query_string);

	return querytree_list;
}

/*
 * Do parse analysis and rewriting.  This is the same as
 * pg_analyze_and_rewrite_fixedparams except that, instead of a fixed list of
 * parameter datatypes, a parser callback is supplied that can do
 * external-parameter resolution and possibly other things.
 */
List *
pg_analyze_and_rewrite_withcb(RawStmt *parsetree,
							  const char *query_string,
							  ParserSetupHook parserSetup,
							  void *parserSetupArg,
							  QueryEnvironment *queryEnv)
{
	Query	   *query;
	List	   *querytree_list;

	TRACE_POSTGRESQL_QUERY_REWRITE_START(query_string);

	/*
	 * (1) Perform parse analysis.
	 */
	if (log_parser_stats)
		ResetUsage();

	query = parse_analyze_withcb(parsetree, query_string, parserSetup, parserSetupArg,
								 queryEnv);

	if (log_parser_stats)
		ShowUsage("PARSE ANALYSIS STATISTICS");

	/*
	 * (2) Rewrite the queries, as necessary
	 */
	querytree_list = pg_rewrite_query(query);

	TRACE_POSTGRESQL_QUERY_REWRITE_DONE(query_string);

	return querytree_list;
}

/*
 * Perform rewriting of a query produced by parse analysis.
 *
 * Note: query must just have come from the parser, because we do not do
 * AcquireRewriteLocks() on it.
 */
List *
pg_rewrite_query(Query *query)
{
	List	   *querytree_list;

	if (Debug_print_parse)
		elog_node_display(LOG, "parse tree", query,
						  Debug_pretty_print);

	if (log_parser_stats)
		ResetUsage();

	if (query->commandType == CMD_UTILITY)
	{
		/* don't rewrite utilities, just dump 'em into result list */
		querytree_list = list_make1(query);
	}
	else
	{
		/* rewrite regular queries */
		querytree_list = QueryRewrite(query);
	}

	if (log_parser_stats)
		ShowUsage("REWRITER STATISTICS");

#ifdef COPY_PARSE_PLAN_TREES
	/* Optional debugging check: pass querytree through copyObject() */
	{
		List	   *new_list;

		new_list = copyObject(querytree_list);
		/* This checks both copyObject() and the equal() routines... */
		if (!equal(new_list, querytree_list))
			elog(WARNING, "copyObject() failed to produce equal parse tree");
		else
			querytree_list = new_list;
	}
#endif

#ifdef WRITE_READ_PARSE_PLAN_TREES
	/* Optional debugging check: pass querytree through outfuncs/readfuncs */
	{
		List	   *new_list = NIL;
		ListCell   *lc;

		/*
		 * We currently lack outfuncs/readfuncs support for most utility
		 * statement types, so only attempt to write/read non-utility queries.
		 */
		foreach(lc, querytree_list)
		{
			Query	   *query = lfirst_node(Query, lc);

			if (query->commandType != CMD_UTILITY)
			{
				char	   *str = nodeToString(query);
				Query	   *new_query = stringToNodeWithLocations(str);

				/*
				 * queryId is not saved in stored rules, but we must preserve
				 * it here to avoid breaking pg_stat_statements.
				 */
				new_query->queryId = query->queryId;

				new_list = lappend(new_list, new_query);
				pfree(str);
			}
			else
				new_list = lappend(new_list, query);
		}

		/* This checks both outfuncs/readfuncs and the equal() routines... */
		if (!equal(new_list, querytree_list))
			elog(WARNING, "outfuncs/readfuncs failed to produce equal parse tree");
		else
			querytree_list = new_list;
	}
#endif

	if (Debug_print_rewritten)
		elog_node_display(LOG, "rewritten parse tree", querytree_list,
						  Debug_pretty_print);

	return querytree_list;
}


/*
 * Generate a plan for a single already-rewritten query.
 * This is a thin wrapper around planner() and takes the same parameters.
 */
PlannedStmt *
pg_plan_query(Query *querytree, const char *query_string, int cursorOptions,
			  ParamListInfo boundParams)
{
	PlannedStmt *plan;

	/* Utility commands have no plans. */
	if (querytree->commandType == CMD_UTILITY)
		return NULL;

	/* Planner must have a snapshot in case it calls user-defined functions. */
	Assert(ActiveSnapshotSet());

	TRACE_POSTGRESQL_QUERY_PLAN_START();

	if (log_planner_stats)
		ResetUsage();

	/* call the optimizer */
	plan = planner(querytree, query_string, cursorOptions, boundParams);

	if (log_planner_stats)
		ShowUsage("PLANNER STATISTICS");

#ifdef COPY_PARSE_PLAN_TREES
	/* Optional debugging check: pass plan tree through copyObject() */
	{
		PlannedStmt *new_plan = copyObject(plan);

		/*
		 * equal() currently does not have routines to compare Plan nodes, so
		 * don't try to test equality here.  Perhaps fix someday?
		 */
#ifdef NOT_USED
		/* This checks both copyObject() and the equal() routines... */
		if (!equal(new_plan, plan))
			elog(WARNING, "copyObject() failed to produce an equal plan tree");
		else
#endif
			plan = new_plan;
	}
#endif

#ifdef WRITE_READ_PARSE_PLAN_TREES
	/* Optional debugging check: pass plan tree through outfuncs/readfuncs */
	{
		char	   *str;
		PlannedStmt *new_plan;

		str = nodeToString(plan);
		new_plan = stringToNodeWithLocations(str);
		pfree(str);

		/*
		 * equal() currently does not have routines to compare Plan nodes, so
		 * don't try to test equality here.  Perhaps fix someday?
		 */
#ifdef NOT_USED
		/* This checks both outfuncs/readfuncs and the equal() routines... */
		if (!equal(new_plan, plan))
			elog(WARNING, "outfuncs/readfuncs failed to produce an equal plan tree");
		else
#endif
			plan = new_plan;
	}
#endif

	/*
	 * Print plan if debugging.
	 */
	if (Debug_print_plan)
		elog_node_display(LOG, "plan", plan, Debug_pretty_print);

	TRACE_POSTGRESQL_QUERY_PLAN_DONE();

	return plan;
}

/*
 * Generate plans for a list of already-rewritten queries.
 *
 * For normal optimizable statements, invoke the planner.  For utility
 * statements, just make a wrapper PlannedStmt node.
 *
 * The result is a list of PlannedStmt nodes.
 */
List *
pg_plan_queries(List *querytrees, const char *query_string, int cursorOptions,
				ParamListInfo boundParams)
{
	List	   *stmt_list = NIL;
	ListCell   *query_list;

	foreach(query_list, querytrees)
	{
		Query	   *query = lfirst_node(Query, query_list);
		PlannedStmt *stmt;

		if (query->commandType == CMD_UTILITY)
		{
			/* Utility commands require no planning. */
			stmt = makeNode(PlannedStmt);
			stmt->commandType = CMD_UTILITY;
			stmt->canSetTag = query->canSetTag;
			stmt->utilityStmt = query->utilityStmt;
			stmt->stmt_location = query->stmt_location;
			stmt->stmt_len = query->stmt_len;
			stmt->queryId = query->queryId;
		}
		else
		{
			stmt = pg_plan_query(query, query_string, cursorOptions,
								 boundParams);
		}

		stmt_list = lappend(stmt_list, stmt);
	}

	return stmt_list;
}


/*
 * exec_simple_query
 *
 * Execute a "simple Query" protocol message.
 */
static void
exec_simple_query(const char *query_string)
{
	CommandDest dest = whereToSendOutput;
	MemoryContext oldcontext;
	List	   *parsetree_list;
	ListCell   *parsetree_item;
	bool		save_log_statement_stats = log_statement_stats;
	bool		was_logged = false;
	bool		use_implicit_block;
	char		msec_str[32];
	const char *redacted_query_string;
	CommandTag command_tag;

	/*
	 * Report query to various monitoring facilities.
	 */
	debug_query_string = query_string;

	/* Use YbParseCommandTag to suppress error warnings. */
	command_tag = YbParseCommandTag(query_string);
	redacted_query_string = YbRedactPasswordIfExists(query_string, command_tag);
	pgstat_report_activity(STATE_RUNNING, redacted_query_string);

	TRACE_POSTGRESQL_QUERY_START(query_string);

	/*
	 * We use save_log_statement_stats so ShowUsage doesn't report incorrect
	 * results because ResetUsage wasn't called.
	 */
	if (save_log_statement_stats)
		ResetUsage();

	/*
	 * Start up a transaction command.  All queries generated by the
	 * query_string will be in this same command block, *unless* we find a
	 * BEGIN/COMMIT/ABORT statement; we have to force a new xact command after
	 * one of those, else bad things will happen in xact.c. (Note that this
	 * will normally change current memory context.)
	 */
	yb_start_xact_command_internal(yb_skip_read_committed_internal_savepoint(command_tag));

	/*
	 * Zap any pre-existing unnamed statement.  (While not strictly necessary,
	 * it seems best to define simple-Query mode as if it used the unnamed
	 * statement and portal; this ensures we recover any storage used by prior
	 * unnamed operations.)
	 */
	drop_unnamed_stmt();

	/*
	 * Switch to appropriate context for constructing parsetrees.
	 */
	oldcontext = MemoryContextSwitchTo(MessageContext);

	/*
	 * Do basic parsing of the query or queries (this should be safe even if
	 * we are in aborted transaction state!)
	 */
	parsetree_list = pg_parse_query(query_string);

	/* Log the redacted query immediately if dictated by log_statement */
	if (check_log_statement(parsetree_list))
	{
		ereport(LOG,
				(errmsg("statement: %s", redacted_query_string),
				 errhidestmt(true),
				 errdetail_execute(parsetree_list)));
		was_logged = true;
	}

	/*
	 * Switch back to transaction context to enter the loop.
	 */
	MemoryContextSwitchTo(oldcontext);

	/*
	 * For historical reasons, if multiple SQL statements are given in a
	 * single "simple Query" message, we execute them as a single transaction,
	 * unless explicit transaction control commands are included to make
	 * portions of the list be separate transactions.  To represent this
	 * behavior properly in the transaction machinery, we use an "implicit"
	 * transaction block.
	 */
	use_implicit_block = (list_length(parsetree_list) > 1);
	yb_is_multi_statement_query = use_implicit_block;

	/*
	 * Run through the raw parsetree(s) and process each one.
	 */
	foreach(parsetree_item, parsetree_list)
	{
		RawStmt    *parsetree = lfirst_node(RawStmt, parsetree_item);
		bool		snapshot_set = false;
		CommandTag	commandTag;
		QueryCompletion qc;
		MemoryContext per_parsetree_context = NULL;
		List	   *querytree_list,
				   *plantree_list;
		Portal		portal;
		DestReceiver *receiver;
		int16		format;

		pgstat_report_query_id(0, true);

		/*
		 * Get the command name for use in status display (it also becomes the
		 * default completion tag, down inside PortalRun).  Set ps_status and
		 * do any special start-of-SQL-command processing needed by the
		 * destination.
		 */
		commandTag = CreateCommandTag(parsetree->stmt);

		set_ps_display(GetCommandTagName(commandTag));

		BeginCommand(commandTag, dest);

		/*
		 * If we are in an aborted transaction, reject all commands except
		 * COMMIT/ABORT.  It is important that this test occur before we try
		 * to do parse analysis, rewrite, or planning, since all those phases
		 * try to do database accesses, which may fail in abort state. (It
		 * might be safe to allow some additional utility commands in this
		 * state, but not many...)
		 */
		if (IsAbortedTransactionBlockState() &&
			!IsTransactionExitStmt(parsetree->stmt))
			ereport(ERROR,
					(errcode(ERRCODE_IN_FAILED_SQL_TRANSACTION),
					 errmsg("current transaction is aborted, "
							"commands ignored until end of transaction block"),
					 errdetail_abort()));

		/* Make sure we are in a transaction command */
		yb_start_xact_command_internal(yb_skip_read_committed_internal_savepoint(commandTag));

		/*
		 * If using an implicit transaction block, and we're not already in a
		 * transaction block, start an implicit block to force this statement
		 * to be grouped together with any following ones.  (We must do this
		 * each time through the loop; otherwise, a COMMIT/ROLLBACK in the
		 * list would cause later statements to not be grouped.)
		 */
		if (use_implicit_block)
			BeginImplicitTransactionBlock();

		/* If we got a cancel signal in parsing or prior command, quit */
		CHECK_FOR_INTERRUPTS();

		/*
		 * Set up a snapshot if parse analysis/planning will need one.
		 */
		if (analyze_requires_snapshot(parsetree))
		{
			PushActiveSnapshot(GetTransactionSnapshot());
			snapshot_set = true;
		}

		/*
		 * OK to analyze, rewrite, and plan this query.
		 *
		 * Switch to appropriate context for constructing query and plan trees
		 * (these can't be in the transaction context, as that will get reset
		 * when the command is COMMIT/ROLLBACK).  If we have multiple
		 * parsetrees, we use a separate context for each one, so that we can
		 * free that memory before moving on to the next one.  But for the
		 * last (or only) parsetree, just use MessageContext, which will be
		 * reset shortly after completion anyway.  In event of an error, the
		 * per_parsetree_context will be deleted when MessageContext is reset.
		 */
		if (lnext(parsetree_list, parsetree_item) != NULL)
		{
			per_parsetree_context =
				AllocSetContextCreate(MessageContext,
									  "per-parsetree message context",
									  ALLOCSET_DEFAULT_SIZES);
			oldcontext = MemoryContextSwitchTo(per_parsetree_context);
		}
		else
			oldcontext = MemoryContextSwitchTo(MessageContext);

		querytree_list = pg_analyze_and_rewrite_fixedparams(parsetree, query_string,
															NULL, 0, NULL);

		plantree_list = pg_plan_queries(querytree_list, query_string,
										CURSOR_OPT_PARALLEL_OK, NULL);

		/*
		 * Done with the snapshot used for parsing/planning.
		 *
		 * While it looks promising to reuse the same snapshot for query
		 * execution (at least for simple protocol), unfortunately it causes
		 * execution to use a snapshot that has been acquired before locking
		 * any of the tables mentioned in the query.  This creates user-
		 * visible anomalies, so refrain.  Refer to
		 * https://postgr.es/m/flat/5075D8DF.6050500@fuzzy.cz for details.
		 */
		if (snapshot_set)
			PopActiveSnapshot();

		/* If we got a cancel signal in analysis or planning, quit */
		CHECK_FOR_INTERRUPTS();

		/*
		 * Create unnamed portal to run the query or queries in. If there
		 * already is one, silently drop it.
		 */
		portal = CreatePortal("", true, true);
		/* Don't display the portal in pg_cursors */
		portal->visible = false;

		/*
		 * We don't have to copy anything into the portal, because everything
		 * we are passing here is in MessageContext or the
		 * per_parsetree_context, and so will outlive the portal anyway.
		 */
		PortalDefineQuery(portal,
						  NULL,
						  query_string,
						  commandTag,
						  plantree_list,
						  NULL);

		/*
		 * Start the portal.  No parameters here.
		 */
		PortalStart(portal, NULL, 0, InvalidSnapshot);

		/*
		 * Select the appropriate output format: text unless we are doing a
		 * FETCH from a binary cursor.  (Pretty grotty to have to do this here
		 * --- but it avoids grottiness in other places.  Ah, the joys of
		 * backward compatibility...)
		 */
		format = 0;				/* TEXT is default */
		if (IsA(parsetree->stmt, FetchStmt))
		{
			FetchStmt  *stmt = (FetchStmt *) parsetree->stmt;

			if (!stmt->ismove)
			{
				Portal		fportal = GetPortalByName(stmt->portalname);

				if (PortalIsValid(fportal) &&
					(fportal->cursorOptions & CURSOR_OPT_BINARY))
					format = 1; /* BINARY */
			}
		}
		PortalSetResultFormat(portal, 1, &format);

		/*
		 * Now we can create the destination receiver object.
		 */
		receiver = CreateDestReceiver(dest);
		if (dest == DestRemote)
			SetRemoteDestReceiverParams(receiver, portal);

		/*
		 * Switch back to transaction context for execution.
		 */
		MemoryContextSwitchTo(oldcontext);

		/*
		 * Run the portal to completion, and then drop it (and the receiver).
		 */
		(void) PortalRun(portal,
						 FETCH_ALL,
						 true,	/* always top level */
						 true,
						 receiver,
						 receiver,
						 &qc);

		receiver->rDestroy(receiver);

		PortalDrop(portal, false);

		if (lnext(parsetree_list, parsetree_item) == NULL)
		{
			/*
			 * If this is the last parsetree of the query string, close down
			 * transaction statement before reporting command-complete.  This
			 * is so that any end-of-transaction errors are reported before
			 * the command-complete message is issued, to avoid confusing
			 * clients who will expect either a command-complete message or an
			 * error, not one and then the other.  Also, if we're using an
			 * implicit transaction block, we must close that out first.
			 */
			if (use_implicit_block)
				EndImplicitTransactionBlock();
			finish_xact_command();
		}
		else if (IsA(parsetree->stmt, TransactionStmt))
		{
			/*
			 * If this was a transaction control statement, commit it. We will
			 * start a new xact command for the next command.
			 */
			finish_xact_command();
		}
		else
		{
			/*
			 * We had better not see XACT_FLAGS_NEEDIMMEDIATECOMMIT set if
			 * we're not calling finish_xact_command().  (The implicit
			 * transaction block should have prevented it from getting set.)
			 */
			Assert(!(MyXactFlags & XACT_FLAGS_NEEDIMMEDIATECOMMIT));

			/*
			 * We need a CommandCounterIncrement after every query, except
			 * those that start or end a transaction block.
			 */
			CommandCounterIncrement();

			/*
			 * Disable statement timeout between queries of a multi-query
			 * string, so that the timeout applies separately to each query.
			 * (Our next loop iteration will start a fresh timeout.)
			 */
			disable_statement_timeout();
		}

		/*
		 * Tell client that we're done with this query.  Note we emit exactly
		 * one EndCommand report for each raw parsetree, thus one for each SQL
		 * command the client sent, regardless of rewriting. (But a command
		 * aborted by error will not send an EndCommand report at all.)
		 */
		EndCommand(&qc, dest, false);

		/* Now we may drop the per-parsetree context, if one was created. */
		if (per_parsetree_context)
			MemoryContextDelete(per_parsetree_context);
	}							/* end loop over parsetrees */

	/*
	 * Close down transaction statement, if one is open.  (This will only do
	 * something if the parsetree list was empty; otherwise the last loop
	 * iteration already did it.)
	 */
	finish_xact_command();

	/*
	 * If there were no parsetrees, return EmptyQueryResponse message.
	 */
	if (!parsetree_list)
		NullCommand(dest);

	/*
	 * Emit duration logging if appropriate.
	 */
	switch (check_log_duration(msec_str, was_logged))
	{
		case 1:
			ereport(LOG,
					(errmsg("duration: %s ms", msec_str),
					 errhidestmt(true)));
			break;
		case 2:
			ereport(LOG,
					(errmsg("duration: %s ms  statement: %s",
							msec_str, redacted_query_string),
					 errhidestmt(true),
					 errdetail_execute(parsetree_list)));
			break;
	}

	if (save_log_statement_stats)
		ShowUsage("QUERY STATISTICS");

	TRACE_POSTGRESQL_QUERY_DONE(query_string);

	debug_query_string = NULL;
}

/*
 * exec_parse_message
 *
 * Execute a "Parse" protocol message.
 */
static void
exec_parse_message(const char *query_string,	/* string to execute */
				   const char *stmt_name,	/* name for prepared stmt */
				   Oid *paramTypes, /* parameter types */
				   int numParams, /* number of parameters */
				   CommandDest output_dest) /* where to send output */
{
	MemoryContext unnamed_stmt_context = NULL;
	MemoryContext oldcontext;
	List	   *parsetree_list;
	RawStmt    *raw_parse_tree;
	List	   *querytree_list;
	CachedPlanSource *psrc;
	bool		is_named;
	bool		save_log_statement_stats = log_statement_stats;
	char		msec_str[32];
	const char *redacted_query_string;
	CommandTag command_tag;

	/*
	 * Report query to various monitoring facilities.
	 */
	debug_query_string = query_string;

	/* Use YbParseCommandTag to suppress error warnings. */
	command_tag = YbParseCommandTag(query_string);
	redacted_query_string = YbRedactPasswordIfExists(query_string, command_tag);
	pgstat_report_activity(STATE_RUNNING, redacted_query_string);

	set_ps_display("PARSE");

	if (save_log_statement_stats)
		ResetUsage();

	ereport(DEBUG2,
			(errmsg_internal("parse %s: %s",
					*stmt_name ? stmt_name : "<unnamed>",
					redacted_query_string)));

	/*
	 * Start up a transaction command so we can run parse analysis etc. (Note
	 * that this will normally change current memory context.) Nothing happens
	 * if we are already in one.  This also arms the statement timeout if
	 * necessary.
	 */
	yb_start_xact_command_internal(yb_skip_read_committed_internal_savepoint(command_tag));

	/*
	 * Switch to appropriate context for constructing parsetrees.
	 *
	 * We have two strategies depending on whether the prepared statement is
	 * named or not.  For a named prepared statement, we do parsing in
	 * MessageContext and copy the finished trees into the prepared
	 * statement's plancache entry; then the reset of MessageContext releases
	 * temporary space used by parsing and rewriting. For an unnamed prepared
	 * statement, we assume the statement isn't going to hang around long, so
	 * getting rid of temp space quickly is probably not worth the costs of
	 * copying parse trees.  So in this case, we create the plancache entry's
	 * query_context here, and do all the parsing work therein.
	 */
	is_named = (stmt_name[0] != '\0');
	if (is_named)
	{
		/* Named prepared statement --- parse in MessageContext */
		oldcontext = MemoryContextSwitchTo(MessageContext);
	}
	else
	{
		/* Unnamed prepared statement --- release any prior unnamed stmt */
		drop_unnamed_stmt();
		/* Create context for parsing */
		unnamed_stmt_context =
			AllocSetContextCreate(MessageContext,
								  "unnamed prepared statement",
								  ALLOCSET_DEFAULT_SIZES);
		oldcontext = MemoryContextSwitchTo(unnamed_stmt_context);
	}

	/*
	 * Do basic parsing of the query or queries (this should be safe even if
	 * we are in aborted transaction state!)
	 */
	parsetree_list = pg_parse_query(query_string);

	/*
	 * We only allow a single user statement in a prepared statement. This is
	 * mainly to keep the protocol simple --- otherwise we'd need to worry
	 * about multiple result tupdescs and things like that.
	 */
	if (list_length(parsetree_list) > 1)
		ereport(ERROR,
				(errcode(ERRCODE_SYNTAX_ERROR),
				 errmsg("cannot insert multiple commands into a prepared statement")));

	if (parsetree_list != NIL)
	{
		bool		snapshot_set = false;

		raw_parse_tree = linitial_node(RawStmt, parsetree_list);

		/*
		 * If we are in an aborted transaction, reject all commands except
		 * COMMIT/ROLLBACK.  It is important that this test occur before we
		 * try to do parse analysis, rewrite, or planning, since all those
		 * phases try to do database accesses, which may fail in abort state.
		 * (It might be safe to allow some additional utility commands in this
		 * state, but not many...)
		 */
		if (IsAbortedTransactionBlockState() &&
			!IsTransactionExitStmt(raw_parse_tree->stmt))
			ereport(ERROR,
					(errcode(ERRCODE_IN_FAILED_SQL_TRANSACTION),
					 errmsg("current transaction is aborted, "
							"commands ignored until end of transaction block"),
					 errdetail_abort()));

		/*
		 * Create the CachedPlanSource before we do parse analysis, since it
		 * needs to see the unmodified raw parse tree.
		 */
		psrc = CreateCachedPlan(raw_parse_tree, query_string,
								CreateCommandTag(raw_parse_tree->stmt));

		/*
		 * Set up a snapshot if parse analysis will need one.
		 */
		if (analyze_requires_snapshot(raw_parse_tree))
		{
			PushActiveSnapshot(GetTransactionSnapshot());
			snapshot_set = true;
		}

		/*
		 * Analyze and rewrite the query.  Note that the originally specified
		 * parameter set is not required to be complete, so we have to use
		 * pg_analyze_and_rewrite_varparams().
		 */
		querytree_list = pg_analyze_and_rewrite_varparams(raw_parse_tree,
														  query_string,
														  &paramTypes,
														  &numParams,
														  NULL);

		/* Done with the snapshot used for parsing */
		if (snapshot_set)
			PopActiveSnapshot();
	}
	else
	{
		/* Empty input string.  This is legal. */
		raw_parse_tree = NULL;
		psrc = CreateCachedPlan(raw_parse_tree, query_string,
								CMDTAG_UNKNOWN);
		querytree_list = NIL;
	}

	/*
	 * CachedPlanSource must be a direct child of MessageContext before we
	 * reparent unnamed_stmt_context under it, else we have a disconnected
	 * circular subgraph.  Klugy, but less so than flipping contexts even more
	 * above.
	 */
	if (unnamed_stmt_context)
		MemoryContextSetParent(psrc->context, MessageContext);

	/* Finish filling in the CachedPlanSource */
	CompleteCachedPlan(psrc,
					   querytree_list,
					   unnamed_stmt_context,
					   paramTypes,
					   numParams,
					   NULL,
					   NULL,
					   CURSOR_OPT_PARALLEL_OK,	/* allow parallel mode */
					   true);	/* fixed result */

	/* If we got a cancel signal during analysis, quit */
	CHECK_FOR_INTERRUPTS();

	if (is_named)
	{
		/*
		 * Store the query as a prepared statement.
		 */
		StorePreparedStatement(stmt_name, psrc, false);
	}
	else
	{
		/*
		 * We just save the CachedPlanSource into unnamed_stmt_psrc.
		 */
		SaveCachedPlan(psrc);
		unnamed_stmt_psrc = psrc;
	}

	MemoryContextSwitchTo(oldcontext);

	/*
	 * We do NOT close the open transaction command here; that only happens
	 * when the client sends Sync.  Instead, do CommandCounterIncrement just
	 * in case something happened during parse/plan.
	 */
	CommandCounterIncrement();

	/*
	 * Send ParseComplete.
	 */
	if (output_dest == DestRemote)
		pq_putemptymessage('1');

	/*
	 * Emit duration logging if appropriate.
	 */
	switch (check_log_duration(msec_str, false))
	{
		case 1:
			ereport(LOG,
					(errmsg("duration: %s ms", msec_str),
					 errhidestmt(true)));
			break;
		case 2:
			ereport(LOG,
					(errmsg("duration: %s ms  parse %s: %s",
							msec_str,
							*stmt_name ? stmt_name : "<unnamed>",
							redacted_query_string),
					 errhidestmt(true)));
			break;
	}

	if (save_log_statement_stats)
		ShowUsage("PARSE MESSAGE STATISTICS");

	debug_query_string = NULL;
}

/*
 * exec_bind_message
 *
 * Process a "Bind" message to create a portal from a prepared statement
 */
static void
exec_bind_message(StringInfo input_message)
{
	const char *portal_name;
	const char *stmt_name;
	int			numPFormats;
	int16	   *pformats = NULL;
	int			numParams;
	int			numRFormats;
	int16	   *rformats = NULL;
	CachedPlanSource *psrc;
	CachedPlan *cplan;
	Portal		portal;
	char	   *query_string;
	const char *redacted_query_string;
	char	   *saved_stmt_name;
	ParamListInfo params;
	MemoryContext oldContext;
	bool		save_log_statement_stats = log_statement_stats;
	bool		snapshot_set = false;
	char		msec_str[32];
	ParamsErrorCbData params_data;
	ErrorContextCallback params_errcxt;
	CommandTag command_tag;

	/* Get the fixed part of the message */
	portal_name = pq_getmsgstring(input_message);
	stmt_name = pq_getmsgstring(input_message);

	ereport(DEBUG2,
			(errmsg_internal("bind %s to %s",
							 *portal_name ? portal_name : "<unnamed>",
							 *stmt_name ? stmt_name : "<unnamed>")));

	/* Find prepared statement */
	if (stmt_name[0] != '\0')
	{
		PreparedStatement *pstmt;

		pstmt = FetchPreparedStatement(stmt_name, true);
		psrc = pstmt->plansource;
	}
	else
	{
		/* special-case the unnamed statement */
		psrc = unnamed_stmt_psrc;
		if (!psrc)
			ereport(ERROR,
					(errcode(ERRCODE_UNDEFINED_PSTATEMENT),
					 errmsg("unnamed prepared statement does not exist")));
	}

	/*
	 * Report query to various monitoring facilities.
	 */
	debug_query_string = psrc->query_string;

	/* Use YbParseCommandTag to suppress error warnings. */
	command_tag = YbParseCommandTag(psrc->query_string);
	redacted_query_string = YbRedactPasswordIfExists(psrc->query_string, command_tag);
	pgstat_report_activity(STATE_RUNNING, redacted_query_string);

	set_ps_display("BIND");

	if (save_log_statement_stats)
		ResetUsage();

	/*
	 * Start up a transaction command so we can call functions etc. (Note that
	 * this will normally change current memory context.) Nothing happens if
	 * we are already in one.  This also arms the statement timeout if
	 * necessary.
	 */
	yb_start_xact_command_internal(yb_skip_read_committed_internal_savepoint(command_tag));

	/* Switch back to message context */
	MemoryContextSwitchTo(MessageContext);

	/* Get the parameter format codes */
	numPFormats = pq_getmsgint(input_message, 2);
	if (numPFormats > 0)
	{
		pformats = (int16 *) palloc(numPFormats * sizeof(int16));
		for (int i = 0; i < numPFormats; i++)
			pformats[i] = pq_getmsgint(input_message, 2);
	}

	/* Get the parameter value count */
	numParams = pq_getmsgint(input_message, 2);

	if (numPFormats > 1 && numPFormats != numParams)
		ereport(ERROR,
				(errcode(ERRCODE_PROTOCOL_VIOLATION),
				 errmsg("bind message has %d parameter formats but %d parameters",
						numPFormats, numParams)));

	if (numParams != psrc->num_params)
		ereport(ERROR,
				(errcode(ERRCODE_PROTOCOL_VIOLATION),
				 errmsg("bind message supplies %d parameters, but prepared statement \"%s\" requires %d",
						numParams, stmt_name, psrc->num_params)));

	/*
	 * If we are in aborted transaction state, the only portals we can
	 * actually run are those containing COMMIT or ROLLBACK commands. We
	 * disallow binding anything else to avoid problems with infrastructure
	 * that expects to run inside a valid transaction.  We also disallow
	 * binding any parameters, since we can't risk calling user-defined I/O
	 * functions.
	 */
	if (IsAbortedTransactionBlockState() &&
		(!(psrc->raw_parse_tree &&
		   IsTransactionExitStmt(psrc->raw_parse_tree->stmt)) ||
		 numParams != 0))
		ereport(ERROR,
				(errcode(ERRCODE_IN_FAILED_SQL_TRANSACTION),
				 errmsg("current transaction is aborted, "
						"commands ignored until end of transaction block"),
				 errdetail_abort()));

	/*
	 * Create the portal.  Allow silent replacement of an existing portal only
	 * if the unnamed portal is specified.
	 */
	if (portal_name[0] == '\0')
		portal = CreatePortal(portal_name, true, true);
	else
		portal = CreatePortal(portal_name, false, false);

	/*
	 * Prepare to copy stuff into the portal's memory context.  We do all this
	 * copying first, because it could possibly fail (out-of-memory) and we
	 * don't want a failure to occur between GetCachedPlan and
	 * PortalDefineQuery; that would result in leaking our plancache refcount.
	 */
	oldContext = MemoryContextSwitchTo(portal->portalContext);

	/* Copy the plan's query string into the portal */
	query_string = pstrdup(psrc->query_string);

	/* Likewise make a copy of the statement name, unless it's unnamed */
	if (stmt_name[0])
		saved_stmt_name = pstrdup(stmt_name);
	else
		saved_stmt_name = NULL;

	/*
	 * Set a snapshot if we have parameters to fetch (since the input
	 * functions might need it) or the query isn't a utility command (and
	 * hence could require redoing parse analysis and planning).  We keep the
	 * snapshot active till we're done, so that plancache.c doesn't have to
	 * take new ones.
	 */
	if (numParams > 0 ||
		(psrc->raw_parse_tree &&
		 analyze_requires_snapshot(psrc->raw_parse_tree)))
	{
		PushActiveSnapshot(GetTransactionSnapshot());
		snapshot_set = true;
	}

	/*
	 * Fetch parameters, if any, and store in the portal's memory context.
	 */
	if (numParams > 0)
	{
		char	  **knownTextValues = NULL; /* allocate on first use */
		BindParamCbData one_param_data;

		/*
		 * Set up an error callback so that if there's an error in this phase,
		 * we can report the specific parameter causing the problem.
		 */
		one_param_data.portalName = portal->name;
		one_param_data.paramno = -1;
		one_param_data.paramval = NULL;
		params_errcxt.previous = error_context_stack;
		params_errcxt.callback = bind_param_error_callback;
		params_errcxt.arg = (void *) &one_param_data;
		error_context_stack = &params_errcxt;

		params = makeParamList(numParams);

		for (int paramno = 0; paramno < numParams; paramno++)
		{
			Oid			ptype = psrc->param_types[paramno];
			int32		plength;
			Datum		pval;
			bool		isNull;
			StringInfoData pbuf;
			char		csave;
			int16		pformat;

			one_param_data.paramno = paramno;
			one_param_data.paramval = NULL;

			plength = pq_getmsgint(input_message, 4);
			isNull = (plength == -1);

			if (!isNull)
			{
				const char *pvalue = pq_getmsgbytes(input_message, plength);

				/*
				 * Rather than copying data around, we just set up a phony
				 * StringInfo pointing to the correct portion of the message
				 * buffer.  We assume we can scribble on the message buffer so
				 * as to maintain the convention that StringInfos have a
				 * trailing null.  This is grotty but is a big win when
				 * dealing with very large parameter strings.
				 */
				pbuf.data = unconstify(char *, pvalue);
				pbuf.maxlen = plength + 1;
				pbuf.len = plength;
				pbuf.cursor = 0;

				csave = pbuf.data[plength];
				pbuf.data[plength] = '\0';
			}
			else
			{
				pbuf.data = NULL;	/* keep compiler quiet */
				csave = 0;
			}

			if (numPFormats > 1)
				pformat = pformats[paramno];
			else if (numPFormats > 0)
				pformat = pformats[0];
			else
				pformat = 0;	/* default = text */

			if (pformat == 0)	/* text mode */
			{
				Oid			typinput;
				Oid			typioparam;
				char	   *pstring;

				getTypeInputInfo(ptype, &typinput, &typioparam);

				/*
				 * We have to do encoding conversion before calling the
				 * typinput routine.
				 */
				if (isNull)
					pstring = NULL;
				else
					pstring = pg_client_to_server(pbuf.data, plength);

				/* Now we can log the input string in case of error */
				one_param_data.paramval = pstring;

				pval = OidInputFunctionCall(typinput, pstring, typioparam, -1);

				one_param_data.paramval = NULL;

				/*
				 * If we might need to log parameters later, save a copy of
				 * the converted string in MessageContext; then free the
				 * result of encoding conversion, if any was done.
				 */
				if (pstring)
				{
					if (log_parameter_max_length_on_error != 0)
					{
						MemoryContext oldcxt;

						oldcxt = MemoryContextSwitchTo(MessageContext);

						if (knownTextValues == NULL)
							knownTextValues =
								palloc0(numParams * sizeof(char *));

						if (log_parameter_max_length_on_error < 0)
							knownTextValues[paramno] = pstrdup(pstring);
						else
						{
							/*
							 * We can trim the saved string, knowing that we
							 * won't print all of it.  But we must copy at
							 * least two more full characters than
							 * BuildParamLogString wants to use; otherwise it
							 * might fail to include the trailing ellipsis.
							 */
							knownTextValues[paramno] =
								pnstrdup(pstring,
										 log_parameter_max_length_on_error
										 + 2 * MAX_MULTIBYTE_CHAR_LEN);
						}

						MemoryContextSwitchTo(oldcxt);
					}
					if (pstring != pbuf.data)
						pfree(pstring);
				}
			}
			else if (pformat == 1)	/* binary mode */
			{
				Oid			typreceive;
				Oid			typioparam;
				StringInfo	bufptr;

				/*
				 * Call the parameter type's binary input converter
				 */
				getTypeBinaryInputInfo(ptype, &typreceive, &typioparam);

				if (isNull)
					bufptr = NULL;
				else
					bufptr = &pbuf;

				pval = OidReceiveFunctionCall(typreceive, bufptr, typioparam, -1);

				/* Trouble if it didn't eat the whole buffer */
				if (!isNull && pbuf.cursor != pbuf.len)
					ereport(ERROR,
							(errcode(ERRCODE_INVALID_BINARY_REPRESENTATION),
							 errmsg("incorrect binary data format in bind parameter %d",
									paramno + 1)));
			}
			else
			{
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("unsupported format code: %d",
								pformat)));
				pval = 0;		/* keep compiler quiet */
			}

			/* Restore message buffer contents */
			if (!isNull)
				pbuf.data[plength] = csave;

			params->params[paramno].value = pval;
			params->params[paramno].isnull = isNull;

			/*
			 * We mark the params as CONST.  This ensures that any custom plan
			 * makes full use of the parameter values.
			 */
			params->params[paramno].pflags = PARAM_FLAG_CONST;
			params->params[paramno].ptype = ptype;
		}

		/* Pop the per-parameter error callback */
		error_context_stack = error_context_stack->previous;

		/*
		 * Once all parameters have been received, prepare for printing them
		 * in future errors, if configured to do so.  (This is saved in the
		 * portal, so that they'll appear when the query is executed later.)
		 */
		if (log_parameter_max_length_on_error != 0)
			params->paramValuesStr =
				BuildParamLogString(params,
									knownTextValues,
									log_parameter_max_length_on_error);
	}
	else
		params = NULL;

	/* Done storing stuff in portal's context */
	MemoryContextSwitchTo(oldContext);

	/*
	 * Set up another error callback so that all the parameters are logged if
	 * we get an error during the rest of the BIND processing.
	 */
	params_data.portalName = portal->name;
	params_data.params = params;
	params_errcxt.previous = error_context_stack;
	params_errcxt.callback = ParamsErrorCallback;
	params_errcxt.arg = (void *) &params_data;
	error_context_stack = &params_errcxt;

	/* Get the result format codes */
	numRFormats = pq_getmsgint(input_message, 2);
	if (numRFormats > 0)
	{
		rformats = (int16 *) palloc(numRFormats * sizeof(int16));
		for (int i = 0; i < numRFormats; i++)
			rformats[i] = pq_getmsgint(input_message, 2);
	}

	pq_getmsgend(input_message);

	/*
	 * Obtain a plan from the CachedPlanSource.  Any cruft from (re)planning
	 * will be generated in MessageContext.  The plan refcount will be
	 * assigned to the Portal, so it will be released at portal destruction.
	 */
	cplan = GetCachedPlan(psrc, params, NULL, NULL);

	/*
	 * Now we can define the portal.
	 *
	 * DO NOT put any code that could possibly throw an error between the
	 * above GetCachedPlan call and here.
	 */
	PortalDefineQuery(portal,
					  saved_stmt_name,
					  query_string,
					  psrc->commandTag,
					  cplan->stmt_list,
					  cplan);

	/* Done with the snapshot used for parameter I/O and parsing/planning */
	if (snapshot_set)
		PopActiveSnapshot();

	/*
	 * And we're ready to start portal execution.
	 */
	PortalStart(portal, params, 0, InvalidSnapshot);

	/*
	 * Apply the result format requests to the portal.
	 */
	PortalSetResultFormat(portal, numRFormats, rformats);

	/*
	 * Done binding; remove the parameters error callback.  Entries emitted
	 * later determine independently whether to log the parameters or not.
	 */
	error_context_stack = error_context_stack->previous;

	/*
	 * Send BindComplete.
	 */
	if (whereToSendOutput == DestRemote)
		pq_putemptymessage('2');

	/*
	 * Emit duration logging if appropriate.
	 */
	switch (check_log_duration(msec_str, false))
	{
		case 1:
			ereport(LOG,
					(errmsg("duration: %s ms", msec_str),
					 errhidestmt(true)));
			break;
		case 2:
			ereport(LOG,
					(errmsg("duration: %s ms  bind %s%s%s: %s",
							msec_str,
							*stmt_name ? stmt_name : "<unnamed>",
							*portal_name ? "/" : "",
							*portal_name ? portal_name : "",
							redacted_query_string),
					 errhidestmt(true),
					 errdetail_params(params)));
			break;
	}

	if (save_log_statement_stats)
		ShowUsage("BIND MESSAGE STATISTICS");

	debug_query_string = NULL;
}

/*
 * exec_execute_message
 *
 * Process an "Execute" message for a portal
 */
static void
exec_execute_message(const char *portal_name, long max_rows)
{
	CommandDest dest;
	DestReceiver *receiver;
	Portal		portal;
	bool		completed;
	QueryCompletion qc;
	const char *sourceText;
	const char *prepStmtName;
	ParamListInfo portalParams;
	bool		save_log_statement_stats = log_statement_stats;
	bool		is_xact_command;
	bool		execute_is_fetch;
	bool		was_logged = false;
	char		msec_str[32];
	ParamsErrorCbData params_data;
	ErrorContextCallback params_errcxt;

	/* Adjust destination to tell printtup.c what to do */
	dest = whereToSendOutput;
	if (dest == DestRemote)
		dest = DestRemoteExecute;

	portal = GetPortalByName(portal_name);
	if (!PortalIsValid(portal))
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_CURSOR),
				 errmsg("portal \"%s\" does not exist", portal_name)));

	/*
	 * If the original query was a null string, just return
	 * EmptyQueryResponse.
	 */
	if (portal->commandTag == CMDTAG_UNKNOWN)
	{
		Assert(portal->stmts == NIL);
		NullCommand(dest);
		return;
	}

	/* Does the portal contain a transaction command? */
	is_xact_command = IsTransactionStmtList(portal->stmts);

	/*
	 * We must copy the sourceText and prepStmtName into MessageContext in
	 * case the portal is destroyed during finish_xact_command.  We do not
	 * make a copy of the portalParams though, preferring to just not print
	 * them in that case.
	 */
	sourceText = pstrdup(portal->sourceText);
	if (portal->prepStmtName)
		prepStmtName = pstrdup(portal->prepStmtName);
	else
		prepStmtName = "<unnamed>";
	portalParams = portal->portalParams;

	/*
	 * Report query to various monitoring facilities.
	 */
	debug_query_string = sourceText;

	pgstat_report_activity(STATE_RUNNING, sourceText);

	set_ps_display(GetCommandTagName(portal->commandTag));

	if (save_log_statement_stats)
		ResetUsage();

	BeginCommand(portal->commandTag, dest);

	/*
	 * Create dest receiver in MessageContext (we don't want it in transaction
	 * context, because that may get deleted if portal contains VACUUM).
	 */
	receiver = CreateDestReceiver(dest);
	if (dest == DestRemoteExecute)
		SetRemoteDestReceiverParams(receiver, portal);

	/*
	 * Ensure we are in a transaction command (this should normally be the
	 * case already due to prior BIND).
	 */
	yb_start_xact_command_internal(yb_skip_read_committed_internal_savepoint(portal->commandTag));

	/*
	 * If we re-issue an Execute protocol request against an existing portal,
	 * then we are only fetching more rows rather than completely re-executing
	 * the query from the start. atStart is never reset for a v3 portal, so we
	 * are safe to use this check.
	 */
	execute_is_fetch = !portal->atStart;

	/* Log immediately if dictated by log_statement */
	if (check_log_statement(portal->stmts))
	{
		ereport(LOG,
				(errmsg("%s %s%s%s: %s",
						execute_is_fetch ?
						_("execute fetch from") :
						_("execute"),
						prepStmtName,
						*portal_name ? "/" : "",
						*portal_name ? portal_name : "",
						sourceText),
				 errhidestmt(true),
				 errdetail_params(portalParams)));
		was_logged = true;
	}

	/*
	 * If we are in aborted transaction state, the only portals we can
	 * actually run are those containing COMMIT or ROLLBACK commands.
	 */
	if (IsAbortedTransactionBlockState() &&
		!IsTransactionExitStmtList(portal->stmts))
		ereport(ERROR,
				(errcode(ERRCODE_IN_FAILED_SQL_TRANSACTION),
				 errmsg("current transaction is aborted, "
						"commands ignored until end of transaction block"),
				 errdetail_abort()));

	/* Check for cancel signal before we start execution */
	CHECK_FOR_INTERRUPTS();

	/*
	 * Okay to run the portal.  Set the error callback so that parameters are
	 * logged.  The parameters must have been saved during the bind phase.
	 */
	params_data.portalName = portal->name;
	params_data.params = portalParams;
	params_errcxt.previous = error_context_stack;
	params_errcxt.callback = ParamsErrorCallback;
	params_errcxt.arg = (void *) &params_data;
	error_context_stack = &params_errcxt;

	if (max_rows <= 0)
		max_rows = FETCH_ALL;

	completed = PortalRun(portal,
						  max_rows,
						  true, /* always top level */
						  !execute_is_fetch && max_rows == FETCH_ALL,
						  receiver,
						  receiver,
						  &qc);

	receiver->rDestroy(receiver);

	/* Done executing; remove the params error callback */
	error_context_stack = error_context_stack->previous;

	if (completed)
	{
		if (is_xact_command || (MyXactFlags & XACT_FLAGS_NEEDIMMEDIATECOMMIT))
		{
			/*
			 * If this was a transaction control statement, commit it.  We
			 * will start a new xact command for the next command (if any).
			 * Likewise if the statement required immediate commit.  Without
			 * this provision, we wouldn't force commit until Sync is
			 * received, which creates a hazard if the client tries to
			 * pipeline immediate-commit statements.
			 */
			finish_xact_command();

			/*
			 * These commands typically don't have any parameters, and even if
			 * one did we couldn't print them now because the storage went
			 * away during finish_xact_command.  So pretend there were none.
			 */
			portalParams = NULL;
		}
		else
		{
			/*
			 * We need a CommandCounterIncrement after every query, except
			 * those that start or end a transaction block.
			 */
			CommandCounterIncrement();

			/*
			 * Set XACT_FLAGS_PIPELINING whenever we complete an Execute
			 * message without immediately committing the transaction.
			 */
			MyXactFlags |= XACT_FLAGS_PIPELINING;

			/*
			 * Disable statement timeout whenever we complete an Execute
			 * message.  The next protocol message will start a fresh timeout.
			 */
			disable_statement_timeout();
		}

		/* Send appropriate CommandComplete to client */
		EndCommand(&qc, dest, false);
	}
	else
	{
		/* Portal run not complete, so send PortalSuspended */
		if (whereToSendOutput == DestRemote)
			pq_putemptymessage('s');

		/*
		 * Set XACT_FLAGS_PIPELINING whenever we suspend an Execute message,
		 * too.
		 */
		MyXactFlags |= XACT_FLAGS_PIPELINING;
	}

	/*
	 * Emit duration logging if appropriate.
	 */
	switch (check_log_duration(msec_str, was_logged))
	{
		case 1:
			ereport(LOG,
					(errmsg("duration: %s ms", msec_str),
					 errhidestmt(true)));
			break;
		case 2:
			ereport(LOG,
					(errmsg("duration: %s ms  %s %s%s%s: %s",
							msec_str,
							execute_is_fetch ?
							_("execute fetch from") :
							_("execute"),
							prepStmtName,
							*portal_name ? "/" : "",
							*portal_name ? portal_name : "",
							sourceText),
					 errhidestmt(true),
					 errdetail_params(portalParams)));
			break;
	}

	if (save_log_statement_stats)
		ShowUsage("EXECUTE MESSAGE STATISTICS");

	debug_query_string = NULL;
}

/*
 * check_log_statement
 *		Determine whether command should be logged because of log_statement
 *
 * stmt_list can be either raw grammar output or a list of planned
 * statements
 */
static bool
check_log_statement(List *stmt_list)
{
	ListCell   *stmt_item;

	if (log_statement == LOGSTMT_NONE)
		return false;
	if (log_statement == LOGSTMT_ALL)
		return true;

	/* Else we have to inspect the statement(s) to see whether to log */
	foreach(stmt_item, stmt_list)
	{
		Node	   *stmt = (Node *) lfirst(stmt_item);

		if (GetCommandLogLevel(stmt) <= log_statement)
			return true;
	}

	return false;
}

/*
 * check_log_duration
 *		Determine whether current command's duration should be logged
 *		We also check if this statement in this transaction must be logged
 *		(regardless of its duration).
 *
 * Returns:
 *		0 if no logging is needed
 *		1 if just the duration should be logged
 *		2 if duration and query details should be logged
 *
 * If logging is needed, the duration in msec is formatted into msec_str[],
 * which must be a 32-byte buffer.
 *
 * was_logged should be true if caller already logged query details (this
 * essentially prevents 2 from being returned).
 */
int
check_log_duration(char *msec_str, bool was_logged)
{
	if (log_duration || log_min_duration_sample >= 0 ||
		log_min_duration_statement >= 0 || xact_is_sampled)
	{
		long		secs;
		int			usecs;
		int			msecs;
		bool		exceeded_duration;
		bool		exceeded_sample_duration;
		bool		in_sample = false;

		TimestampDifference(GetCurrentStatementStartTimestamp(),
							GetCurrentTimestamp(),
							&secs, &usecs);
		msecs = usecs / 1000;

		/*
		 * This odd-looking test for log_min_duration_* being exceeded is
		 * designed to avoid integer overflow with very long durations: don't
		 * compute secs * 1000 until we've verified it will fit in int.
		 */
		exceeded_duration = (log_min_duration_statement == 0 ||
							 (log_min_duration_statement > 0 &&
							  (secs > log_min_duration_statement / 1000 ||
							   secs * 1000 + msecs >= log_min_duration_statement)));

		exceeded_sample_duration = (log_min_duration_sample == 0 ||
									(log_min_duration_sample > 0 &&
									 (secs > log_min_duration_sample / 1000 ||
									  secs * 1000 + msecs >= log_min_duration_sample)));

		/*
		 * Do not log if log_statement_sample_rate = 0. Log a sample if
		 * log_statement_sample_rate <= 1 and avoid unnecessary PRNG call if
		 * log_statement_sample_rate = 1.
		 */
		if (exceeded_sample_duration)
			in_sample = log_statement_sample_rate != 0 &&
				(log_statement_sample_rate == 1 ||
				 pg_prng_double(&pg_global_prng_state) <= log_statement_sample_rate);

		if (exceeded_duration || in_sample || log_duration || xact_is_sampled)
		{
			snprintf(msec_str, 32, "%ld.%03d",
					 secs * 1000 + msecs, usecs % 1000);
			if ((exceeded_duration || in_sample || xact_is_sampled) && !was_logged)
				return 2;
			else
				return 1;
		}
	}

	return 0;
}

/*
 * errdetail_execute
 *
 * Add an errdetail() line showing the query referenced by an EXECUTE, if any.
 * The argument is the raw parsetree list.
 */
static int
errdetail_execute(List *raw_parsetree_list)
{
	ListCell   *parsetree_item;

	foreach(parsetree_item, raw_parsetree_list)
	{
		RawStmt    *parsetree = lfirst_node(RawStmt, parsetree_item);

		if (IsA(parsetree->stmt, ExecuteStmt))
		{
			ExecuteStmt *stmt = (ExecuteStmt *) parsetree->stmt;
			PreparedStatement *pstmt;

			pstmt = FetchPreparedStatement(stmt->name, false);
			if (pstmt)
			{
				errdetail("prepare: %s", pstmt->plansource->query_string);
				return 0;
			}
		}
	}

	return 0;
}

/*
 * errdetail_params
 *
 * Add an errdetail() line showing bind-parameter data, if available.
 * Note that this is only used for statement logging, so it is controlled
 * by log_parameter_max_length not log_parameter_max_length_on_error.
 */
static int
errdetail_params(ParamListInfo params)
{
	if (params && params->numParams > 0 && log_parameter_max_length != 0)
	{
		char	   *str;

		str = BuildParamLogString(params, NULL, log_parameter_max_length);
		if (str && str[0] != '\0')
			errdetail("parameters: %s", str);
	}

	return 0;
}

/*
 * errdetail_abort
 *
 * Add an errdetail() line showing abort reason, if any.
 */
static int
errdetail_abort(void)
{
	if (MyProc->recoveryConflictPending)
		errdetail("abort reason: recovery conflict");

	return 0;
}

/*
 * errdetail_recovery_conflict
 *
 * Add an errdetail() line showing conflict source.
 */
static int
errdetail_recovery_conflict(void)
{
	switch (RecoveryConflictReason)
	{
		case PROCSIG_RECOVERY_CONFLICT_BUFFERPIN:
			errdetail("User was holding shared buffer pin for too long.");
			break;
		case PROCSIG_RECOVERY_CONFLICT_LOCK:
			errdetail("User was holding a relation lock for too long.");
			break;
		case PROCSIG_RECOVERY_CONFLICT_TABLESPACE:
			errdetail("User was or might have been using tablespace that must be dropped.");
			break;
		case PROCSIG_RECOVERY_CONFLICT_SNAPSHOT:
			errdetail("User query might have needed to see row versions that must be removed.");
			break;
		case PROCSIG_RECOVERY_CONFLICT_STARTUP_DEADLOCK:
			errdetail("User transaction caused buffer deadlock with recovery.");
			break;
		case PROCSIG_RECOVERY_CONFLICT_DATABASE:
			errdetail("User was connected to a database that must be dropped.");
			break;
		default:
			break;
			/* no errdetail */
	}

	return 0;
}

/*
 * bind_param_error_callback
 *
 * Error context callback used while parsing parameters in a Bind message
 */
static void
bind_param_error_callback(void *arg)
{
	BindParamCbData *data = (BindParamCbData *) arg;
	StringInfoData buf;
	char	   *quotedval;

	if (data->paramno < 0)
		return;

	/* If we have a textual value, quote it, and trim if necessary */
	if (data->paramval)
	{
		initStringInfo(&buf);
		appendStringInfoStringQuoted(&buf, data->paramval,
									 log_parameter_max_length_on_error);
		quotedval = buf.data;
	}
	else
		quotedval = NULL;

	if (data->portalName && data->portalName[0] != '\0')
	{
		if (quotedval)
			errcontext("portal \"%s\" parameter $%d = %s",
					   data->portalName, data->paramno + 1, quotedval);
		else
			errcontext("portal \"%s\" parameter $%d",
					   data->portalName, data->paramno + 1);
	}
	else
	{
		if (quotedval)
			errcontext("unnamed portal parameter $%d = %s",
					   data->paramno + 1, quotedval);
		else
			errcontext("unnamed portal parameter $%d",
					   data->paramno + 1);
	}

	if (quotedval)
		pfree(quotedval);
}

/*
 * exec_describe_statement_message
 *
 * Process a "Describe" message for a prepared statement
 */
static void
exec_describe_statement_message(const char *stmt_name)
{
	CachedPlanSource *psrc;

	/*
	 * Start up a transaction command. (Note that this will normally change
	 * current memory context.) Nothing happens if we are already in one.
	 */
	yb_start_xact_command_internal(true /* yb_skip_read_committed_internal_savepoint */);

	/* Switch back to message context */
	MemoryContextSwitchTo(MessageContext);

	/* Find prepared statement */
	if (stmt_name[0] != '\0')
	{
		PreparedStatement *pstmt;

		pstmt = FetchPreparedStatement(stmt_name, true);
		psrc = pstmt->plansource;
	}
	else
	{
		/* special-case the unnamed statement */
		psrc = unnamed_stmt_psrc;
		if (!psrc)
			ereport(ERROR,
					(errcode(ERRCODE_UNDEFINED_PSTATEMENT),
					 errmsg("unnamed prepared statement does not exist")));
	}

	/* Prepared statements shouldn't have changeable result descs */
	Assert(psrc->fixed_result);

	/*
	 * If we are in aborted transaction state, we can't run
	 * SendRowDescriptionMessage(), because that needs catalog accesses.
	 * Hence, refuse to Describe statements that return data.  (We shouldn't
	 * just refuse all Describes, since that might break the ability of some
	 * clients to issue COMMIT or ROLLBACK commands, if they use code that
	 * blindly Describes whatever it does.)  We can Describe parameters
	 * without doing anything dangerous, so we don't restrict that.
	 */
	if (IsAbortedTransactionBlockState() &&
		psrc->resultDesc)
		ereport(ERROR,
				(errcode(ERRCODE_IN_FAILED_SQL_TRANSACTION),
				 errmsg("current transaction is aborted, "
						"commands ignored until end of transaction block"),
				 errdetail_abort()));

	if (whereToSendOutput != DestRemote)
		return;					/* can't actually do anything... */

	/*
	 * First describe the parameters...
	 */
	pq_beginmessage_reuse(&row_description_buf, 't');	/* parameter description
														 * message type */
	pq_sendint16(&row_description_buf, psrc->num_params);

	for (int i = 0; i < psrc->num_params; i++)
	{
		Oid			ptype = psrc->param_types[i];

		pq_sendint32(&row_description_buf, (int) ptype);
	}
	pq_endmessage_reuse(&row_description_buf);

	/*
	 * Next send RowDescription or NoData to describe the result...
	 */
	if (psrc->resultDesc)
	{
		List	   *tlist;

		/* Get the plan's primary targetlist */
		tlist = CachedPlanGetTargetList(psrc, NULL);

		SendRowDescriptionMessage(&row_description_buf,
								  psrc->resultDesc,
								  tlist,
								  NULL);
	}
	else
		pq_putemptymessage('n');	/* NoData */
}

/*
 * exec_describe_portal_message
 *
 * Process a "Describe" message for a portal
 */
static void
exec_describe_portal_message(const char *portal_name)
{
	Portal		portal;

	/*
	 * Start up a transaction command. (Note that this will normally change
	 * current memory context.) Nothing happens if we are already in one.
	 */
	yb_start_xact_command_internal(true /* yb_skip_read_committed_internal_savepoint */);

	/* Switch back to message context */
	MemoryContextSwitchTo(MessageContext);

	portal = GetPortalByName(portal_name);
	if (!PortalIsValid(portal))
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_CURSOR),
				 errmsg("portal \"%s\" does not exist", portal_name)));

	/*
	 * If we are in aborted transaction state, we can't run
	 * SendRowDescriptionMessage(), because that needs catalog accesses.
	 * Hence, refuse to Describe portals that return data.  (We shouldn't just
	 * refuse all Describes, since that might break the ability of some
	 * clients to issue COMMIT or ROLLBACK commands, if they use code that
	 * blindly Describes whatever it does.)
	 */
	if (IsAbortedTransactionBlockState() &&
		portal->tupDesc)
		ereport(ERROR,
				(errcode(ERRCODE_IN_FAILED_SQL_TRANSACTION),
				 errmsg("current transaction is aborted, "
						"commands ignored until end of transaction block"),
				 errdetail_abort()));

	if (whereToSendOutput != DestRemote)
		return;					/* can't actually do anything... */

	if (portal->tupDesc)
		SendRowDescriptionMessage(&row_description_buf,
								  portal->tupDesc,
								  FetchPortalTargetList(portal),
								  portal->formats);
	else
		pq_putemptymessage('n');	/* NoData */
}


/*
 * Convenience routines for starting/committing a single command.
 */
static void
start_xact_command(void)
{
	yb_start_xact_command_internal(false /* yb_skip_read_committed_internal_savepoint */);
}

static void
yb_start_xact_command_internal(bool yb_skip_read_committed_internal_savepoint)
{
	if (!xact_started)
	{
		YBStartTransactionCommandInternal(yb_skip_read_committed_internal_savepoint);

		xact_started = true;
	}

	/*
	 * Start statement timeout if necessary.  Note that this'll intentionally
	 * not reset the clock on an already started timeout, to avoid the timing
	 * overhead when start_xact_command() is invoked repeatedly, without an
	 * interceding finish_xact_command() (e.g. parse/bind/execute).  If that's
	 * not desired, the timeout has to be disabled explicitly.
	 */
	enable_statement_timeout();

	/* Start timeout for checking if the client has gone away if necessary. */
	if (client_connection_check_interval > 0 &&
		IsUnderPostmaster &&
		MyProcPort &&
		!get_timeout_active(CLIENT_CONNECTION_CHECK_TIMEOUT))
		enable_timeout_after(CLIENT_CONNECTION_CHECK_TIMEOUT,
							 client_connection_check_interval);
}

static void
finish_xact_command(void)
{
	/* cancel active statement timeout after each command */
	disable_statement_timeout();

	if (xact_started)
	{
		CommitTransactionCommand();

#ifdef MEMORY_CONTEXT_CHECKING
		/* Check all memory contexts that weren't freed during commit */
		/* (those that were, were checked before being deleted) */
		MemoryContextCheck(TopMemoryContext);
#endif

#ifdef SHOW_MEMORY_STATS
		/* Print mem stats after each commit for leak tracking */
		MemoryContextStats(TopMemoryContext);
#endif

		xact_started = false;
	}
}


/*
 * Convenience routines for checking whether a statement is one of the
 * ones that we allow in transaction-aborted state.
 */

/* Test a bare parsetree */
static bool
IsTransactionExitStmt(Node *parsetree)
{
	if (parsetree && IsA(parsetree, TransactionStmt))
	{
		TransactionStmt *stmt = (TransactionStmt *) parsetree;

		if (stmt->kind == TRANS_STMT_COMMIT ||
			stmt->kind == TRANS_STMT_PREPARE ||
			stmt->kind == TRANS_STMT_ROLLBACK ||
			stmt->kind == TRANS_STMT_ROLLBACK_TO)
			return true;
	}
	return false;
}

/* Test a list that contains PlannedStmt nodes */
static bool
IsTransactionExitStmtList(List *pstmts)
{
	if (list_length(pstmts) == 1)
	{
		PlannedStmt *pstmt = linitial_node(PlannedStmt, pstmts);

		if (pstmt->commandType == CMD_UTILITY &&
			IsTransactionExitStmt(pstmt->utilityStmt))
			return true;
	}
	return false;
}

/* Test a list that contains PlannedStmt nodes */
static bool
IsTransactionStmtList(List *pstmts)
{
	if (list_length(pstmts) == 1)
	{
		PlannedStmt *pstmt = linitial_node(PlannedStmt, pstmts);

		if (pstmt->commandType == CMD_UTILITY &&
			IsA(pstmt->utilityStmt, TransactionStmt))
			return true;
	}
	return false;
}

/* Release any existing unnamed prepared statement */
static void
drop_unnamed_stmt(void)
{
	/* paranoia to avoid a dangling pointer in case of error */
	if (unnamed_stmt_psrc)
	{
		CachedPlanSource *psrc = unnamed_stmt_psrc;

		unnamed_stmt_psrc = NULL;
		DropCachedPlan(psrc);
	}
}


/* --------------------------------
 *		signal handler routines used in PostgresMain()
 * --------------------------------
 */

/*
 * quickdie() occurs when signaled SIGQUIT by the postmaster.
 *
 * Either some backend has bought the farm, or we've been told to shut down
 * "immediately"; so we need to stop what we're doing and exit.
 */
void
quickdie(SIGNAL_ARGS)
{
	sigaddset(&BlockSig, SIGQUIT);	/* prevent nested calls */
	PG_SETMASK(&BlockSig);

	/*
	 * Prevent interrupts while exiting; though we just blocked signals that
	 * would queue new interrupts, one may have been pending.  We don't want a
	 * quickdie() downgraded to a mere query cancel.
	 */
	HOLD_INTERRUPTS();

	/*
	 * If we're aborting out of client auth, don't risk trying to send
	 * anything to the client; we will likely violate the protocol, not to
	 * mention that we may have interrupted the guts of OpenSSL or some
	 * authentication library.
	 */
	if (ClientAuthInProgress && whereToSendOutput == DestRemote)
		whereToSendOutput = DestNone;

	/*
	 * Notify the client before exiting, to give a clue on what happened.
	 *
	 * It's dubious to call ereport() from a signal handler.  It is certainly
	 * not async-signal safe.  But it seems better to try, than to disconnect
	 * abruptly and leave the client wondering what happened.  It's remotely
	 * possible that we crash or hang while trying to send the message, but
	 * receiving a SIGQUIT is a sign that something has already gone badly
	 * wrong, so there's not much to lose.  Assuming the postmaster is still
	 * running, it will SIGKILL us soon if we get stuck for some reason.
	 *
	 * One thing we can do to make this a tad safer is to clear the error
	 * context stack, so that context callbacks are not called.  That's a lot
	 * less code that could be reached here, and the context info is unlikely
	 * to be very relevant to a SIGQUIT report anyway.
	 */
	error_context_stack = NULL;

	/*
	 * When responding to a postmaster-issued signal, we send the message only
	 * to the client; sending to the server log just creates log spam, plus
	 * it's more code that we need to hope will work in a signal handler.
	 *
	 * Ideally these should be ereport(FATAL), but then we'd not get control
	 * back to force the correct type of process exit.
	 */
	switch (GetQuitSignalReason())
	{
		case PMQUIT_NOT_SENT:
			if (postgres_signal_arg == SIGTERM)
			{
				/*
				 * pg_cron uses quickdie for not only SIGQUIT handler but also
				 * SIGTERM handler to avoid stuck process.  SIGTERM is received
				 * when tserver tries to kill PG during shutdown.
				 */
				ereport(WARNING_CLIENT_ONLY,
						(errcode(ERRCODE_ADMIN_SHUTDOWN),
						 errmsg("terminating connection due to shutdown command")));
			}
			else
			{
				/* Hmm, SIGQUIT arrived out of the blue */
				ereport(WARNING,
						(errcode(ERRCODE_ADMIN_SHUTDOWN),
						 errmsg("terminating connection because of unexpected SIGQUIT signal")));
			}
			break;
		case PMQUIT_FOR_CRASH:
			/* YB_TODO(Deepthi) Commit c5f22319c2b77de0f2ebeeb797791d925dfd070d */
#ifndef THREAD_SANITIZER
			/* A crash-and-restart cycle is in progress */
			ereport(WARNING_CLIENT_ONLY,
					(errcode(ERRCODE_CRASH_SHUTDOWN),
					 errmsg("terminating connection because of crash of another server process"),
					 errdetail("The postmaster has commanded this server process to roll back"
							   " the current transaction and exit, because another"
							   " server process exited abnormally and possibly corrupted"
							   " shared memory."),
					 errhint("In a moment you should be able to reconnect to the"
							 " database and repeat your command.")));
#endif
			break;
		case PMQUIT_FOR_STOP:
			/* Immediate-mode stop */
			ereport(WARNING_CLIENT_ONLY,
					(errcode(ERRCODE_ADMIN_SHUTDOWN),
					 errmsg("terminating connection due to immediate shutdown command")));
			break;
	}

	/*
	 * We DO NOT want to run proc_exit() or atexit() callbacks -- we're here
	 * because shared memory may be corrupted, so we don't want to try to
	 * clean up our transaction.  Just nail the windows shut and get out of
	 * town.  The callbacks wouldn't be safe to run from a signal handler,
	 * anyway.
	 *
	 * Note we do _exit(2) not _exit(0).  This is to force the postmaster into
	 * a system reset cycle if someone sends a manual SIGQUIT to a random
	 * backend.  This is necessary precisely because we don't clean up our
	 * shared memory state.  (The "dead man switch" mechanism in pmsignal.c
	 * should ensure the postmaster sees this as a crash, too, but no harm in
	 * being doubly sure.)
	 */
	_exit(2);
}

/*
 * Shutdown signal from postmaster: abort transaction and exit
 * at soonest convenient time
 */
void
die(SIGNAL_ARGS)
{
	int			save_errno = errno;

	/* Don't joggle the elbow of proc_exit */
	if (!proc_exit_inprogress)
	{
		InterruptPending = true;
		ProcDiePending = true;
	}

	if (IsYugaByteEnabled())
		YBCInterruptPgGate();

	/* for the cumulative stats system */
	pgStatSessionEndCause = DISCONNECT_KILLED;

	/* If we're still here, waken anything waiting on the process latch */
	SetLatch(MyLatch);

	/*
	 * If we're in single user mode, we want to quit immediately - we can't
	 * rely on latches as they wouldn't work when stdin/stdout is a file.
	 * Rather ugly, but it's unlikely to be worthwhile to invest much more
	 * effort just for the benefit of single user mode.
	 */
	if (DoingCommandRead && whereToSendOutput != DestRemote)
		ProcessInterrupts();

	errno = save_errno;
}

/*
 * Query-cancel signal from postmaster: abort current transaction
 * at soonest convenient time
 */
void
StatementCancelHandler(SIGNAL_ARGS)
{
	int			save_errno = errno;

	/*
	 * Don't joggle the elbow of proc_exit
	 */
	if (!proc_exit_inprogress)
	{
		InterruptPending = true;
		QueryCancelPending = true;
	}

	/* If we're still here, waken anything waiting on the process latch */
	SetLatch(MyLatch);

	errno = save_errno;
}

/* signal handler for floating point exception */
void
FloatExceptionHandler(SIGNAL_ARGS)
{
	/* We're not returning, so no need to save errno */
	ereport(ERROR,
			(errcode(ERRCODE_FLOATING_POINT_EXCEPTION),
			 errmsg("floating-point exception"),
			 errdetail("An invalid floating-point operation was signaled. "
					   "This probably means an out-of-range result or an "
					   "invalid operation, such as division by zero.")));
}

/*
 * RecoveryConflictInterrupt: out-of-line portion of recovery conflict
 * handling following receipt of SIGUSR1. Designed to be similar to die()
 * and StatementCancelHandler(). Called only by a normal user backend
 * that begins a transaction during recovery.
 */
void
RecoveryConflictInterrupt(ProcSignalReason reason)
{
	int			save_errno = errno;

	/*
	 * Don't joggle the elbow of proc_exit
	 */
	if (!proc_exit_inprogress)
	{
		RecoveryConflictReason = reason;
		switch (reason)
		{
			case PROCSIG_RECOVERY_CONFLICT_STARTUP_DEADLOCK:

				/*
				 * If we aren't waiting for a lock we can never deadlock.
				 */
				if (!IsWaitingForLock())
					return;

				/* Intentional fall through to check wait for pin */
				switch_fallthrough();

			case PROCSIG_RECOVERY_CONFLICT_BUFFERPIN:

				/*
				 * If PROCSIG_RECOVERY_CONFLICT_BUFFERPIN is requested but we
				 * aren't blocking the Startup process there is nothing more
				 * to do.
				 *
				 * When PROCSIG_RECOVERY_CONFLICT_STARTUP_DEADLOCK is
				 * requested, if we're waiting for locks and the startup
				 * process is not waiting for buffer pin (i.e., also waiting
				 * for locks), we set the flag so that ProcSleep() will check
				 * for deadlocks.
				 */
				if (!HoldingBufferPinThatDelaysRecovery())
				{
					if (reason == PROCSIG_RECOVERY_CONFLICT_STARTUP_DEADLOCK &&
						GetStartupBufferPinWaitBufId() < 0)
						CheckDeadLockAlert();
					return;
				}

				MyProc->recoveryConflictPending = true;

				/* Intentional fall through to error handling */
				switch_fallthrough();

			case PROCSIG_RECOVERY_CONFLICT_LOCK:
			case PROCSIG_RECOVERY_CONFLICT_TABLESPACE:
			case PROCSIG_RECOVERY_CONFLICT_SNAPSHOT:

				/*
				 * If we aren't in a transaction any longer then ignore.
				 */
				if (!IsTransactionOrTransactionBlock())
					return;

				/*
				 * If we can abort just the current subtransaction then we are
				 * OK to throw an ERROR to resolve the conflict. Otherwise
				 * drop through to the FATAL case.
				 *
				 * XXX other times that we can throw just an ERROR *may* be
				 * PROCSIG_RECOVERY_CONFLICT_LOCK if no locks are held in
				 * parent transactions
				 *
				 * PROCSIG_RECOVERY_CONFLICT_SNAPSHOT if no snapshots are held
				 * by parent transactions and the transaction is not
				 * transaction-snapshot mode
				 *
				 * PROCSIG_RECOVERY_CONFLICT_TABLESPACE if no temp files or
				 * cursors open in parent transactions
				 */
				if (!IsSubTransaction())
				{
					/*
					 * If we already aborted then we no longer need to cancel.
					 * We do this here since we do not wish to ignore aborted
					 * subtransactions, which must cause FATAL, currently.
					 */
					if (IsAbortedTransactionBlockState())
						return;

					RecoveryConflictPending = true;
					QueryCancelPending = true;
					InterruptPending = true;
					break;
				}

				/* Intentional fall through to session cancel */
				switch_fallthrough();

			case PROCSIG_RECOVERY_CONFLICT_DATABASE:
				RecoveryConflictPending = true;
				ProcDiePending = true;
				InterruptPending = true;
				break;

			default:
				elog(FATAL, "unrecognized conflict mode: %d",
					 (int) reason);
		}

		Assert(RecoveryConflictPending && (QueryCancelPending || ProcDiePending));

		/*
		 * All conflicts apart from database cause dynamic errors where the
		 * command or transaction can be retried at a later point with some
		 * potential for success. No need to reset this, since non-retryable
		 * conflict errors are currently FATAL.
		 */
		if (reason == PROCSIG_RECOVERY_CONFLICT_DATABASE)
			RecoveryConflictRetryable = false;
	}

	/*
	 * Set the process latch. This function essentially emulates signal
	 * handlers like die() and StatementCancelHandler() and it seems prudent
	 * to behave similarly as they do.
	 */
	SetLatch(MyLatch);

	errno = save_errno;
}

/*
 * ProcessInterrupts: out-of-line portion of CHECK_FOR_INTERRUPTS() macro
 *
 * If an interrupt condition is pending, and it's safe to service it,
 * then clear the flag and accept the interrupt.  Called only when
 * InterruptPending is true.
 *
 * Note: if INTERRUPTS_CAN_BE_PROCESSED() is true, then ProcessInterrupts
 * is guaranteed to clear the InterruptPending flag before returning.
 * (This is not the same as guaranteeing that it's still clear when we
 * return; another interrupt could have arrived.  But we promise that
 * any pre-existing one will have been serviced.)
 */
void
ProcessInterrupts(void)
{
	/* OK to accept any interrupts now? */
	if (InterruptHoldoffCount != 0 || CritSectionCount != 0)
		return;
	InterruptPending = false;

	if (ProcDiePending)
	{
		ProcDiePending = false;
		QueryCancelPending = false; /* ProcDie trumps QueryCancel */
		LockErrorCleanup();
		/* As in quickdie, don't risk sending to client during auth */
		if (ClientAuthInProgress && whereToSendOutput == DestRemote)
			whereToSendOutput = DestNone;
		if (ClientAuthInProgress)
			ereport(FATAL,
					(errcode(ERRCODE_QUERY_CANCELED),
					 errmsg("canceling authentication due to timeout")));
		else if (IsAutoVacuumWorkerProcess())
			ereport(FATAL,
					(errcode(ERRCODE_ADMIN_SHUTDOWN),
					 errmsg("terminating autovacuum process due to administrator command")));
		else if (IsLogicalWorker())
			ereport(FATAL,
					(errcode(ERRCODE_ADMIN_SHUTDOWN),
					 errmsg("terminating logical replication worker due to administrator command")));
		else if (IsLogicalLauncher())
		{
			ereport(DEBUG1,
					(errmsg_internal("logical replication launcher shutting down")));

			/*
			 * The logical replication launcher can be stopped at any time.
			 * Use exit status 1 so the background worker is restarted.
			 */
			proc_exit(1);
		}
		else if (RecoveryConflictPending && RecoveryConflictRetryable)
		{
			pgstat_report_recovery_conflict(RecoveryConflictReason);
			ereport(FATAL,
					(errcode(ERRCODE_T_R_SERIALIZATION_FAILURE),
					 errmsg("terminating connection due to conflict with recovery"),
					 errdetail_recovery_conflict()));
		}
		else if (RecoveryConflictPending)
		{
			/* Currently there is only one non-retryable recovery conflict */
			Assert(RecoveryConflictReason == PROCSIG_RECOVERY_CONFLICT_DATABASE);
			pgstat_report_recovery_conflict(RecoveryConflictReason);
			ereport(FATAL,
					(errcode(ERRCODE_DATABASE_DROPPED),
					 errmsg("terminating connection due to conflict with recovery"),
					 errdetail_recovery_conflict()));
		}
		else if (IsBackgroundWorker)
			ereport(FATAL,
					(errcode(ERRCODE_ADMIN_SHUTDOWN),
					 errmsg("terminating background worker \"%s\" due to administrator command",
							MyBgworkerEntry->bgw_type)));
		else
			ereport(FATAL,
					(errcode(ERRCODE_ADMIN_SHUTDOWN),
					 errmsg("terminating connection due to administrator command")));
	}

	if (CheckClientConnectionPending)
	{
		CheckClientConnectionPending = false;

		/*
		 * Check for lost connection and re-arm, if still configured, but not
		 * if we've arrived back at DoingCommandRead state.  We don't want to
		 * wake up idle sessions, and they already know how to detect lost
		 * connections.
		 */
		if (!DoingCommandRead && client_connection_check_interval > 0)
		{
			if (!pq_check_connection())
				ClientConnectionLost = true;
			else
				enable_timeout_after(CLIENT_CONNECTION_CHECK_TIMEOUT,
									 client_connection_check_interval);
		}
	}

	if (ClientConnectionLost)
	{
		QueryCancelPending = false; /* lost connection trumps QueryCancel */
		LockErrorCleanup();
		/* don't send to client, we already know the connection to be dead. */
		whereToSendOutput = DestNone;
		ereport(FATAL,
				(errcode(ERRCODE_CONNECTION_FAILURE),
				 errmsg("connection to client lost")));
	}

	/*
	 * If a recovery conflict happens while we are waiting for input from the
	 * client, the client is presumably just sitting idle in a transaction,
	 * preventing recovery from making progress.  Terminate the connection to
	 * dislodge it.
	 */
	if (RecoveryConflictPending && DoingCommandRead)
	{
		QueryCancelPending = false; /* this trumps QueryCancel */
		RecoveryConflictPending = false;
		LockErrorCleanup();
		pgstat_report_recovery_conflict(RecoveryConflictReason);
		ereport(FATAL,
				(errcode(ERRCODE_T_R_SERIALIZATION_FAILURE),
				 errmsg("terminating connection due to conflict with recovery"),
				 errdetail_recovery_conflict(),
				 errhint("In a moment you should be able to reconnect to the"
						 " database and repeat your command.")));
	}

	/*
	 * Don't allow query cancel interrupts while reading input from the
	 * client, because we might lose sync in the FE/BE protocol.  (Die
	 * interrupts are OK, because we won't read any further messages from the
	 * client in that case.)
	 */
	if (QueryCancelPending && QueryCancelHoldoffCount != 0)
	{
		/*
		 * Re-arm InterruptPending so that we process the cancel request as
		 * soon as we're done reading the message.  (XXX this is seriously
		 * ugly: it complicates INTERRUPTS_CAN_BE_PROCESSED(), and it means we
		 * can't use that macro directly as the initial test in this function,
		 * meaning that this code also creates opportunities for other bugs to
		 * appear.)
		 */
		InterruptPending = true;
	}
	else if (QueryCancelPending)
	{
		bool		lock_timeout_occurred;
		bool		stmt_timeout_occurred;

		QueryCancelPending = false;

		/*
		 * If LOCK_TIMEOUT and STATEMENT_TIMEOUT indicators are both set, we
		 * need to clear both, so always fetch both.
		 */
		lock_timeout_occurred = get_timeout_indicator(LOCK_TIMEOUT, true);
		stmt_timeout_occurred = get_timeout_indicator(STATEMENT_TIMEOUT, true);

		/*
		 * If both were set, we want to report whichever timeout completed
		 * earlier; this ensures consistent behavior if the machine is slow
		 * enough that the second timeout triggers before we get here.  A tie
		 * is arbitrarily broken in favor of reporting a lock timeout.
		 */
		if (lock_timeout_occurred && stmt_timeout_occurred &&
			get_timeout_finish_time(STATEMENT_TIMEOUT) < get_timeout_finish_time(LOCK_TIMEOUT))
			lock_timeout_occurred = false;	/* report stmt timeout */

		if (lock_timeout_occurred)
		{
			LockErrorCleanup();
			ereport(ERROR,
					(errcode(ERRCODE_LOCK_NOT_AVAILABLE),
					 errmsg("canceling statement due to lock timeout")));
		}
		if (stmt_timeout_occurred)
		{
			LockErrorCleanup();
			ereport(ERROR,
					(errcode(ERRCODE_QUERY_CANCELED),
					 errmsg("canceling statement due to statement timeout")));
		}
		if (IsAutoVacuumWorkerProcess())
		{
			LockErrorCleanup();
			ereport(ERROR,
					(errcode(ERRCODE_QUERY_CANCELED),
					 errmsg("canceling autovacuum task")));
		}
		if (RecoveryConflictPending)
		{
			RecoveryConflictPending = false;
			LockErrorCleanup();
			pgstat_report_recovery_conflict(RecoveryConflictReason);
			ereport(ERROR,
					(errcode(ERRCODE_T_R_SERIALIZATION_FAILURE),
					 errmsg("canceling statement due to conflict with recovery"),
					 errdetail_recovery_conflict()));
		}

		/*
		 * If we are reading a command from the client, just ignore the cancel
		 * request --- sending an extra error message won't accomplish
		 * anything.  Otherwise, go ahead and throw the error.
		 */
		if (!DoingCommandRead)
		{
			LockErrorCleanup();
			ereport(ERROR,
					(errcode(ERRCODE_QUERY_CANCELED),
					 errmsg("canceling statement due to user request")));
		}
	}

	if (IdleInTransactionSessionTimeoutPending)
	{
		/*
		 * If the GUC has been reset to zero, ignore the signal.  This is
		 * important because the GUC update itself won't disable any pending
		 * interrupt.
		 */
		if (IdleInTransactionSessionTimeout > 0)
			ereport(FATAL,
					(errcode(ERRCODE_IDLE_IN_TRANSACTION_SESSION_TIMEOUT),
					 errmsg("terminating connection due to idle-in-transaction timeout")));
		else
			IdleInTransactionSessionTimeoutPending = false;
	}

	if (IdleSessionTimeoutPending)
	{
		/* As above, ignore the signal if the GUC has been reset to zero. */
		if (IdleSessionTimeout > 0)
			ereport(FATAL,
					(errcode(ERRCODE_IDLE_SESSION_TIMEOUT),
					 errmsg("terminating connection due to idle-session timeout")));
		else
			IdleSessionTimeoutPending = false;
	}

	/*
	 * If there are pending stats updates and we currently are truly idle
	 * (matching the conditions in PostgresMain(), report stats now.
	 */
	if (IdleStatsUpdateTimeoutPending &&
		DoingCommandRead && !IsTransactionOrTransactionBlock())
	{
		IdleStatsUpdateTimeoutPending = false;
		pgstat_report_stat(true);
	}

	if (ProcSignalBarrierPending)
		ProcessProcSignalBarrier();

	if (ParallelMessagePending)
		HandleParallelMessages();

	if (LogMemoryContextPending)
		ProcessLogMemoryContextInterrupt();
}


/*
 * IA64-specific code to fetch the AR.BSP register for stack depth checks.
 *
 * We currently support gcc, icc, and HP-UX's native compiler here.
 *
 * Note: while icc accepts gcc asm blocks on x86[_64], this is not true on
 * ia64 (at least not in icc versions before 12.x).  So we have to carry a
 * separate implementation for it.
 */
#if defined(__ia64__) || defined(__ia64)

#if defined(__hpux) && !defined(__GNUC__) && !defined(__INTEL_COMPILER)
/* Assume it's HP-UX native compiler */
#include <ia64/sys/inline.h>
#define ia64_get_bsp() ((char *) (_Asm_mov_from_ar(_AREG_BSP, _NO_FENCE)))
#elif defined(__INTEL_COMPILER)
/* icc */
#include <asm/ia64regs.h>
#define ia64_get_bsp() ((char *) __getReg(_IA64_REG_AR_BSP))
#else
/* gcc */
static __inline__ char *
ia64_get_bsp(void)
{
	char	   *ret;

	/* the ;; is a "stop", seems to be required before fetching BSP */
	__asm__ __volatile__(
						 ";;\n"
						 "	mov	%0=ar.bsp	\n"
:						 "=r"(ret));

	return ret;
}
#endif
#endif							/* IA64 */


/*
 * set_stack_base: set up reference point for stack depth checking
 *
 * Returns the old reference point, if any.
 */
pg_stack_base_t
set_stack_base(void)
{
#ifndef HAVE__BUILTIN_FRAME_ADDRESS
	char		stack_base;
#endif
	pg_stack_base_t old;

#if defined(__ia64__) || defined(__ia64)
	old.stack_base_ptr = stack_base_ptr;
	old.register_stack_base_ptr = register_stack_base_ptr;
#else
	old = stack_base_ptr;
#endif

	/*
	 * Set up reference point for stack depth checking.  On recent gcc we use
	 * __builtin_frame_address() to avoid a warning about storing a local
	 * variable's address in a long-lived variable.
	 */
/* YB_TODO(mikhail) Commit fda466915e304491214789d9b08f36c19e7fd775 */
#if !defined(__clang__) && defined(__GNUC__) && __GNUC__ >= 12
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdangling-pointer"
#endif

#ifdef HAVE__BUILTIN_FRAME_ADDRESS
	stack_base_ptr = __builtin_frame_address(0);
#else
	stack_base_ptr = &stack_base;
#endif

#if !defined(__clang__) && defined(__GNUC__) && __GNUC__ >= 12
#pragma GCC diagnostic pop
#endif

#if defined(__ia64__) || defined(__ia64)
	register_stack_base_ptr = ia64_get_bsp();
#endif

	return old;
}

/*
 * restore_stack_base: restore reference point for stack depth checking
 *
 * This can be used after set_stack_base() to restore the old value. This
 * is currently only used in PL/Java. When PL/Java calls a backend function
 * from different thread, the thread's stack is at a different location than
 * the main thread's stack, so it sets the base pointer before the call, and
 * restores it afterwards.
 */
void
restore_stack_base(pg_stack_base_t base)
{
#if defined(__ia64__) || defined(__ia64)
	stack_base_ptr = base.stack_base_ptr;
	register_stack_base_ptr = base.register_stack_base_ptr;
#else
	stack_base_ptr = base;
#endif
}

/*
 * check_stack_depth/stack_is_too_deep: check for excessively deep recursion
 *
 * This should be called someplace in any recursive routine that might possibly
 * recurse deep enough to overflow the stack.  Most Unixen treat stack
 * overflow as an unrecoverable SIGSEGV, so we want to error out ourselves
 * before hitting the hardware limit.
 *
 * check_stack_depth() just throws an error summarily.  stack_is_too_deep()
 * can be used by code that wants to handle the error condition itself.
 */
void
check_stack_depth(void)
{
	if (stack_is_too_deep())
	{
		ereport(ERROR,
				(errcode(ERRCODE_STATEMENT_TOO_COMPLEX),
				 errmsg("stack depth limit exceeded"),
				 errhint("Increase the configuration parameter \"max_stack_depth\" (currently %dkB), "
						 "after ensuring the platform's stack depth limit is adequate.",
						 max_stack_depth)));
	}
}

bool
stack_is_too_deep(void)
{
#ifdef ADDRESS_SANITIZER
	// Postgres analyzes/limits stack depth based on local variables address
	// offset.
	// This method works well in case of regular call stack (i.e. when all
	// stack frames are allocated in stack).
	// But for the detect_stack_use_after_return ASAN uses fake stack. In case
	// of using it stack frames are allocated in the heap. As a result it is
	// not possible to estimate stack depth base on local variables address
	// offset.
	// To make stack_is_too_deep return predictable results in case of ASAN it
	// is reasonable to return false all the time.
	// Note:
	// YSQL has some unit tests which checks that Postgres can detect too
	// deep recursion. These tests change the `max_stack_depth` GUC variable
	// to lower value. And later restore the original value with the
	// `RESET max_stack_depth` statement.
	// To make these tests works under the ASAN the function returns true in
	// case the `max_stack_depth` GUC contains non default value and number of
	// call stack frames is huge enough.
	// The check of call stack frames is required to avoid undesired failure on
	// attempt to restore original value for the `max_stack_depth` GUC with
	// the `RESET max_stack_depth` statement.
	if (get_guc_variables()) {
		const char* max_stack_depth_GUC = "max_stack_depth";
		const char* current_value =
			GetConfigOption(max_stack_depth_GUC, false, false);
		const char* default_value =
			GetConfigOptionResetString(max_stack_depth_GUC);
		if (strcmp(current_value, default_value) != 0) {
			static const int MAX_STACK_FRAMES = 64;
			void* frames[MAX_STACK_FRAMES];
			int frames_count =
				YBCGetCallStackFrames(frames, MAX_STACK_FRAMES, 0);
			return frames_count >= MAX_STACK_FRAMES;
		}
	}
	return false;
#endif
	char		stack_top_loc;
	long		stack_depth;

	/*
	 * Compute distance from reference point to my local variables
	 */
	stack_depth = (long) (stack_base_ptr - &stack_top_loc);

	/*
	 * Take abs value, since stacks grow up on some machines, down on others
	 */
	if (stack_depth < 0)
		stack_depth = -stack_depth;

	/*
	 * Trouble?
	 *
	 * The test on stack_base_ptr prevents us from erroring out if called
	 * during process setup or in a non-backend process.  Logically it should
	 * be done first, but putting it here avoids wasting cycles during normal
	 * cases.
	 */
	if (stack_depth > max_stack_depth_bytes &&
		stack_base_ptr != NULL)
		return true;

	/*
	 * On IA64 there is a separate "register" stack that requires its own
	 * independent check.  For this, we have to measure the change in the
	 * "BSP" pointer from PostgresMain to here.  Logic is just as above,
	 * except that we know IA64's register stack grows up.
	 *
	 * Note we assume that the same max_stack_depth applies to both stacks.
	 */
#if defined(__ia64__) || defined(__ia64)
	stack_depth = (long) (ia64_get_bsp() - register_stack_base_ptr);

	if (stack_depth > max_stack_depth_bytes &&
		register_stack_base_ptr != NULL)
		return true;
#endif							/* IA64 */

	return false;
}

/* GUC check hook for max_stack_depth */
bool
check_max_stack_depth(int *newval, void **extra, GucSource source)
{
	long		newval_bytes = *newval * 1024L;
	long		stack_rlimit = get_stack_depth_rlimit();

	if (stack_rlimit > 0 && newval_bytes > stack_rlimit - STACK_DEPTH_SLOP)
	{
		GUC_check_errdetail("\"max_stack_depth\" must not exceed %ldkB.",
							(stack_rlimit - STACK_DEPTH_SLOP) / 1024L);
		GUC_check_errhint("Increase the platform's stack depth limit via \"ulimit -s\" or local equivalent.");
		return false;
	}
	return true;
}

/* GUC assign hook for max_stack_depth */
void
assign_max_stack_depth(int newval, void *extra)
{
	long		newval_bytes = newval * 1024L;

	max_stack_depth_bytes = newval_bytes;
}

/*
 * GUC check_hook for restrict_nonsystem_relation_kind
 */
bool
check_restrict_nonsystem_relation_kind(char **newval, void **extra, GucSource source)
{
	char	   *rawstring;
	List	   *elemlist;
	ListCell   *l;
	int			flags = 0;

	/* Need a modifiable copy of string */
	rawstring = pstrdup(*newval);

	if (!SplitIdentifierString(rawstring, ',', &elemlist))
	{
		/* syntax error in list */
		GUC_check_errdetail("List syntax is invalid.");
		pfree(rawstring);
		list_free(elemlist);
		return false;
	}

	foreach(l, elemlist)
	{
		char	   *tok = (char *) lfirst(l);

		if (pg_strcasecmp(tok, "view") == 0)
			flags |= RESTRICT_RELKIND_VIEW;
		else if (pg_strcasecmp(tok, "foreign-table") == 0)
			flags |= RESTRICT_RELKIND_FOREIGN_TABLE;
		else
		{
			GUC_check_errdetail("Unrecognized key word: \"%s\".", tok);
			pfree(rawstring);
			list_free(elemlist);
			return false;
		}
	}

	pfree(rawstring);
	list_free(elemlist);

	/* Save the flags in *extra, for use by the assign function */
	*extra = malloc(sizeof(int));
	*((int *) *extra) = flags;

	return true;
}

/*
 * GUC assign_hook for restrict_nonsystem_relation_kind
 */
void
assign_restrict_nonsystem_relation_kind(const char *newval, void *extra)
{
	int		   *flags = (int *) extra;

	restrict_nonsystem_relation_kind = *flags;
}

/*
 * set_debug_options --- apply "-d N" command line option
 *
 * -d is not quite the same as setting log_min_messages because it enables
 * other output options.
 */
void
set_debug_options(int debug_flag, GucContext context, GucSource source)
{
	if (debug_flag > 0)
	{
		char		debugstr[64];

		sprintf(debugstr, "debug%d", debug_flag);
		SetConfigOption("log_min_messages", debugstr, context, source);
	}
	else
		SetConfigOption("log_min_messages", "notice", context, source);

	if (debug_flag >= 1 && context == PGC_POSTMASTER)
	{
		SetConfigOption("log_connections", "true", context, source);
		SetConfigOption("log_disconnections", "true", context, source);
	}
	if (debug_flag >= 2)
		SetConfigOption("log_statement", "all", context, source);
	if (debug_flag >= 3)
		SetConfigOption("debug_print_parse", "true", context, source);
	if (debug_flag >= 4)
		SetConfigOption("debug_print_plan", "true", context, source);
	if (debug_flag >= 5)
		SetConfigOption("debug_print_rewritten", "true", context, source);
}


bool
set_plan_disabling_options(const char *arg, GucContext context, GucSource source)
{
	const char *tmp = NULL;

	switch (arg[0])
	{
		case 's':				/* seqscan */
			tmp = "enable_seqscan";
			break;
		case 'i':				/* indexscan */
			tmp = "enable_indexscan";
			break;
		case 'o':				/* indexonlyscan */
			tmp = "enable_indexonlyscan";
			break;
		case 'b':				/* bitmapscan */
			tmp = "enable_bitmapscan";
			break;
		case 't':				/* tidscan */
			tmp = "enable_tidscan";
			break;
		case 'n':				/* nestloop */
			tmp = "enable_nestloop";
			break;
		case 'm':				/* mergejoin */
			tmp = "enable_mergejoin";
			break;
		case 'h':				/* hashjoin */
			tmp = "enable_hashjoin";
			break;
	}
	if (tmp)
	{
		SetConfigOption(tmp, "false", context, source);
		return true;
	}
	else
		return false;
}


const char *
get_stats_option_name(const char *arg)
{
	switch (arg[0])
	{
		case 'p':
			if (optarg[1] == 'a')	/* "parser" */
				return "log_parser_stats";
			else if (optarg[1] == 'l')	/* "planner" */
				return "log_planner_stats";
			break;

		case 'e':				/* "executor" */
			return "log_executor_stats";
			break;
	}

	return NULL;
}


/* ----------------------------------------------------------------
 * process_postgres_switches
 *	   Parse command line arguments for backends
 *
 * This is called twice, once for the "secure" options coming from the
 * postmaster or command line, and once for the "insecure" options coming
 * from the client's startup packet.  The latter have the same syntax but
 * may be restricted in what they can do.
 *
 * argv[0] is ignored in either case (it's assumed to be the program name).
 *
 * ctx is PGC_POSTMASTER for secure options, PGC_BACKEND for insecure options
 * coming from the client, or PGC_SU_BACKEND for insecure options coming from
 * a superuser client.
 *
 * If a database name is present in the command line arguments, it's
 * returned into *dbname (this is allowed only if *dbname is initially NULL).
 * ----------------------------------------------------------------
 */
void
process_postgres_switches(int argc, char *argv[], GucContext ctx,
						  const char **dbname)
{
	bool		secure = (ctx == PGC_POSTMASTER);
	int			errs = 0;
	GucSource	gucsource;
	int			flag;

	if (secure)
	{
		gucsource = PGC_S_ARGV; /* switches came from command line */

		/* Ignore the initial --single argument, if present */
		if (argc > 1 && strcmp(argv[1], "--single") == 0)
		{
			argv++;
			argc--;
		}
	}
	else
	{
		gucsource = PGC_S_CLIENT;	/* switches came from client */
	}

#ifdef HAVE_INT_OPTERR

	/*
	 * Turn this off because it's either printed to stderr and not the log
	 * where we'd want it, or argv[0] is now "--single", which would make for
	 * a weird error message.  We print our own error message below.
	 */
	opterr = 0;
#endif

	/*
	 * Parse command-line options.  CAUTION: keep this in sync with
	 * postmaster/postmaster.c (the option sets should not conflict) and with
	 * the common help() function in main/main.c.
	 */
	while ((flag = getopt(argc, argv, "B:bc:C:D:d:EeFf:h:ijk:lN:nOPp:r:S:sTt:v:W:-:")) != -1)
	{
		switch (flag)
		{
			case 'B':
				SetConfigOption("shared_buffers", optarg, ctx, gucsource);
				break;

			case 'b':
				/* Undocumented flag used for binary upgrades */
				if (secure)
					IsBinaryUpgrade = true;
				break;

			case 'C':
				/* ignored for consistency with the postmaster */
				break;

			case 'D':
				if (secure)
					userDoption = strdup(optarg);
				break;

			case 'd':
				set_debug_options(atoi(optarg), ctx, gucsource);
				break;

			case 'E':
				if (secure)
					EchoQuery = true;
				break;

			case 'e':
				SetConfigOption("datestyle", "euro", ctx, gucsource);
				break;

			case 'F':
				SetConfigOption("fsync", "false", ctx, gucsource);
				break;

			case 'f':
				if (!set_plan_disabling_options(optarg, ctx, gucsource))
					errs++;
				break;

			case 'h':
				SetConfigOption("listen_addresses", optarg, ctx, gucsource);
				break;

			case 'i':
				SetConfigOption("listen_addresses", "*", ctx, gucsource);
				break;

			case 'j':
				if (secure)
					UseSemiNewlineNewline = true;
				break;

			case 'k':
				SetConfigOption("unix_socket_directories", optarg, ctx, gucsource);
				break;

			case 'l':
				SetConfigOption("ssl", "true", ctx, gucsource);
				break;

			case 'N':
				SetConfigOption("max_connections", optarg, ctx, gucsource);
				break;

			case 'n':
				/* ignored for consistency with postmaster */
				break;

			case 'O':
				SetConfigOption("allow_system_table_mods", "true", ctx, gucsource);
				break;

			case 'P':
				SetConfigOption("ignore_system_indexes", "true", ctx, gucsource);
				break;

			case 'p':
				SetConfigOption("port", optarg, ctx, gucsource);
				break;

			case 'r':
				/* send output (stdout and stderr) to the given file */
				if (secure)
					strlcpy(OutputFileName, optarg, MAXPGPATH);
				break;

			case 'S':
				SetConfigOption("work_mem", optarg, ctx, gucsource);
				break;

			case 's':
				SetConfigOption("log_statement_stats", "true", ctx, gucsource);
				break;

			case 'T':
				/* ignored for consistency with the postmaster */
				break;

			case 't':
				{
					const char *tmp = get_stats_option_name(optarg);

					if (tmp)
						SetConfigOption(tmp, "true", ctx, gucsource);
					else
						errs++;
					break;
				}

			case 'v':

				/*
				 * -v is no longer used in normal operation, since
				 * FrontendProtocol is already set before we get here. We keep
				 * the switch only for possible use in standalone operation,
				 * in case we ever support using normal FE/BE protocol with a
				 * standalone backend.
				 */
				if (secure)
					FrontendProtocol = (ProtocolVersion) atoi(optarg);
				break;

			case 'W':
				SetConfigOption("post_auth_delay", optarg, ctx, gucsource);
				break;

			case 'c':
			case '-':
				{
					char	   *name,
							   *value;

					ParseLongOption(optarg, &name, &value);
					if (!value)
					{
						if (flag == '-')
							ereport(ERROR,
									(errcode(ERRCODE_SYNTAX_ERROR),
									 errmsg("--%s requires a value",
											optarg)));
						else
							ereport(ERROR,
									(errcode(ERRCODE_SYNTAX_ERROR),
									 errmsg("-c %s requires a value",
											optarg)));
					}
					SetConfigOption(name, value, ctx, gucsource);
					free(name);
					if (value)
						free(value);
					break;
				}

			default:
				errs++;
				break;
		}

		if (errs)
			break;
	}

	/*
	 * Optional database name should be there only if *dbname is NULL.
	 */
	if (!errs && dbname && *dbname == NULL && argc - optind >= 1)
		*dbname = strdup(argv[optind++]);

	if (errs || argc != optind)
	{
		if (errs)
			optind--;			/* complain about the previous argument */

		/* spell the error message a bit differently depending on context */
		if (IsUnderPostmaster)
			ereport(FATAL,
					errcode(ERRCODE_SYNTAX_ERROR),
					errmsg("invalid command-line argument for server process: %s", argv[optind]),
					errhint("Try \"%s --help\" for more information.", progname));
		else
			ereport(FATAL,
					errcode(ERRCODE_SYNTAX_ERROR),
					errmsg("%s: invalid command-line argument: %s",
						   progname, argv[optind]),
					errhint("Try \"%s --help\" for more information.", progname));
	}

	/*
	 * Reset getopt(3) library so that it will work correctly in subprocesses
	 * or when this function is called a second time with another array.
	 */
	optind = 1;
#ifdef HAVE_INT_OPTRESET
	optreset = 1;				/* some systems need this too */
#endif
}

/*
 * Reload the postgres caches and update the cache version.
 * Note: if catalog changes sneaked in since getting the
 * version it is unfortunate but ok. The master version will have
 * changed too (making our version number obsolete) so we will just end
 * up needing to do another cache refresh later.
 * See the comment for yb_catalog_cache_version in 'pg_yb_utils.h' for
 * more details.
 */
static void YBRefreshCache()
{
	Assert(OidIsValid(MyDatabaseId));

	/*
	 * Check that we are not already inside a transaction or we might end up
	 * leaking cache references for any open relations (i.e. relations in-use by
	 * the current transaction).
	 *
	 * Caller(s) should have already ensured that this is the case.
	 */
	if (xact_started)
	{
		ereport(ERROR,
				(errcode(ERRCODE_INTERNAL_ERROR),
				 errmsg("cannot refresh cache within a transaction")));
	}

	if (yb_debug_log_catcache_events)
	{
		ereport(LOG,(errmsg("refreshing catalog cache.")));
	}

	/*
	 * Get the latest syscatalog version from the master.
	 * Reset the cached version type if needed to force reading catalog version
	 * from the catalog table first.
	 */
	if (yb_catalog_version_type != CATALOG_VERSION_CATALOG_TABLE)
		yb_catalog_version_type = CATALOG_VERSION_UNSET;

	/* Need to execute some (read) queries internally so start a local txn. */
	yb_start_xact_command_internal(true /* yb_skip_read_committed_internal_savepoint */);

	/* Clear and reload system catalog caches, including all callbacks. */
	ResetCatalogCaches();
	YbRelationCacheInvalidate();
	CallSystemCacheCallbacks();

	YBPreloadRelCache();

	/* Also invalidate the pggate cache. */
	HandleYBStatus(YBCPgInvalidateCache());

	yb_need_cache_refresh = false;

	finish_xact_command();
}

static bool YBTableSchemaVersionMismatchError(ErrorData *edata, char **table_id)
{
	if (!IsYugaByteEnabled())
		return false;

	const char *table_cache_refresh_search_str = "schema version mismatch for table ";
	char *table_to_refresh = strstr(edata->message, table_cache_refresh_search_str);
	if (table_to_refresh)
	{
		table_to_refresh += strlen(table_cache_refresh_search_str);
		const int size_of_uuid = 16; /* boost::uuids::uuid::static_size() */
		const int size_of_hex_uuid = size_of_uuid * 2;
		if (strlen(table_to_refresh) >= size_of_hex_uuid)
		{
			if (table_id)
				*table_id = pnstrdup(table_to_refresh, size_of_hex_uuid);
			return true;
		}
	}
	return false;
}

static void YBPrepareCacheRefreshIfNeeded(ErrorData *edata,
										  bool consider_retry,
										  bool is_dml,
										  bool *need_retry)
{
	*need_retry = false;

	/*
	 * A retry is only required if the transaction is handled by YugaByte.
	 */
	if (!IsYugaByteEnabled())
		return;

	/*
	 * A non-DDL statement that failed due to transaction conflict does not
	 * require cache refresh.
	*/
	bool is_read_restart_error = YBCIsRestartReadError(edata->yb_txn_errcode);
	bool is_conflict_error     = YBCIsTxnConflictError(edata->yb_txn_errcode);
	bool is_deadlock_error	   = YBCIsTxnDeadlockError(edata->yb_txn_errcode);
	bool is_aborted_error      = YBCIsTxnAbortedError(edata->yb_txn_errcode);

	/*
	 * Note that 'is_dml' could be set for a Select operation on a pg_catalog
	 * table. Even if it fails due to conflict, a retry is expected to succeed
	 * without refreshing the cache (as the schema of a PG catalog table cannot
	 * change).
	 */
	if (is_dml && (is_read_restart_error || is_conflict_error || is_deadlock_error
			|| is_aborted_error))
	{
		return;
	}
	char *table_to_refresh = NULL;
	const bool need_table_cache_refresh =
		YBTableSchemaVersionMismatchError(edata, &table_to_refresh);

	/*
	 * Get the latest syscatalog version from the master to check if we need
	 * to refresh the cache.
	 */
	bool need_global_cache_refresh = false;
	/*
	 * If an operation on the PG catalog has failed at this point, the
	 * below YbGetMasterCatalogVersion() is not expected to succeed either as it
	 * would be using the same transaction as the failed operation.
	*/
	if (!yb_non_ddl_txn_for_sys_tables_allowed)
	{
		YBCPgResetCatalogReadTime();
		const uint64_t catalog_master_version = YbGetMasterCatalogVersion();
		if (YbGetCatalogCacheVersion() != catalog_master_version) {
			need_global_cache_refresh = true;
			YbUpdateLastKnownCatalogCacheVersion(catalog_master_version);
		}
		if (*YBCGetGFlags()->log_ysql_catalog_versions)
		{
			int elevel = need_global_cache_refresh ? LOG : DEBUG1;
			ereport(elevel,
					(errmsg("%s: got master catalog version: %" PRIu64,
							__func__, catalog_master_version)));
		}
	}
	if (!(need_global_cache_refresh || need_table_cache_refresh))
		return;

	/*
	 * Reset catalog version so that the cache gets marked as invalid and
	 * will be refreshed after the txn ends.
	 */
	if (need_global_cache_refresh)
		yb_need_cache_refresh = true;
	else if (need_table_cache_refresh)
	{
		ereport(LOG,
				(errmsg("invalidating table cache entry %s",
						table_to_refresh)));
		HandleYBStatus(YBCPgInvalidateTableCacheByTableId(table_to_refresh));
	}

	/*
	 * For single-query transactions we abort the current
	 * transaction to undo any already-applied operations
	 * and retry the query.
	 *
	 * For transaction blocks we would have to re-apply
	 * all previous queries and also continue the
	 * transaction for future queries (before commit).
	 * So we just re-throw the error in that case.
	 *
	 * Do not retry statements in a batch for the same reason.
	 *
	 */
	if (consider_retry &&
			!IsTransactionBlock() &&
			!YbIsBatchedExecution() &&
			!YBCGetDisableTransparentCacheRefreshRetry())
	{
		/* Clear error state */
		FlushErrorState();

		/*
		 * Make sure debug_query_string gets reset before we possibly clobber
		 * the storage it points at.
		 */
		debug_query_string = NULL;

		/* Abort the transaction and clean up. */
		AbortCurrentTransaction();
		if (am_walsender)
			WalSndErrorCleanup();

		if (MyReplicationSlot != NULL)
			ReplicationSlotRelease();

		ReplicationSlotCleanup();

		if (doing_extended_query_message)
			ignore_till_sync = true;

		xact_started = false;

		/* Refresh cache now so that the retry uses latest version. */
		if (need_global_cache_refresh)
			YBRefreshCache();

		*need_retry = true;
	}
	else
	{
		if (need_global_cache_refresh)
		{
			int error_code = edata->sqlerrcode;

			/*
			 * TODO: This error occurs in tablet service when snapshot is outdated.
			 * We should eventually translate this type of error as a retryable error
			 * in the upper layer such as in YBCStatusPgsqlError().
			 */
			bool isInvalidCatalogSnapshotError = strstr(edata->message,
					"catalog snapshot used for this transaction has been invalidated") != NULL;

			/*
			 * If we got a schema-version-mismatch error while a DDL happened,
			 * this is likely caused by a conflict between the current
			 * transaction and the DDL transaction.
			 * So we map it to the retryable serialization failure error code.
			 * TODO: consider if we should
			 * 1. map this case to a different (retryable) error code
			 * 2. always map schema-version-mismatch to a retryable error.
			 */
			if (need_table_cache_refresh || isInvalidCatalogSnapshotError)
			{
				error_code = ERRCODE_T_R_SERIALIZATION_FAILURE;
			}

			/*
			 * Report the original error, but add a context mentioning that a
			 * possibly-conflicting, concurrent DDL transaction happened.
			 */
			ereport(edata->elevel,
					(yb_txn_errcode(edata->yb_txn_errcode),
					 errcode(error_code),
					 errmsg("%s", edata->message),
					 edata->detail ? errdetail("%s", edata->detail) : 0,
					 edata->hint ? errhint("%s", edata->hint) : 0,
					 errcontext("Catalog Version Mismatch: A DDL occurred "
								"while processing this query. Try again.")));
		}
		else
		{
			Assert(need_table_cache_refresh);
			ereport(ERROR,
					(errcode(ERRCODE_INTERNAL_ERROR),
					 errmsg("%s", edata->message)));
		}
	}
}

/*
 * Parse query tree via pg_parse_query, suppressing log messages below ERROR level.
 * This is useful e.g. for avoiding "not supported yet and will be ignored" warnings.
 */
static List *
yb_parse_query_silently(const char *query_string)
{
	List* parsetree_list;

	int prev_log_min_messages    = log_min_messages;
	int prev_client_min_messages = client_min_messages;
	PG_TRY();
	{
		log_min_messages    = ERROR;
		client_min_messages = ERROR;
		parsetree_list      = pg_parse_query(query_string);
		log_min_messages    = prev_log_min_messages;
		client_min_messages = prev_client_min_messages;
	}
	PG_CATCH();
	{
		log_min_messages    = prev_log_min_messages;
		client_min_messages = prev_client_min_messages;
		PG_RE_THROW();
	}
	PG_END_TRY();

	return parsetree_list;
}

CommandTag
YbParseCommandTag(const char *query_string)
{
	List* parsetree_list = yb_parse_query_silently(query_string);

	if (list_length(parsetree_list) > 0)
	{
		RawStmt* raw_parse_tree = linitial_node(RawStmt, parsetree_list);
		return CreateCommandTag(raw_parse_tree->stmt);
	}
	else
		return CMDTAG_UNKNOWN;
}

static bool yb_is_begin_transaction(CommandTag command_tag)
{
	return (command_tag == CMDTAG_BEGIN ||
			command_tag == CMDTAG_START_TRANSACTION);
}

/*
 * Find whether the statement is a SELECT/UPDATE/INSERT/DELETE
 * with minimum parsing.
 * Note: This function will always return false if
 * yb_non_ddl_txn_for_sys_tables_allowed is set to true.
 */
static bool yb_is_dml_command(const char *query_string)
{
	if (yb_non_ddl_txn_for_sys_tables_allowed)
	{
		/*
		 * This guc variable is typically used to update the system catalog
		 * directly. Therefore we can assume that the user is running a non-
		 * DML statement.
		*/
		return false;
	}
	if (!query_string)
		return false;

	/*
	 * Detect and return false for replication commands since they are never a
	 * DML. This is needed to avoid calling YbParseCommandTag for replication
	 * commands which have a different grammar (repl_gram.y) and will always
	 * lead to a syntax error.
	 */
	replication_scanner_init(query_string);
	if (replication_scanner_is_replication_command())
	{
		replication_scanner_finish();
		return false;
	}

	CommandTag command_tag = YbParseCommandTag(query_string);
	return (command_tag == CMDTAG_DELETE ||
			command_tag == CMDTAG_INSERT ||
			command_tag == CMDTAG_SELECT ||
			command_tag == CMDTAG_UPDATE);
}

/*
 * Only retry supported commands.
 */
static bool yb_check_retry_allowed(const char *query_string)
{
	return yb_is_dml_command(query_string);
}

static void YBCheckSharedCatalogCacheVersion() {
	/*
	 * We cannot refresh the cache if we are already inside a transaction, so don't
	 * bother checking shared memory.
	 */
	if (IsTransactionOrTransactionBlock())
		return;

	/*
	 * Don't check shared memory if we are in initdb. E.g. during initial system
	 * catalog snapshot creation, tablet servers may not be running.
	 */
	if (YBCIsInitDbModeEnvVarSet())
		return;

	const uint64_t shared_catalog_version = YbGetSharedCatalogVersion();
	const bool need_global_cache_refresh =
		YbGetCatalogCacheVersion() < shared_catalog_version;
	if (*YBCGetGFlags()->log_ysql_catalog_versions)
	{
		int elevel = need_global_cache_refresh ? LOG : DEBUG1;
		ereport(elevel,
				(errmsg("%s: got tserver catalog version: %" PRIu64,
						__func__, shared_catalog_version)));
	}
	if (need_global_cache_refresh)
	{
		YbUpdateLastKnownCatalogCacheVersion(shared_catalog_version);
		YBRefreshCache();
	}
}

/*
 * Data needed to restart a query (plaintext or portal) after its execution failed.
 *
 * Note that in case of a portal query, it refers to values from portal's memory context,
 * so it's only valid as long as the portal exists.
 */
typedef struct YBQueryRetryData
{
	const char *portal_name;	/* '\0' for unnamed portal, NULL if not a portal */
	const char *query_string;
	CommandTag command_tag;
} YBQueryRetryData;

static bool
YBIsDmlCommandTag(CommandTag command_tag)
{
	return (command_tag == CMDTAG_UPDATE ||
			command_tag == CMDTAG_INSERT ||
			command_tag == CMDTAG_DELETE);
}

/* Whether we are allowed to restart current query/txn. */
static bool
yb_is_retry_possible(ErrorData *edata, int attempt,
					 const YBQueryRetryData *retry_data)
{
	CommandTag command_tag;

	if (yb_debug_log_internal_restarts)
		elog(LOG, "Error details: edata->message=%s, edata->filename=%s, "
				 "edata->lineno=%d, edata->yb_txn_errcode=%d", edata->message, edata->filename, edata->lineno,
				 edata->yb_txn_errcode);

	if (!IsYugaByteEnabled())
	{
		if (yb_debug_log_internal_restarts)
			elog(LOG, "query layer retry isn't possible, YB is not enabled");
		return false;
	}

	// kConflict and kReadRestartRequired are retried by restarting the whole
	// transaction in case of Repeatable Read and Serializable isolation levels. In case of
	// Read Committed isolation, these are retried by undoing and retrying just the statement that
	// failed.
	bool is_read_restart_error = YBCIsRestartReadError(edata->yb_txn_errcode);
	bool is_conflict_error = YBCIsTxnConflictError(edata->yb_txn_errcode);

	// Retrying kDeadlock and kAborted errors require restart of the whole transaction. They can't be
	// retried by just retrying the statement that failed.
	bool is_deadlock_error = YBCIsTxnDeadlockError(edata->yb_txn_errcode);
	bool is_aborted_error = YBCIsTxnAbortedError(edata->yb_txn_errcode);

	if (!is_read_restart_error && !is_conflict_error && !is_deadlock_error && !is_aborted_error)
	{
		if (yb_debug_log_internal_restarts)
			elog(LOG,
				 "query layer retry isn't possible, txn error %s isn't one of "
				 "kConflict/kReadRestart/kDeadlock/kAborted",
				 YBCTxnErrCodeToString(edata->yb_txn_errcode));
		return false;
	}

	if (yb_is_multi_statement_query)
	{
		const char* retry_err = "query layer retries aren't supported for multi-statement queries "
				"issued via the simple query protocol, upvote github issue #21833 if you want this";
		edata->message = psprintf("%s (%s)", edata->message, retry_err);
		if (yb_debug_log_internal_restarts)
			elog(LOG, "%s", retry_err);
		return false;
	}

	/*
	 * When retrying read committed transactions with best-effort in case of deadlock/ abort errors,
	 * note that we can't rollback and retry just the current statement as we do in read committed for
	 * kReadRestart and kConflict errors (see yb_attempt_to_retry_on_error()). There are 2 reasons
	 * for this:
	 *
	 * 1) The txn has already been aborted (directly or for breaking the deadlock).
	 * 2) For deadlock errors, even if we were to somehow ensure in docdb that the transaction is not
	 *    aborted but just removed from the wait queue to avoid (1), we can't differentiate if locks
	 *    acquired by this transaction that are part of the deadlock cycle were acquired in a previous
	 *    statement or the current statement. If acquired in a previous statement, retrying the
	 *    current statement is of no use. If acquired in the current statement, a restart could help
	 *    in resolving the deadlock since the acquired locks would be released in the retry.
	 *
	 * So, we need to restart the whole transaction if we want to retry kDeadlock/kAborted errors.
	 * And we can only do that if YBIsDataSent() is false because we can't retry the transaction if
	 * some data has already been sent to the external client as part of this transaction.
	 */
	if (IsYBReadCommitted() && (is_deadlock_error || is_aborted_error) && YBIsDataSent())
	{
		const char* retry_err = "";
		retry_err = psprintf("%s %s %s",
				"query layer retry isn't possible, READ COMMITTED transaction was aborted",
				(is_deadlock_error ? "to break a deadlock cycle" : ""),
				"and some data was already sent to the user");

		edata->message = psprintf("%s (%s)", edata->message, retry_err);
		if (yb_debug_log_internal_restarts)
			elog(LOG, "%s", retry_err);
		return false;
	}

	/*
	 * In REPEATABLE READ and SERIALIZABLE isolation levels, retrying involves restarting the whole
	 * transaction. So, we can only retry if no data has been sent to the external client as part of
	 * the current transaction.
	 *
	 * In READ COMMITTED, we can perform retries even if data has been sent as part of the txn, but
	 * not if data has been sent as part of the current query. This is because in RC, we just have
	 * to retry the query, and not the whole transaction.
	 */
	if ((!IsYBReadCommitted() && YBIsDataSent()) ||
			(IsYBReadCommitted() && YBIsDataSentForCurrQuery()))
	{
		const char* retry_err = "query layer retry isn't possible because data was already sent, "
				"if this is the read committed isolation (or) the first statement in repeatable read/ "
				"serializable isolation transaction, consider increasing the tserver gflag "
				"ysql_output_buffer_size";
		edata->message = psprintf("%s (%s)", edata->message, retry_err);
		if (yb_debug_log_internal_restarts)
			elog(LOG, "%s", retry_err);
		return false;
	}

	/*
	 * In batch processing using the extended query protocol, YBIsDataSent() /
	 * YBIsDataSentForCurrQuery() can be false even if earlier mutation statements have been
	 * processed. Unless it's the first statement of the batch, we cannot retry the current
	 * statement. If we do, we will lose the mutations from earlier statements (this is true
	 * irrespective of whether the retry is done by restarting the whole transaaction in RR/SR
	 * isolation, or by rolling back to the previous internal savepoint in RC).
	 */
	if (YbIsBatchedExecution() && (GetCurrentCommandId(false) > FirstCommandId))
	{
		const char* retry_err = "query layer retries aren't supported when executing non-first "
				"statement in batch, will be unable to replay earlier commands";
		edata->message = psprintf("%s (%s)", edata->message, retry_err);
		if (yb_debug_log_internal_restarts)
			elog(LOG, "%s", retry_err);
		return false;
	}

	if (attempt >= yb_max_query_layer_retries)
	{
		const char* retry_err = psprintf("yb_max_query_layer_retries set to %d are exhausted",
										 yb_max_query_layer_retries);
		edata->message = psprintf("%s (%s)", edata->message, retry_err);
		if (yb_debug_log_internal_restarts)
			elog(LOG, "%s", retry_err);
		return false;
	}

	if (!retry_data)
	{
		if (yb_debug_log_internal_restarts)
			elog(LOG, "query layer retry isn't possible, retry data is missing");
		return false;
	}

	/* can only restart SELECT queries */
	if (!retry_data->query_string)
	{
		if (yb_debug_log_internal_restarts)
			elog(LOG, "query layer retry isn't possible, query string is missing");
		return false;
	}

	command_tag = retry_data->command_tag;

	/*
	 * If we're executing a prepared statement, we're interested in the command
	 * tag of the underlying statement.
	 */
	if (command_tag == CMDTAG_EXECUTE)
	{
		List* parsetree_list =
			yb_parse_query_silently(retry_data->query_string);
		if (list_length(parsetree_list) == 0)
			return false;
		ExecuteStmt* execute_stmt =
			(ExecuteStmt*) linitial_node(RawStmt, parsetree_list)->stmt;
		PreparedStatement* prepared_stmt =
			FetchPreparedStatement(execute_stmt->name, false /* throwError */);
		if (prepared_stmt == NULL)
			return false;
		command_tag = prepared_stmt->plansource->commandTag;
	}

	bool is_read = command_tag == CMDTAG_SELECT;
	bool is_dml  = YBIsDmlCommandTag(command_tag);

	if (IsYBReadCommitted())
	{
		if (YBGetDdlNestingLevel() != 0) {
			const char* retry_err = "query layer retries aren't supported for DDLs inside a "
					"read committed isolation transaction block";
			edata->message = psprintf("%s (%s)", edata->message, retry_err);
			if (yb_debug_log_internal_restarts)
				elog(LOG, "%s", retry_err);
			return false;
		}
	}
	else if (!(is_read || is_dml))
	{
		// if !read committed, we only support retries with SELECT/UPDATE/INSERT/DELETE. There are other
		// statements that might result in a kReadRestart/kConflict like CREATE INDEX. We don't retry
		// those as of now.
		if (yb_debug_log_internal_restarts)
			elog(LOG, "query layer retries not possible because statement isn't one of "
					 "SELECT/UPDATE/INSERT/DELETE");
		return false;
	}

	return true;
}

/*
 * Collect data necessary for yb_attempt_to_retry_on_error invocation.
 */
static YBQueryRetryData *
yb_collect_portal_restart_data(const char *portal_name)
{
	Portal portal = GetPortalByName(portal_name);
	Assert(portal);
	Assert(!strcmp(portal->name, portal_name));

	YBQueryRetryData *result = palloc(sizeof(YBQueryRetryData));
	result->portal_name  = portal->name;
	result->query_string = portal->sourceText;
	result->command_tag  = portal->commandTag;
	return result;
}

/*
 * The next two functions yb_clear_portal_before_restart and
 * yb_restart_portal_after_clear performs portal restart and prepares it for
 * re-execution.
 *
 * This allows us to reuse portal's MemoryContext, which contains,
 * among other things, bound variables.
 * Some of them might be pointers to a memory within the same context
 * (e.g. arrays), so it's important to preserve the context as-is instead of
 * e.g. copying it into a fresh portal.
 *
 * Our goal is to emulate what would PortalDrop + CreatePortal do,
 * but instead of actually destroying/creating a portal, we're going to
 * reuse an existing one.
 *
 * The yb_clear_portal_before_restart function is a selective copy-paste from
 * PortalDrop routine.
 * Original comments are preserved, even though some of the described use cases
 * are not applicable here.
 */
static void
yb_clear_portal_before_restart(Portal portal)
{
	Assert(PointerIsValid(portal));
	Assert(portal->status == PORTAL_FAILED);
	if (yb_debug_log_internal_restarts)
		elog(LOG, "Restarting portal %s for retry", portal->name);

	/*
	 * Allow portalcmds.c to clean up the state it knows about, in particular
	 * shutting down the executor if still active.  This step potentially runs
	 * user-defined code so failure has to be expected.  It's the cleanup
	 * hook's responsibility to not try to do that more than once, in the case
	 * that failure occurs and then we come back to drop the portal again
	 * during transaction abort.
	 *
	 * Note: in most paths of control, this will have been done already in
	 * MarkPortalDone or MarkPortalFailed.  We're just making sure.
	 */
	if (PointerIsValid(portal->cleanup))
	{
		portal->cleanup(portal);
		portal->cleanup = NULL;
	}

	/*
	 * If portal has a snapshot protecting its data, release that.  This needs
	 * a little care since the registration will be attached to the portal's
	 * resowner; if the portal failed, we will already have released the
	 * resowner (and the snapshot) during transaction abort.
	 */
	if (portal->holdSnapshot)
	{
		if (portal->resowner)
			UnregisterSnapshotFromOwner(portal->holdSnapshot,
										portal->resowner);
		portal->holdSnapshot = NULL;
	}

	/*
	 * Release any resources still attached to the portal.  There are several
	 * cases being covered here:
	 *
	 * Top transaction commit (indicated by isTopCommit): normally we should
	 * do nothing here and let the regular end-of-transaction resource
	 * releasing mechanism handle these resources too.  However, if we have a
	 * FAILED portal (eg, a cursor that got an error), we'd better clean up
	 * its resources to avoid resource-leakage warning messages.
	 *
	 * Sub transaction commit: never comes here at all, since we don't kill
	 * any portals in AtSubCommit_Portals().
	 *
	 * Main or sub transaction abort: we will do nothing here because
	 * portal->resowner was already set NULL; the resources were already
	 * cleaned up in transaction abort.
	 *
	 * Ordinary portal drop: must release resources.  However, if the portal
	 * is not FAILED then we do not release its locks.  The locks become the
	 * responsibility of the transaction's ResourceOwner (since it is the
	 * parent of the portal's owner) and will be released when the transaction
	 * eventually ends.
	 */
	if (portal->resowner)
	{
		bool		isCommit = (portal->status != PORTAL_FAILED);

		ResourceOwnerRelease(portal->resowner,
							 RESOURCE_RELEASE_BEFORE_LOCKS,
							 isCommit, false);
		ResourceOwnerRelease(portal->resowner,
							 RESOURCE_RELEASE_LOCKS,
							 isCommit, false);
		ResourceOwnerRelease(portal->resowner,
							 RESOURCE_RELEASE_AFTER_LOCKS,
							 isCommit, false);
		ResourceOwnerDelete(portal->resowner);
	}
	portal->resowner = NULL;

	/*
	 * Delete tuplestore if present.  We should do this even under error
	 * conditions; since the tuplestore would have been using cross-
	 * transaction storage, its temp files need to be explicitly deleted.
	 */
	if (portal->holdStore)
	{
		MemoryContext oldcontext;

		oldcontext = MemoryContextSwitchTo(portal->holdContext);
		tuplestore_end(portal->holdStore);
		MemoryContextSwitchTo(oldcontext);
		portal->holdStore = NULL;
	}

	/* delete tuplestore storage, if any */
	if (portal->holdContext)
	{
		MemoryContextDelete(portal->holdContext);
		portal->holdContext = NULL;
	}

	/*
	 * Fully detach portal from transaction to keep it alive in case of
	 * transaction restart
	 */
	portal->createSubid = InvalidSubTransactionId;
	portal->activeSubid = InvalidSubTransactionId;
}

/*
 * The yb_restart_portal_after_clear is a selective copy-paste from CreatePortal
 * routine.
 */
static void
yb_restart_portal_after_clear(Portal portal)
{
	Assert(PointerIsValid(portal));

	/* create a resource owner for the portal */
	portal->resowner = ResourceOwnerCreate(CurTransactionResourceOwner,
										   "Portal");

	/* initialize portal fields that don't start off zero */
	portal->status = PORTAL_NEW;
	portal->cleanup = PortalCleanup;
	portal->createSubid = GetCurrentSubTransactionId();
	portal->activeSubid = portal->createSubid;
	portal->strategy = PORTAL_MULTI_QUERY;
	portal->cursorOptions = CURSOR_OPT_NO_SCROLL;
	portal->atStart = true;
	portal->atEnd = true;		/* disallow fetches until query is set */
	portal->visible = true;
	portal->creation_time = GetCurrentStatementStartTimestamp();

	/* -------------------------------------------------------------------------
	 * YB NOTE:
	 *
	 * Now that our portal looks like a fresh one, time to prepare and start it.
	 */

	/*
	 * No need for GetCachedPlan + PortalDefineQuery routine, everything is in
	 * place already.
	 */
	portal->status = PORTAL_DEFINED;
	PortalStart(portal, portal->portalParams, 0 /* eflags */, InvalidSnapshot);

	/* no need to call PortalSetResultFormat either - formats array is already set */
}

static long
yb_get_sleep_usecs_on_txn_conflict(int attempt) {
	/* Use exponential backoff to calculate the sleep duration. */
	if (!*YBCGetGFlags()->ysql_sleep_before_retry_on_txn_conflict)
		return 0;

	/*
	 * While the guc variables are being changed, RetryMaxBackoffMsecs can be
	 * smaller than RetryMinBackoffMsecs. Return RetryMaxBackoffMsecs in this
	 * case.
	 */
	if (RetryMaxBackoffMsecs <= RetryMinBackoffMsecs)
		return RetryMaxBackoffMsecs;

	if (RetryMaxBackoffMsecs == 0 || RetryMinBackoffMsecs == 0)
		return 0;

	return (long) (PowerWithUpperLimit(RetryBackoffMultiplier, attempt,
				1.0 * RetryMaxBackoffMsecs / RetryMinBackoffMsecs) *
			RetryMinBackoffMsecs * 1000);
}

static void
yb_maybe_sleep_on_txn_conflict(int attempt)
{
	if (YBIsWaitQueueEnabled())
		return;

	/*
	 * If transactions are leveraging the wait queue based infrastructure for
	 * blocking semantics on conflicts, they need not sleep with exponential
	 * backoff between retries. The wait queues ensure that the transaction's
	 * read/ write rpc which faced a kConflict error is unblocked only when all
	 * conflicting transactions have ended (either committed or aborted).
	 */
	pgstat_report_wait_start(WAIT_EVENT_YB_TXN_CONFLICT_BACKOFF);
	pg_usleep(yb_get_sleep_usecs_on_txn_conflict(attempt));
	pgstat_report_wait_end();
}

static void
yb_restart_current_stmt(int attempt, bool is_read_restart)
{
	if (yb_debug_log_internal_restarts)
		elog(LOG, "Rolling back and retrying current statement. Sub-txn id: %d to %d"
			, GetCurrentSubTransactionId(), GetCurrentSubTransactionId() + 1);

	// TODO(Piyush): Perform pg_session_->InvalidateForeignKeyReferenceCache()
	// and create tests that would fail without this.

	/*
	 * Rollback to the savepoint that was started in StartTransactionCommand()
	 * for READ COMMITTED isolation.
	 */

	// TODO(read committed): remove this once the feature is GA
	Assert(!strcmp(GetCurrentTransactionName(),
				   YB_READ_COMMITTED_INTERNAL_SUB_TXN_NAME));
	RollbackAndReleaseCurrentSubTransaction();
	YbBeginInternalSubTransactionForReadCommittedStatement();

	if (is_read_restart)
	{
		HandleYBStatus(YBCPgRestartReadPoint());
	}
	else
	{
		HandleYBStatus(YBCPgResetTransactionReadPoint());
		yb_maybe_sleep_on_txn_conflict(attempt);
	}
}

static void
yb_restart_transaction(int attempt, bool is_read_restart)
{
	if (yb_debug_log_internal_restarts)
		elog(LOG, "Restarting transaction");

	/*
	 * The txn might or might not have performed writes. Reset the state in
	 * either case to avoid checking/tracking if a write could have been
	 * performed.
	 */
	YBCRestartWriteTransaction();

	if (is_read_restart)
	{
		YBCRestartTransaction();
	}
	else
	{
		/*
		 * Retry the transaction by recreating the YB state for the transaction without
		 * changing/ resetting the Pg-side transaction. This call preserves the priority of the current
		 * YB transaction so that when we retry, we re-use the same priority for the new YB transaction.
		 * Note that priorities are only used for Fail-on-Conflict concurrency control mode.
		 */
		YBCRecreateTransaction();

		if (IsTransactionBlock() && IsYBReadCommitted()) {
			/*
			 * Each statement in a read committed transaction block (i.e., after BEGIN) registers an
			 * internal sub-transaction to be able to undo and retry the statement for kConflict and
			 * kReadRestart errors (see yb_restart_current_stmt()). This registration is done in
			 * YBStartTransactionCommandInternal(). However, since we are retrying by surgically resetting
			 * just the YB-side transaction state without resetting and retriggering the Pg-side
			 * transaction state machine changes, we should explicitly make the sub-transaction changes
			 * on Pg side i.e., by registsring a new internal sub transaction.
			 */

			// TODO(read committed): remove the below check once the feature is GA
			Assert(!strcmp(GetCurrentTransactionName(),
						   YB_READ_COMMITTED_INTERNAL_SUB_TXN_NAME));
			RollbackAndReleaseCurrentSubTransaction();

			/*
			 * This creates a new PG side sub-txn and increments the sub-txn id.
			 *
			 * NOTE: this will result in a situation where the new YB side distributed transaction will
			 * start with a sub transaction id that isn't 2 (which is the intial id for the internal
			 * savepoint registered before the first statement in any RC transaction block). The id could
			 * be much higher depending on how many statement level retries have already been done so far
			 * using the same YB transaction (i.e., via yb_restart_current_stmt()).
			 */
			YbBeginInternalSubTransactionForReadCommittedStatement();
		}

		yb_maybe_sleep_on_txn_conflict(attempt);
	}
}

static void
yb_prepare_transaction_for_retry(int attempt, bool is_read_restart, bool statement_retry_possible)
{
	if (IsYBReadCommitted() && IsTransactionBlock() && statement_retry_possible)
	{
		/*
		 * If using RC, we can retry the failed statement by just undoing it
		 * instead of restarting the whole transaction. This is not possible if the
		 * transaction is already aborted which happens in case of a
		 * kAbort/kDeadlock error.
		 *
		 * The undo is done by rolling back to the internal savepoint registered
		 * before executing any statement in a RC transaction block.
		 */
		yb_restart_current_stmt(attempt, is_read_restart);
	}
	else
	{
		/*
		 * In this case the txn is restarted, which can be done since we haven't
		 * executed even the first statement fully and no data has been sent to
		 * the client.
		 */
		yb_restart_transaction(attempt, is_read_restart);
	}
}

static void
yb_perform_retry_on_error(int attempt, uint16_t txn_errcode,
						  const char *portal_name)
{
	if (yb_debug_log_internal_restarts)
		ereport(LOG, (errmsg("performing query layer retry, attempt number %d", attempt)));

	const bool is_read_restart = YBCIsRestartReadError(txn_errcode);
	const bool is_conflict_error = YBCIsTxnConflictError(txn_errcode);
	const bool is_deadlock_error = YBCIsTxnDeadlockError(txn_errcode);
	const bool is_aborted_error = YBCIsTxnAbortedError(txn_errcode);
	if (!(is_read_restart || is_conflict_error || is_deadlock_error || is_aborted_error))
	{
		Assert(false);
		elog(ERROR, "unexpected txn error code: %d", txn_errcode);
	}

	/*
	 * If in parallel mode, destroy parallel contexts.
	 * It is important to do before portal's the resource owners cleanup,
	 * because they free DSM blocks they own, leaving dangling references
	 * in the parallel contexts.
	 */
	if (IsInParallelMode())
		YbClearParallelContexts();

	Portal portal = portal_name ? GetPortalByName(portal_name) : NULL;
	if (portal)
	{
		yb_clear_portal_before_restart(portal);
		/*
		 * Portal is fully detached from current transaction now. It is
		 * necessary to manually drop it in case of error happening prior to
		 * the call of the yb_restart_portal_after_clear function.
		 */
	}

	PG_TRY();
	{
		yb_prepare_transaction_for_retry(attempt, is_read_restart,
				is_conflict_error || is_read_restart /* statement_retry_possible */);
	}
	PG_CATCH();
	{
		/*
		 * Paranoid check that portal was not removed by the transaction
		 * restart routines.
		 */
		Assert(!portal || GetPortalByName(portal_name) == portal);

		if (portal && !portal->autoHeld && !portal->portalPinned)
			PortalDrop(portal, /* isTopCommit */ false);

		PG_RE_THROW();
	}
	PG_END_TRY();

	if (portal)
	{
		/*
		 * Paranoid check that portal was not removed by the transaction
		 * restart routines.
		 */
		Assert(GetPortalByName(portal_name) == portal);
		yb_restart_portal_after_clear(portal);
	}
	YBRestoreOutputBufferPosition();
	/* Presence of triggers pushes additional snapshots. Pop all of them. */
	PopAllActiveSnapshots();
}

/*
 * Process an error that happened during execution with expected expected
 * retriable errors. Prepares the re-execution if an error is restartable,
 * otherwise - rethrows the error.
 */
static void
yb_attempt_to_retry_on_error(int attempt, const YBQueryRetryData *retry_data,
							 MemoryContext exec_context)
{
	/*
	 * Switch the context back to the original one when server started
	 * processing user request.
	 */
	MemoryContextSwitchTo(exec_context);
	ErrorData *edata = CopyErrorData();

	if (yb_is_retry_possible(edata, attempt, retry_data))
	{
		FlushErrorState();
		yb_perform_retry_on_error(attempt, edata->yb_txn_errcode,
								  retry_data->portal_name);
	} else {
		/* if we shouldn't restart - propagate the error */
		ReThrowError(edata);
	}
}

typedef void(*YBFunctor)(const void*);

static void
yb_exec_query_wrapper_one_attempt(MemoryContext exec_context,
								  const YBQueryRetryData *retry_data,
								  YBFunctor functor,
								  const void *functor_context,
								  int attempt,
								  bool *retry)
{
	elog(DEBUG2, "yb_exec_query_wrapper attempt %d for %s", attempt, retry_data->query_string);
	YBSaveOutputBufferPosition(!yb_is_begin_transaction(retry_data->command_tag));
	PG_TRY();
	{
		(*functor)(functor_context);
		/*
			* Stop retrying if successful. Note, break or return could not be
			* used here, they would prevent PG_END_TRY();
			*/
		*retry = false;
	}
	PG_CATCH();
	{
		YBResetOperationsBuffering();
		yb_attempt_to_retry_on_error(attempt, retry_data, exec_context);
	}
	PG_END_TRY();
}

static void
yb_exec_query_wrapper(MemoryContext exec_context,
					  const YBQueryRetryData *retry_data,
					  YBFunctor functor,
					  const void *functor_context)
{
	bool retry = true;
	for (int attempt = 0; retry; ++attempt)
	{
		yb_exec_query_wrapper_one_attempt(exec_context, retry_data, functor,
										  functor_context, attempt, &retry);
	}
}

static void
yb_exec_simple_query_impl(const void *query_string)
{
	exec_simple_query((const char *)query_string);
}

/*
 * Wraps exec_simple_query, attempting to transparently do restarts when possible.
 * Accepts execution memory context to revert to in case of an error.
 */
static void
yb_exec_simple_query(const char *query_string, MemoryContext exec_context)
{
	YBQueryRetryData retry_data  = {
		.portal_name  = NULL,
		.query_string = query_string,
		.command_tag  = YbParseCommandTag(query_string)
	};
	yb_exec_query_wrapper(exec_context, &retry_data,
						  &yb_exec_simple_query_impl, query_string);

	/*
	 * Fetch the updated session execution stats at the end of each query, so
	 * that stats don't accumulate across queries. The stats collected here
	 * typically correspond to completed flushes, reads associated with triggers
	 * etc.
	 */
	YbRefreshSessionStatsDuringExecution();
}

typedef struct YBExecuteMessageFunctorContext
{
	const char *portal_name;
	long max_rows;
} YBExecuteMessageFunctorContext;

static void
yb_exec_execute_message_impl(const void* raw_ctx)
{
	const YBExecuteMessageFunctorContext* ctx = (const YBExecuteMessageFunctorContext*)(raw_ctx);
	exec_execute_message(ctx->portal_name, ctx->max_rows);
}

/*
 * Wraps exec_execute_message, attempting to transparently do restarts when possible.
 * Accepts execution memory context to revert to in case of an error.
 */
static void
yb_exec_execute_message(long max_rows,
						const YBQueryRetryData *restart_data,
						MemoryContext exec_context)
{
	YBExecuteMessageFunctorContext ctx = {
		.portal_name = restart_data->portal_name,
		.max_rows = max_rows
	};
	yb_exec_query_wrapper(exec_context, restart_data,
						  &yb_exec_execute_message_impl, &ctx);

	/*
	 * Fetch the updated session execution stats at the end of each query, so
	 * that stats don't accumulate across queries. The stats collected here
	 * typically correspond to completed flushes, reads associated with triggers
	 * etc.
	 */
	YbRefreshSessionStatsDuringExecution();
}

static void yb_report_cache_version_restart(const char* query, ErrorData *edata)
{
	ereport(LOG,
			(errmsg("restarting statement due to catalog version mismatch"),
			 errdetail("Query: %s\nError: %s",
					   query,
					   edata->message)));
}

/*
 * PostgresSingleUserMain
 *     Entry point for single user mode. argc/argv are the command line
 *     arguments to be used.
 *
 * Performs single user specific setup then calls PostgresMain() to actually
 * process queries. Single user mode specific setup should go here, rather
 * than PostgresMain() or InitPostgres() when reasonably possible.
 */
void
PostgresSingleUserMain(int argc, char *argv[],
					   const char *username)
{
	const char *dbname = NULL;

	Assert(!IsUnderPostmaster);

	/* Initialize startup process environment. */
	InitStandaloneProcess(argv[0]);

	/*
	 * Set default values for command-line options.
	 */
	InitializeGUCOptions();

	/*
	 * Parse command-line options.
	 */
	process_postgres_switches(argc, argv, PGC_POSTMASTER, &dbname);

	/* Must have gotten a database name, or have a default (the username) */
	if (dbname == NULL)
	{
		dbname = username;
		if (dbname == NULL)
			ereport(FATAL,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("%s: no database nor user name specified",
							progname)));
	}

	/* Acquire configuration parameters */
	if (!SelectConfigFiles(userDoption, progname))
		proc_exit(1);

	/*
	 * Validate we have been given a reasonable-looking DataDir and change
	 * into it.
	 */
	checkDataDir();
	ChangeToDataDir();

	/*
	 * Create lockfile for data directory.
	 */
	CreateDataDirLockFile(false);

	/* read control file (error checking and contains config ) */
	LocalProcessControlFile(false);

	/*
	 * process any libraries that should be preloaded at postmaster start
	 */
	process_shared_preload_libraries();

	/* Initialize MaxBackends */
	InitializeMaxBackends();

	/*
	 * Give preloaded libraries a chance to request additional shared memory.
	 */
	process_shmem_requests();

	/*
	 * Now that loadable modules have had their chance to request additional
	 * shared memory, determine the value of any runtime-computed GUCs that
	 * depend on the amount of shared memory required.
	 */
	InitializeShmemGUCs();

	/*
	 * Now that modules have been loaded, we can process any custom resource
	 * managers specified in the wal_consistency_checking GUC.
	 */
	InitializeWalConsistencyChecking();

	CreateSharedMemoryAndSemaphores();

	/*
	 * Remember stand-alone backend startup time,roughly at the same point
	 * during startup that postmaster does so.
	 */
	PgStartTime = GetCurrentTimestamp();

	/*
	 * Create a per-backend PGPROC struct in shared memory. We must do this
	 * before we can use LWLocks.
	 */
	InitProcess();

	/*
	 * Now that sufficient infrastructure has been initialized, PostgresMain()
	 * can do the rest.
	 */
	PostgresMain(dbname, username);
}


/* ----------------------------------------------------------------
 * PostgresMain
 *	   postgres main loop -- all backends, interactive or otherwise loop here
 *
 * dbname is the name of the database to connect to, username is the
 * PostgreSQL user name to be used for the session.
 *
 * NB: Single user mode specific setup should go to PostgresSingleUserMain()
 * if reasonably possible.
 * ----------------------------------------------------------------
 */
void
PostgresMain(const char *dbname, const char *username)
{
	int			firstchar;
	StringInfoData input_message;
	sigjmp_buf	local_sigjmp_buf;
	volatile bool send_ready_for_query = true;
	bool		idle_in_transaction_timeout_enabled = false;
	bool		idle_session_timeout_enabled = false;

	AssertArg(dbname != NULL);
	AssertArg(username != NULL);

	SetProcessingMode(InitProcessing);

	/* TODO(neil)
	 * Once we have our system DB, remove the following code. It is a hack to help us getting
	 * by for now.
	 */
	if (strcmp(dbname, "template0") == 0 || strcmp(dbname, "template1") == 0)
		YbSetConnectedToTemplateDb();

	/*
	 * Set up signal handlers.  (InitPostmasterChild or InitStandaloneProcess
	 * has already set up BlockSig and made that the active signal mask.)
	 *
	 * Note that postmaster blocked all signals before forking child process,
	 * so there is no race condition whereby we might receive a signal before
	 * we have set up the handler.
	 *
	 * Also note: it's best not to use any signals that are SIG_IGNored in the
	 * postmaster.  If such a signal arrives before we are able to change the
	 * handler to non-SIG_IGN, it'll get dropped.  Instead, make a dummy
	 * handler in the postmaster to reserve the signal. (Of course, this isn't
	 * an issue for signals that are locally generated, such as SIGALRM and
	 * SIGPIPE.)
	 */
	if (am_walsender)
		WalSndSignals();
	else
	{
		pqsignal(SIGHUP, SignalHandlerForConfigReload);
		pqsignal(SIGINT, StatementCancelHandler);	/* cancel current query */
		pqsignal(SIGTERM, die); /* cancel current query and exit */

		/*
		 * In a postmaster child backend, replace SignalHandlerForCrashExit
		 * with quickdie, so we can tell the client we're dying.
		 *
		 * In a standalone backend, SIGQUIT can be generated from the keyboard
		 * easily, while SIGTERM cannot, so we make both signals do die()
		 * rather than quickdie().
		 */
		if (IsUnderPostmaster)
			pqsignal(SIGQUIT, quickdie);	/* hard crash time */
		else
			pqsignal(SIGQUIT, die); /* cancel current query and exit */
		InitializeTimeouts();	/* establishes SIGALRM handler */

		/*
		 * Ignore failure to write to frontend. Note: if frontend closes
		 * connection, we will notice it and exit cleanly when control next
		 * returns to outer loop.  This seems safer than forcing exit in the
		 * midst of output during who-knows-what operation...
		 */
		pqsignal(SIGPIPE, SIG_IGN);
		pqsignal(SIGUSR1, procsignal_sigusr1_handler);
		pqsignal(SIGUSR2, SIG_IGN);
		pqsignal(SIGFPE, FloatExceptionHandler);

		/*
		 * Reset some signals that are accepted by postmaster but not by
		 * backend
		 */
		pqsignal(SIGCHLD, SIG_DFL); /* system() requires this on some
									 * platforms */
	}

	/* Early initialization */
	BaseInit();

	/* We need to allow SIGINT, etc during the initial transaction */
	PG_SETMASK(&UnBlockSig);

	/*
	 * General initialization.
	 *
	 * NOTE: if you are tempted to add code in this vicinity, consider putting
	 * it inside InitPostgres() instead.  In particular, anything that
	 * involves database access should be there, not here.
	 */
	InitPostgres(dbname, InvalidOid,	/* database to connect to */
				 username, InvalidOid,	/* role to connect as */
				 !am_walsender, /* honor session_preload_libraries? */
				 false,			/* don't ignore datallowconn */
				 NULL,			/* no out_dbname */
				 NULL);			/* session id */

	/*
	 * If the PostmasterContext is still around, recycle the space; we don't
	 * need it anymore after InitPostgres completes.  Note this does not trash
	 * *MyProcPort, because ConnCreate() allocated that space with malloc()
	 * ... else we'd need to copy the Port data first.  Also, subsidiary data
	 * such as the username isn't lost either; see ProcessStartupPacket().
	 * YB note: PostmasterContext is required in case of connections created by
	 * Ysql Connection Manager for `Authentication Passthrough`, so it shouldn't
	 * be deleted in this case.
	 */
	if (PostmasterContext && !YbIsClientYsqlConnMgr())
	{
		MemoryContextDelete(PostmasterContext);
		PostmasterContext = NULL;
	}

	SetProcessingMode(NormalProcessing);

	/*
	 * Now all GUC states are fully set up.  Report them to client if
	 * appropriate.
	 */
	BeginReportingGUCOptions();

	/*
	 * The authentication backend is only responsible for authentication and
	 * sending initial GUC options.
	 */
	if (yb_is_auth_backend)
	{
		/*
		 * Send a dummy READY_FOR_QUERY packet to the connection manager to
		 * indicate that the auth backend is done.
		 */
		ReadyForQuery(whereToSendOutput);

		/*
		 * Reset whereToSendOutput to prevent ereport from attempting
		 * to send any more messages to client.
		 */
		if (whereToSendOutput == DestRemote)
			whereToSendOutput = DestNone;

		proc_exit(0);
	}

	/*
	 * Also set up handler to log session end; we have to wait till now to be
	 * sure Log_disconnections has its final value.
	 */
	if (IsUnderPostmaster && Log_disconnections)
		on_proc_exit(log_disconnections, 0);

	pgstat_report_connect(MyDatabaseId);

	/* Perform initialization specific to a WAL sender process. */
	if (am_walsender)
		InitWalSender();

	/*
	 * Send this backend's cancellation info to the frontend.
	 */
	if (whereToSendOutput == DestRemote)
	{
		StringInfoData buf;

		pq_beginmessage(&buf, 'K');
		pq_sendint32(&buf, (int32) MyProcPid);
		pq_sendint32(&buf, (int32) MyCancelKey);
		pq_endmessage(&buf);
		/* Need not flush since ReadyForQuery will do it. */
	}

	/* Welcome banner for standalone case */
	if (whereToSendOutput == DestDebug)
		printf("\nPostgreSQL stand-alone backend %s\n", PG_VERSION);

	/*
	 * Create the memory context we will use in the main loop.
	 *
	 * MessageContext is reset once per iteration of the main loop, ie, upon
	 * completion of processing of each command message from the client.
	 */
	MessageContext = AllocSetContextCreate(TopMemoryContext,
										   "MessageContext",
										   ALLOCSET_DEFAULT_SIZES);

	/*
	 * Create memory context and buffer used for RowDescription messages. As
	 * SendRowDescriptionMessage(), via exec_describe_statement_message(), is
	 * frequently executed for ever single statement, we don't want to
	 * allocate a separate buffer every time.
	 */
	row_description_context = AllocSetContextCreate(TopMemoryContext,
													"RowDescriptionContext",
													ALLOCSET_DEFAULT_SIZES);
	MemoryContextSwitchTo(row_description_context);
	initStringInfo(&row_description_buf);
	MemoryContextSwitchTo(TopMemoryContext);

	/*
	 * POSTGRES main processing loop begins here
	 *
	 * If an exception is encountered, processing resumes here so we abort the
	 * current transaction and start a new one.
	 *
	 * You might wonder why this isn't coded as an infinite loop around a
	 * PG_TRY construct.  The reason is that this is the bottom of the
	 * exception stack, and so with PG_TRY there would be no exception handler
	 * in force at all during the CATCH part.  By leaving the outermost setjmp
	 * always active, we have at least some chance of recovering from an error
	 * during error recovery.  (If we get into an infinite loop thereby, it
	 * will soon be stopped by overflow of elog.c's internal state stack.)
	 *
	 * Note that we use sigsetjmp(..., 1), so that this function's signal mask
	 * (to wit, UnBlockSig) will be restored when longjmp'ing to here.  This
	 * is essential in case we longjmp'd out of a signal handler on a platform
	 * where that leaves the signal blocked.  It's not redundant with the
	 * unblock in AbortTransaction() because the latter is only called if we
	 * were inside a transaction.
	 */

	if (sigsetjmp(local_sigjmp_buf, 1) != 0)
	{
		/*
		 * NOTE: if you are tempted to add more code in this if-block,
		 * consider the high probability that it should be in
		 * AbortTransaction() instead.  The only stuff done directly here
		 * should be stuff that is guaranteed to apply *only* for outer-level
		 * error recovery, such as adjusting the FE/BE protocol status.
		 */

		/* Since not using PG_TRY, must reset error stack by hand */
		error_context_stack = NULL;

		/* Prevent interrupts while cleaning up */
		HOLD_INTERRUPTS();

		/*
		 * Forget any pending QueryCancel request, since we're returning to
		 * the idle loop anyway, and cancel any active timeout requests.  (In
		 * future we might want to allow some timeout requests to survive, but
		 * at minimum it'd be necessary to do reschedule_timeouts(), in case
		 * we got here because of a query cancel interrupting the SIGALRM
		 * interrupt handler.)	Note in particular that we must clear the
		 * statement and lock timeout indicators, to prevent any future plain
		 * query cancels from being misreported as timeouts in case we're
		 * forgetting a timeout cancel.
		 */
		disable_all_timeouts(false);
		QueryCancelPending = false; /* second to avoid race condition */

		/* Not reading from the client anymore. */
		DoingCommandRead = false;

		/* Make sure libpq is in a good state */
		pq_comm_reset();

		/* Report the error to the client and/or server log */
		EmitErrorReport();

		/*
		 * Make sure debug_query_string gets reset before we possibly clobber
		 * the storage it points at.
		 */
		debug_query_string = NULL;

		/*
		 * Abort the current transaction in order to recover.
		 */
		AbortCurrentTransaction();

		if (am_walsender)
			WalSndErrorCleanup();

		PortalErrorCleanup();

		/*
		 * We can't release replication slots inside AbortTransaction() as we
		 * need to be able to start and abort transactions while having a slot
		 * acquired. But we never need to hold them across top level errors,
		 * so releasing here is fine. There also is a before_shmem_exit()
		 * callback ensuring correct cleanup on FATAL errors.
		 */
		if (MyReplicationSlot != NULL)
			ReplicationSlotRelease();

		/* We also want to cleanup temporary slots on error. */
		ReplicationSlotCleanup();

		jit_reset_after_error();

		/*
		 * Now return to normal top-level context and clear ErrorContext for
		 * next time.
		 */
		MemoryContextSwitchTo(TopMemoryContext);
		FlushErrorState();

		/*
		 * If we were handling an extended-query-protocol message, initiate
		 * skip till next Sync.  This also causes us not to issue
		 * ReadyForQuery (until we get Sync).
		 */
		if (doing_extended_query_message)
			ignore_till_sync = true;

		/* We don't have a transaction command open anymore */
		xact_started = false;

		/*
		 * If an error occurred while we were reading a message from the
		 * client, we have potentially lost track of where the previous
		 * message ends and the next one begins.  Even though we have
		 * otherwise recovered from the error, we cannot safely read any more
		 * messages from the client, so there isn't much we can do with the
		 * connection anymore.
		 */
		if (pq_is_reading_msg())
			ereport(FATAL,
					(errcode(ERRCODE_PROTOCOL_VIOLATION),
					 errmsg("terminating connection because protocol synchronization was lost")));

		/* Now we can allow interrupts again */
		RESUME_INTERRUPTS();
	}

	/* We can now handle ereport(ERROR) */
	PG_exception_stack = &local_sigjmp_buf;

	if (!ignore_till_sync)
		send_ready_for_query = true;	/* initially, or after error */

	/*
	 * Non-error queries loop here.
	 */

	for (;;)
	{
		/*
		 * At top of loop, reset extended-query-message flag, so that any
		 * errors encountered in "idle" state don't provoke skip.
		 */
		doing_extended_query_message = false;

		/*
		 * Release storage left over from prior query cycle, and create a new
		 * query input buffer in the cleared MessageContext.
		 */
		MemoryContextSwitchTo(MessageContext);
		MemoryContextResetAndDeleteChildren(MessageContext);

		initStringInfo(&input_message);

		/*
		 * Also consider releasing our catalog snapshot if any, so that it's
		 * not preventing advance of global xmin while we wait for the client.
		 */
		InvalidateCatalogSnapshotConditionally();

		/*
		 * (1) If we've reached idle state, tell the frontend we're ready for
		 * a new query.
		 *
		 * Note: this includes fflush()'ing the last of the prior output.
		 *
		 * This is also a good time to flush out collected statistics to the
		 * cumulative stats system, and to update the PS stats display.  We
		 * avoid doing those every time through the message loop because it'd
		 * slow down processing of batched messages, and because we don't want
		 * to report uncommitted updates (that confuses autovacuum).  The
		 * notification processor wants a call too, if we are not in a
		 * transaction block.
		 *
		 * Also, if an idle timeout is enabled, start the timer for that.
		 */
		if (send_ready_for_query)
		{
			if (IsAbortedTransactionBlockState())
			{
				set_ps_display("idle in transaction (aborted)");
				pgstat_report_activity(STATE_IDLEINTRANSACTION_ABORTED, NULL);

				/* Start the idle-in-transaction timer */
				if (IdleInTransactionSessionTimeout > 0)
				{
					idle_in_transaction_timeout_enabled = true;
					enable_timeout_after(IDLE_IN_TRANSACTION_SESSION_TIMEOUT,
										 IdleInTransactionSessionTimeout);
				}
			}
			else if (IsTransactionOrTransactionBlock())
			{
				set_ps_display("idle in transaction");
				pgstat_report_activity(STATE_IDLEINTRANSACTION, NULL);

				/* Start the idle-in-transaction timer */
				if (IdleInTransactionSessionTimeout > 0)
				{
					idle_in_transaction_timeout_enabled = true;
					enable_timeout_after(IDLE_IN_TRANSACTION_SESSION_TIMEOUT,
										 IdleInTransactionSessionTimeout);
				}
			}
			else
			{
				long		stats_timeout;

				if (IsYugaByteEnabled() && yb_need_cache_refresh)
				{
					YBRefreshCache();
				}

				/*
				 * Process incoming notifies (including self-notifies), if
				 * any, and send relevant messages to the client.  Doing it
				 * here helps ensure stable behavior in tests: if any notifies
				 * were received during the just-finished transaction, they'll
				 * be seen by the client before ReadyForQuery is.
				 */
				if (notifyInterruptPending)
					ProcessNotifyInterrupt(false);

				/*
				 * Check if we need to report stats. If pgstat_report_stat()
				 * decides it's too soon to flush out pending stats / lock
				 * contention prevented reporting, it'll tell us when we
				 * should try to report stats again (so that stats updates
				 * aren't unduly delayed if the connection goes idle for a
				 * long time). We only enable the timeout if we don't already
				 * have a timeout in progress, because we don't disable the
				 * timeout below. enable_timeout_after() needs to determine
				 * the current timestamp, which can have a negative
				 * performance impact. That's OK because pgstat_report_stat()
				 * won't have us wake up sooner than a prior call.
				 */
				stats_timeout = pgstat_report_stat(false);
				if (stats_timeout > 0)
				{
					if (!get_timeout_active(IDLE_STATS_UPDATE_TIMEOUT))
						enable_timeout_after(IDLE_STATS_UPDATE_TIMEOUT,
											 stats_timeout);
				}
				else
				{
					/* all stats flushed, no need for the timeout */
					if (get_timeout_active(IDLE_STATS_UPDATE_TIMEOUT))
						disable_timeout(IDLE_STATS_UPDATE_TIMEOUT, false);
				}

				set_ps_display("idle");
				pgstat_report_activity(STATE_IDLE, NULL);

				/* Start the idle-session timer */
				if (IdleSessionTimeout > 0)
				{
					idle_session_timeout_enabled = true;
					enable_timeout_after(IDLE_SESSION_TIMEOUT,
										 IdleSessionTimeout);
				}

				if (IsYugaByteEnabled())
					yb_pgstat_set_has_catalog_version(false);
			}

			/* Report any recently-changed GUC options */
			ReportChangedGUCOptions();

			/*
			 * YB: The server must respond with a ReadyForQuery message when it's
			 * ready to accept new queries. This is a good place to unset
			 * ASH metadata because here we are sure that the previous request
			 * has been completely processed by the server.
			 */
			if (IsYugaByteEnabled() && yb_enable_ash && MyProc->yb_is_ash_metadata_set)
			{
				YbAshUnsetMetadata();
				MyProc->yb_is_ash_metadata_set = false;
			}

			ReadyForQuery(whereToSendOutput);
			send_ready_for_query = false;
		}

		/*
		 * (2) Allow asynchronous signals to be executed immediately if they
		 * come in while we are waiting for client input. (This must be
		 * conditional since we don't want, say, reads on behalf of COPY FROM
		 * STDIN doing the same thing.)
		 */
		DoingCommandRead = true;

		/*
		 * (3) read a command (loop blocks here)
		 */
		firstchar = ReadCommand(&input_message);

		/*
		 * (4) turn off the idle-in-transaction and idle-session timeouts if
		 * active.  We do this before step (5) so that any last-moment timeout
		 * is certain to be detected in step (5).
		 *
		 * At most one of these timeouts will be active, so there's no need to
		 * worry about combining the timeout.c calls into one.
		 */
		if (idle_in_transaction_timeout_enabled)
		{
			disable_timeout(IDLE_IN_TRANSACTION_SESSION_TIMEOUT, false);
			idle_in_transaction_timeout_enabled = false;
		}
		if (idle_session_timeout_enabled)
		{
			disable_timeout(IDLE_SESSION_TIMEOUT, false);
			idle_session_timeout_enabled = false;
		}

		/*
		 * (5) disable async signal conditions again.
		 *
		 * Query cancel is supposed to be a no-op when there is no query in
		 * progress, so if a query cancel arrived while we were idle, just
		 * reset QueryCancelPending. ProcessInterrupts() has that effect when
		 * it's called when DoingCommandRead is set, so check for interrupts
		 * before resetting DoingCommandRead.
		 */
		CHECK_FOR_INTERRUPTS();
		DoingCommandRead = false;

		/*
		 * YB: A single TCP packet from the client might contain a series of messages -
		 * parse, bind, describe, execute and sync. We only want to set the metadata
		 * once during this process.
		 */
		if (IsYugaByteEnabled() && yb_enable_ash && !MyProc->yb_is_ash_metadata_set)
		{
			YbAshSetMetadata();
			MyProc->yb_is_ash_metadata_set = true;
		}

		/*
		 * (6) check for any other interesting events that happened while we
		 * slept.
		 */
		if (ConfigReloadPending)
		{
			ConfigReloadPending = false;
			/*
			 * YB: Reloading postgres config file on a control connection can
			 * have some repercussion, therefore adopting most safest option;
			 * destroy the control connection which leads to failure of client
			 * authentication and let client keep trying agin untill a new
			 * control connection is formed for authentication with updated
			 * config file.
			 * Control connection is identified if a connection receives a
			 * Auth Passthrough Request ('A') packet.
			*/

			if (firstchar == 'A') /* Auth Passthrough Request */
			{
				/* Make sure auth pass through packet is sent by connection manager only */
				if (!YbIsClientYsqlConnMgr())
					ereport(FATAL,
						(errcode(ERRCODE_PROTOCOL_VIOLATION),
						 errmsg("invalid frontend message type %d", firstchar)));

				ereport(FATAL,
						(errcode(ERRCODE_PROTOCOL_VIOLATION),
						 errmsg("reloading config on control connection is not supported")));
			}
			else
			{
				ProcessConfigFile(PGC_SIGHUP);
			}
		}

		/*
		 * (7) process the command.  But ignore it if we're skipping till
		 * Sync.
		 */
		if (ignore_till_sync && firstchar != EOF)
			continue;

		if (IsYugaByteEnabled())
		{
			yb_pgstat_set_has_catalog_version(true);
			YBCPgResetCatalogReadTime();
			YBCheckSharedCatalogCacheVersion();
			yb_run_with_explain_analyze = false;
			if (IsYsqlUpgrade &&
				yb_catalog_version_type != CATALOG_VERSION_CATALOG_TABLE)
				yb_catalog_version_type = CATALOG_VERSION_UNSET;
			yb_is_multi_statement_query = false;
		}

		switch (firstchar)
		{
			case 'Q':			/* simple query */
			{
				const char *query_string;

				/* Set statement_timestamp() */
				SetCurrentStatementStartTimestamp();

				query_string = pq_getmsgstring(&input_message);
				pq_getmsgend(&input_message);
				MemoryContext oldcontext = GetCurrentMemoryContext();

				PG_TRY();
				{
					if (!am_walsender || !exec_replication_command(query_string))
						yb_exec_simple_query(query_string, oldcontext);
				}
				PG_CATCH();
				{
					/* Get error data */
					ErrorData *edata;
					MemoryContext errorcontext = MemoryContextSwitchTo(oldcontext);
					edata = CopyErrorData();

					bool need_retry = false;
					YBPrepareCacheRefreshIfNeeded(edata,
												  yb_check_retry_allowed(query_string),
												  yb_is_dml_command(query_string),
												  &need_retry);

					if (need_retry)
					{
						if (!am_walsender || !exec_replication_command(query_string))
						{
							if (yb_debug_log_internal_restarts)
							{
								yb_report_cache_version_restart(query_string, edata);
							}
							/*
							 * Free edata before restarting, in other branches
							 * the memory context will get reset after anyway.
							 */
							FreeErrorData(edata);
							yb_exec_simple_query(query_string, oldcontext);
						}
					}
					else
					{
						MemoryContextSwitchTo(errorcontext);
						PG_RE_THROW();
					}
				}
				PG_END_TRY();

				send_ready_for_query = true;
			}
			break;

			case 'P':			/* parse */
				{
					const char *stmt_name;
					const char *query_string;
					int			numParams;
					Oid		   *paramTypes = NULL;

					forbidden_in_wal_sender(firstchar);

					/* Set statement_timestamp() */
					SetCurrentStatementStartTimestamp();

					stmt_name = pq_getmsgstring(&input_message);
					query_string = pq_getmsgstring(&input_message);
					numParams = pq_getmsgint(&input_message, 2);
					if (numParams > 0)
					{
						paramTypes = (Oid *) palloc(numParams * sizeof(Oid));
						for (int i = 0; i < numParams; i++)
							paramTypes[i] = pq_getmsgint(&input_message, 4);
					}
					pq_getmsgend(&input_message);

					MemoryContext oldcontext = GetCurrentMemoryContext();

					PG_TRY();
					{
						exec_parse_message(query_string,
										   stmt_name,
										   paramTypes,
										   numParams,
										   whereToSendOutput);
					}
					PG_CATCH();
					{
						/* Get error data */
						ErrorData *edata;
						MemoryContext errorcontext = MemoryContextSwitchTo(oldcontext);
						edata = CopyErrorData();

						/*
						 * TODO Cannot retry parse statements yet (without
						 * aborting the followup bind/execute.
						 */
						bool need_retry = false;
						YBPrepareCacheRefreshIfNeeded(edata,
													  false /* consider_retry */,
													  yb_is_dml_command(query_string),
													  &need_retry);
						MemoryContextSwitchTo(errorcontext);
						PG_RE_THROW();

					}
					PG_END_TRY();

				}
				break;

			case 'B':			/* bind */
				forbidden_in_wal_sender(firstchar);

				/* Set statement_timestamp() */
				SetCurrentStatementStartTimestamp();

				/*
				 * this message is complex enough that it seems best to put
				 * the field extraction out-of-line
				 */
				exec_bind_message(&input_message);
				break;

			case 'E':			/* execute */
				{
					forbidden_in_wal_sender(firstchar);

					/* Set statement_timestamp() */
					SetCurrentStatementStartTimestamp();

					const char *portal_name = pq_getmsgstring(&input_message);
					const int max_rows = pq_getmsgint(&input_message, 4);
					pq_getmsgend(&input_message);

					MemoryContext oldcontext = GetCurrentMemoryContext();
					const YBQueryRetryData *retry_data =
						yb_collect_portal_restart_data(portal_name);

					PG_TRY();
					{
						yb_exec_execute_message(max_rows, retry_data,
												oldcontext);
					}
					PG_CATCH();
					{
						/*
						 * Get error data. Original error will be thrown in 2 cases:
						 * - query can't be restarted transparently
						 * - original error is "schema version mismatch for table"
						 *   and restarting will raise an error (workaround for #6982)
						 */
						MemoryContext errorcontext = MemoryContextSwitchTo(oldcontext);
						ErrorData *edata = CopyErrorData();

						/*
						 * The portal recreation logic is restored to the pre-#2216 state
						 * (it was reworked in #4254).
						 */
						Portal old_portal = GetPortalByName(portal_name);

						/*
						 * TODO Do not support retrying for prepared statements
						 * yet. (i.e. if portal is named or has params).
						 */
						bool can_retry =
							IsYugaByteEnabled() &&
							old_portal &&
							portal_name[0] == '\0' &&
							!old_portal->portalParams &&
							yb_check_retry_allowed(retry_data->query_string);


						/* Stuff we might need for retrying below */
						char* query_string = NULL;
						int   nformats = 0;
						int16 *formats = NULL;

						if (can_retry)
						{
							/*
							 * Copy the data needed to retry before transaction
							 * abort cleans it up.
							 */
							query_string = pstrdup(retry_data->query_string);

							if (old_portal->formats)
							{
								nformats = old_portal->tupDesc->natts;
								formats  = (int16 *) palloc(nformats * sizeof(int16));
								memcpy(formats,
									   old_portal->formats,
									   nformats * sizeof(int16));
							}
						}

						bool need_retry = false;

						/*
						 * Execute may have been partially applied so need to
						 * cleanup (and restart) the transaction.
						 */
						YBPrepareCacheRefreshIfNeeded(edata, can_retry,
													  yb_is_dml_command(query_string),
													  &need_retry);

						if (need_retry && can_retry)
						{
							PG_TRY();
							{
								if (yb_debug_log_internal_restarts)
								{
									yb_report_cache_version_restart(query_string, edata);
								}

								/* 1. Redo Parse: Create Cached stmt (no output) */
								exec_parse_message(query_string,
												   portal_name,
												   NULL /* param_types */,
												   0 /* num_params */,
												   DestNone);

								/* 2. Redo the Bind step */
								Portal portal;
								/* Create portal */
								portal = CreatePortal(portal_name, true, true);

								/* Set portal data */
								MemoryContext oldContext;
								oldContext =
									MemoryContextSwitchTo(portal->portalContext);
								char *stmt_name;
								if (portal_name[0])
									stmt_name = pstrdup(portal_name);
								else
									stmt_name = NULL;
								query_string = pstrdup(query_string);

								/* TODO params are none for now (see above) */
								ParamListInfo params = NULL;

								MemoryContextSwitchTo(oldContext);

								CachedPlan *cplan = GetCachedPlan(unnamed_stmt_psrc,
																  params,
																  false,
																  NULL);

								PortalDefineQuery(portal,
												  stmt_name,
												  query_string,
												  unnamed_stmt_psrc->commandTag,
												  cplan->stmt_list,
												  cplan);

								/* Start portal */
								PortalStart(portal, params, 0, InvalidSnapshot);
								/* Set the output format */
								PortalSetResultFormat(portal, nformats, formats);

								/* Now ready to retry the execute step. */
								yb_exec_execute_message(max_rows,
														retry_data,
														GetCurrentMemoryContext());
							}
							PG_CATCH();
							{
								if (YBTableSchemaVersionMismatchError(edata, NULL /* table_id */))
									ReThrowError(edata);
								else
									PG_RE_THROW();
							}
							PG_END_TRY();
						}
						else
						{
							MemoryContextSwitchTo(errorcontext);
							PG_RE_THROW();
						}

					}
					PG_END_TRY();
				}
				break;

			case 'F':			/* fastpath function call */
				forbidden_in_wal_sender(firstchar);

				/* Set statement_timestamp() */
				SetCurrentStatementStartTimestamp();

				/* Report query to various monitoring facilities. */
				pgstat_report_activity(STATE_FASTPATH, NULL);
				set_ps_display("<FASTPATH>");

				/* start an xact for this function invocation */
				start_xact_command();

				/*
				 * Note: we may at this point be inside an aborted
				 * transaction.  We can't throw error for that until we've
				 * finished reading the function-call message, so
				 * HandleFunctionRequest() must check for it after doing so.
				 * Be careful not to do anything that assumes we're inside a
				 * valid transaction here.
				 */

				/* switch back to message context */
				MemoryContextSwitchTo(MessageContext);

				HandleFunctionRequest(&input_message);

				/* commit the function-invocation transaction */
				finish_xact_command();

				send_ready_for_query = true;
				break;

			case 'C':			/* close */
				{
					int			close_type;
					const char *close_target;

					forbidden_in_wal_sender(firstchar);

					close_type = pq_getmsgbyte(&input_message);
					close_target = pq_getmsgstring(&input_message);
					pq_getmsgend(&input_message);

					switch (close_type)
					{
						case 'S':
							if (close_target[0] != '\0')
								DropPreparedStatement(close_target, false);
							else
							{
								/* special-case the unnamed statement */
								drop_unnamed_stmt();
							}
							break;
						case 'P':
							{
								Portal		portal;

								portal = GetPortalByName(close_target);
								if (PortalIsValid(portal))
									PortalDrop(portal, false);
							}
							break;
						default:
							ereport(ERROR,
									(errcode(ERRCODE_PROTOCOL_VIOLATION),
									 errmsg("invalid CLOSE message subtype %d",
											close_type)));
							break;
					}

					if (whereToSendOutput == DestRemote)
						pq_putemptymessage('3');	/* CloseComplete */
				}
				break;

			case 'D':			/* describe */
				{
					int			describe_type;
					const char *describe_target;

					forbidden_in_wal_sender(firstchar);

					/* Set statement_timestamp() (needed for xact) */
					SetCurrentStatementStartTimestamp();

					describe_type = pq_getmsgbyte(&input_message);
					describe_target = pq_getmsgstring(&input_message);
					pq_getmsgend(&input_message);

					switch (describe_type)
					{
						case 'S':
							exec_describe_statement_message(describe_target);
							break;
						case 'P':
							exec_describe_portal_message(describe_target);
							break;
						default:
							ereport(ERROR,
									(errcode(ERRCODE_PROTOCOL_VIOLATION),
									 errmsg("invalid DESCRIBE message subtype %d",
											describe_type)));
							break;
					}
				}
				break;

			case 'H':			/* flush */
				pq_getmsgend(&input_message);
				if (whereToSendOutput == DestRemote)
					pq_flush();
				break;

			case 'S':			/* sync */
				pq_getmsgend(&input_message);
				finish_xact_command();
				send_ready_for_query = true;
				break;

				/*
				 * 'X' means that the frontend is closing down the socket. EOF
				 * means unexpected loss of frontend connection. Either way,
				 * perform normal shutdown.
				 */
			case EOF:

				/* for the cumulative statistics system */
				pgStatSessionEndCause = DISCONNECT_CLIENT_EOF;

				switch_fallthrough(); /* FALLTHROUGH */

			case 'X':

				/*
				 * Reset whereToSendOutput to prevent ereport from attempting
				 * to send any more messages to client.
				 */
				if (whereToSendOutput == DestRemote)
					whereToSendOutput = DestNone;

				/*
				 * NOTE: if you are tempted to add more code here, DON'T!
				 * Whatever you had in mind to do should be set up as an
				 * on_proc_exit or on_shmem_exit callback, instead. Otherwise
				 * it will fail to be called during other backend-shutdown
				 * scenarios.
				 */
				proc_exit(0);

			case 'd':			/* copy data */
			case 'c':			/* copy done */
			case 'f':			/* copy fail */

				/*
				 * Accept but ignore these messages, per protocol spec; we
				 * probably got here because a COPY failed, and the frontend
				 * is still sending data.
				 */
				break;

			case 'A': /* Auth Passthrough Request */
				if (YbIsClientYsqlConnMgr())
				{
					/*
					 * Do not rely on cache during authentication passthrough.
					 * "ALTER ROLE" does not change the catalog version due to this
					 * local cache may have an invalid cache.
					 *
					 * TODO (GH #21998): Invalidate cache specific to the role credentials and
					 * logic permissions.
					 */
					ResetCatalogCaches();

					/* Store a copy of the old context */
					char *db_name = MyProcPort->database_name;
					char *user_name = MyProcPort->user_name;
					char *host = MyProcPort->remote_host;
					sa_family_t conn_type = MyProcPort->raddr.addr.ss_family;

					/* Update the Port details with the new context. */
					MyProcPort->user_name =
						(char *) pq_getmsgstring(&input_message);
					MyProcPort->database_name =
						(char *) pq_getmsgstring(&input_message);
					MyProcPort->remote_host =
						(char *) pq_getmsgstring(&input_message);
					// HARD Code connection type between client and ysql_conn_mgr to AF_INET (only supported)
					// for authentication
					MyProcPort->raddr.addr.ss_family = AF_INET;
					MyProcPort->yb_is_ssl_enabled_in_logical_conn =
						pq_getmsgbyte(&input_message) == 'E' ? true : false;

					/* Update the `remote_host` */
					struct sockaddr_in *ip_address_1;
					ip_address_1 =
						(struct sockaddr_in *) (&MyProcPort->raddr.addr);
					inet_pton(AF_INET, MyProcPort->remote_host,
							  &(ip_address_1->sin_addr));
					MyProcPort->yb_is_auth_passthrough_req = true;

					/* Start authentication */
					start_xact_command();
					ClientAuthentication(MyProcPort);
					finish_xact_command();

					/* Place back the old context */
					MyProcPort->yb_is_auth_passthrough_req = false;
					MyProcPort->yb_is_ssl_enabled_in_logical_conn = false;
					MyProcPort->user_name = user_name;
					MyProcPort->database_name = db_name;
					MyProcPort->remote_host = host;
					MyProcPort->raddr.addr.ss_family = conn_type;
					inet_pton(AF_INET, MyProcPort->remote_host,
							  &(ip_address_1->sin_addr));

					send_ready_for_query = true;
				}
				else
				{
					ereport(FATAL,
							(errcode(ERRCODE_PROTOCOL_VIOLATION),
							 errmsg("invalid frontend message type %d",
									firstchar)));
				}
				break;

			case 's': /* SET SESSION PARAMETER */
				if (YbIsClientYsqlConnMgr())
				{
					start_xact_command();
					YbHandleSetSessionParam(pq_getmsgint(&input_message, 4));
					int new_shmem_key = yb_logical_client_shmem_key;
					finish_xact_command();

					// finish_xact_command() resets the yb_logical_client_shmem_key value.
					if (new_shmem_key > 0)
						yb_logical_client_shmem_key = new_shmem_key;

					send_ready_for_query = true;
				}
				else
				{
					ereport(FATAL,
							(errcode(ERRCODE_PROTOCOL_VIOLATION),
							 errmsg("invalid frontend message type %d",
								firstchar)));
				}
				break;

			default:
				ereport(FATAL,
						(errcode(ERRCODE_PROTOCOL_VIOLATION),
						 errmsg("invalid frontend message type %d",
								firstchar)));
		}
	}							/* end of input-reading loop */
}

/*
 * Throw an error if we're a WAL sender process.
 *
 * This is used to forbid anything else than simple query protocol messages
 * in a WAL sender process.  'firstchar' specifies what kind of a forbidden
 * message was received, and is used to construct the error message.
 */
static void
forbidden_in_wal_sender(char firstchar)
{
	if (am_walsender)
	{
		if (firstchar == 'F')
			ereport(ERROR,
					(errcode(ERRCODE_PROTOCOL_VIOLATION),
					 errmsg("fastpath function calls not supported in a replication connection")));
		else
			ereport(ERROR,
					(errcode(ERRCODE_PROTOCOL_VIOLATION),
					 errmsg("extended query protocol not supported in a replication connection")));
	}
}


/*
 * Obtain platform stack depth limit (in bytes)
 *
 * Return -1 if unknown
 */
long
get_stack_depth_rlimit(void)
{
#if defined(HAVE_GETRLIMIT) && defined(RLIMIT_STACK)
	static long val = 0;

	/* This won't change after process launch, so check just once */
	if (val == 0)
	{
		struct rlimit rlim;

		if (getrlimit(RLIMIT_STACK, &rlim) < 0)
			val = -1;
		else if (rlim.rlim_cur == RLIM_INFINITY)
			val = LONG_MAX;
		/* rlim_cur is probably of an unsigned type, so check for overflow */
		else if (rlim.rlim_cur >= LONG_MAX)
			val = LONG_MAX;
		else
			val = rlim.rlim_cur;
	}
	return val;
#else							/* no getrlimit */
#if defined(WIN32) || defined(__CYGWIN__)
	/* On Windows we set the backend stack size in src/backend/Makefile */
	return WIN32_STACK_RLIMIT;
#else							/* not windows ... give up */
	return -1;
#endif
#endif
}


static struct rusage Save_r;
static struct timeval Save_t;

void
ResetUsage(void)
{
	getrusage(RUSAGE_SELF, &Save_r);
	gettimeofday(&Save_t, NULL);
}

void
ShowUsage(const char *title)
{
	StringInfoData str;
	struct timeval user,
				sys;
	struct timeval elapse_t;
	struct rusage r;

	getrusage(RUSAGE_SELF, &r);
	gettimeofday(&elapse_t, NULL);
	memcpy((char *) &user, (char *) &r.ru_utime, sizeof(user));
	memcpy((char *) &sys, (char *) &r.ru_stime, sizeof(sys));
	if (elapse_t.tv_usec < Save_t.tv_usec)
	{
		elapse_t.tv_sec--;
		elapse_t.tv_usec += 1000000;
	}
	if (r.ru_utime.tv_usec < Save_r.ru_utime.tv_usec)
	{
		r.ru_utime.tv_sec--;
		r.ru_utime.tv_usec += 1000000;
	}
	if (r.ru_stime.tv_usec < Save_r.ru_stime.tv_usec)
	{
		r.ru_stime.tv_sec--;
		r.ru_stime.tv_usec += 1000000;
	}

	/*
	 * The only stats we don't show here are ixrss, idrss, isrss.  It takes
	 * some work to interpret them, and most platforms don't fill them in.
	 */
	initStringInfo(&str);

	appendStringInfoString(&str, "! system usage stats:\n");
	appendStringInfo(&str,
					 "!\t%ld.%06ld s user, %ld.%06ld s system, %ld.%06ld s elapsed\n",
					 (long) (r.ru_utime.tv_sec - Save_r.ru_utime.tv_sec),
					 (long) (r.ru_utime.tv_usec - Save_r.ru_utime.tv_usec),
					 (long) (r.ru_stime.tv_sec - Save_r.ru_stime.tv_sec),
					 (long) (r.ru_stime.tv_usec - Save_r.ru_stime.tv_usec),
					 (long) (elapse_t.tv_sec - Save_t.tv_sec),
					 (long) (elapse_t.tv_usec - Save_t.tv_usec));
	appendStringInfo(&str,
					 "!\t[%ld.%06ld s user, %ld.%06ld s system total]\n",
					 (long) user.tv_sec,
					 (long) user.tv_usec,
					 (long) sys.tv_sec,
					 (long) sys.tv_usec);
#if defined(HAVE_GETRUSAGE)
	appendStringInfo(&str,
					 "!\t%ld kB max resident size\n",
#if defined(__darwin__)
	/* in bytes on macOS */
					 r.ru_maxrss / 1024
#else
	/* in kilobytes on most other platforms */
					 r.ru_maxrss
#endif
		);
	appendStringInfo(&str,
					 "!\t%ld/%ld [%ld/%ld] filesystem blocks in/out\n",
					 r.ru_inblock - Save_r.ru_inblock,
	/* they only drink coffee at dec */
					 r.ru_oublock - Save_r.ru_oublock,
					 r.ru_inblock, r.ru_oublock);
	appendStringInfo(&str,
					 "!\t%ld/%ld [%ld/%ld] page faults/reclaims, %ld [%ld] swaps\n",
					 r.ru_majflt - Save_r.ru_majflt,
					 r.ru_minflt - Save_r.ru_minflt,
					 r.ru_majflt, r.ru_minflt,
					 r.ru_nswap - Save_r.ru_nswap,
					 r.ru_nswap);
	appendStringInfo(&str,
					 "!\t%ld [%ld] signals rcvd, %ld/%ld [%ld/%ld] messages rcvd/sent\n",
					 r.ru_nsignals - Save_r.ru_nsignals,
					 r.ru_nsignals,
					 r.ru_msgrcv - Save_r.ru_msgrcv,
					 r.ru_msgsnd - Save_r.ru_msgsnd,
					 r.ru_msgrcv, r.ru_msgsnd);
	appendStringInfo(&str,
					 "!\t%ld/%ld [%ld/%ld] voluntary/involuntary context switches\n",
					 r.ru_nvcsw - Save_r.ru_nvcsw,
					 r.ru_nivcsw - Save_r.ru_nivcsw,
					 r.ru_nvcsw, r.ru_nivcsw);
#endif							/* HAVE_GETRUSAGE */

	/* remove trailing newline */
	if (str.data[str.len - 1] == '\n')
		str.data[--str.len] = '\0';

	ereport(LOG,
			(errmsg_internal("%s", title),
			 errdetail_internal("%s", str.data)));

	pfree(str.data);
}

/*
 * on_proc_exit handler to log end of session
 */
static void
log_disconnections(int code, Datum arg)
{
	Port	   *port = MyProcPort;
	long		secs;
	int			usecs;
	int			msecs;
	int			hours,
				minutes,
				seconds;

	TimestampDifference(MyStartTimestamp,
						GetCurrentTimestamp(),
						&secs, &usecs);
	msecs = usecs / 1000;

	hours = secs / SECS_PER_HOUR;
	secs %= SECS_PER_HOUR;
	minutes = secs / SECS_PER_MINUTE;
	seconds = secs % SECS_PER_MINUTE;

	ereport(LOG,
			(errmsg("disconnection: session time: %d:%02d:%02d.%03d "
					"user=%s database=%s host=%s%s%s",
					hours, minutes, seconds, msecs,
					port->user_name, port->database_name, port->remote_host,
					port->remote_port[0] ? " port=" : "", port->remote_port)));
}

/*
 * Start statement timeout timer, if enabled.
 *
 * If there's already a timeout running, don't restart the timer.  That
 * enables compromises between accuracy of timeouts and cost of starting a
 * timeout.
 */
static void
enable_statement_timeout(void)
{
	/* must be within an xact */
	Assert(xact_started);

	if (StatementTimeout > 0)
	{
		if (!get_timeout_active(STATEMENT_TIMEOUT))
			enable_timeout_after(STATEMENT_TIMEOUT, StatementTimeout);
	}
	else
	{
		if (get_timeout_active(STATEMENT_TIMEOUT))
			disable_timeout(STATEMENT_TIMEOUT, false);
	}
}

/*
 * Disable statement timeout, if active.
 */
static void
disable_statement_timeout(void)
{
	if (get_timeout_active(STATEMENT_TIMEOUT))
		disable_timeout(STATEMENT_TIMEOUT, false);
}

/*
 * Redact password, if exists in the query text.
 */
const char*
YbRedactPasswordIfExists(const char *queryStr, CommandTag commandTag)
{
	char *redactedStr;
	char *passwordToken;
	int i;
	int passwordPos;

	/*
	* Parse and check the type of the query. We only redact password
	* for the CREATE USER / CREATE ROLE / ALTER USER / ALTER ROLE queries.
	*/
	if (commandTag == CMDTAG_UNKNOWN ||
		(commandTag != CMDTAG_CREATE_ROLE && commandTag != CMDTAG_ALTER_ROLE))
		return queryStr;

	/* Copy the query string and convert to lower case. */
	redactedStr = pstrdup(queryStr);

	for (i = 0; redactedStr[i]; i++)
		redactedStr[i] = (char)pg_tolower((unsigned char)redactedStr[i]);

	/* Find index of password token. */
	passwordToken = strstr(redactedStr, TOKEN_PASSWORD);

	if (passwordToken != NULL)
	{
		/* Copy query string up to password token. */
		passwordPos = (passwordToken - redactedStr) + strlen(TOKEN_PASSWORD);

		redactedStr = palloc(passwordPos + 1 + strlen(TOKEN_REDACTED) + 1);

		strncpy(redactedStr, queryStr, passwordPos);

		/* And append redacted token. */
		redactedStr[passwordPos] = ' ';

		strcpy(redactedStr + passwordPos + 1, TOKEN_REDACTED);

		return redactedStr;
	}

	return queryStr;
}
