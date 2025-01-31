/*
 * psql - the PostgreSQL interactive terminal
 *
 * Copyright (c) 2000-2022, PostgreSQL Global Development Group
 *
 * src/bin/psql/common.c
 */
#include "postgres_fe.h"

#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <pwd.h>
#include <signal.h>
#ifndef WIN32
#include <unistd.h>				/* for write() */
#else
#include <io.h>					/* for _write() */
#include <win32.h>
#endif

#include "command.h"
#include "common.h"
#include "common/logging.h"
#include "copy.h"
#include "crosstabview.h"
#include "fe_utils/cancel.h"
#include "fe_utils/mbprint.h"
#include "fe_utils/string_utils.h"
#include "portability/instr_time.h"
#include "settings.h"

static bool DescribeQuery(const char *query, double *elapsed_msec);
static bool ExecQueryUsingCursor(const char *query, double *elapsed_msec);
static int	ExecQueryAndProcessResults(const char *query,
									   double *elapsed_msec,
									   bool *svpt_gone_p,
									   bool is_watch,
									   const printQueryOpt *opt,
									   FILE *printQueryFout);
static bool command_no_begin(const char *query);
static bool is_select_command(const char *query);


/*
 * openQueryOutputFile --- attempt to open a query output file
 *
 * fname == NULL selects stdout, else an initial '|' selects a pipe,
 * else plain file.
 *
 * Returns output file pointer into *fout, and is-a-pipe flag into *is_pipe.
 * Caller is responsible for adjusting SIGPIPE state if it's a pipe.
 *
 * On error, reports suitable error message and returns false.
 */
bool
openQueryOutputFile(const char *fname, FILE **fout, bool *is_pipe)
{
	if (!fname || fname[0] == '\0')
	{
		*fout = stdout;
		*is_pipe = false;
	}
	else if (*fname == '|')
	{
		*fout = popen(fname + 1, "w");
		*is_pipe = true;
	}
	else
	{
		*fout = fopen(fname, "w");
		*is_pipe = false;
	}

	if (*fout == NULL)
	{
		pg_log_error("%s: %m", fname);
		return false;
	}

	return true;
}

/*
 * setQFout
 * -- handler for -o command line option and \o command
 *
 * On success, updates pset with the new output file and returns true.
 * On failure, returns false without changing pset state.
 */
bool
setQFout(const char *fname)
{
	FILE	   *fout;
	bool		is_pipe;

	/* First make sure we can open the new output file/pipe */
	if (!openQueryOutputFile(fname, &fout, &is_pipe))
		return false;

	/* Close old file/pipe */
	if (pset.queryFout && pset.queryFout != stdout && pset.queryFout != stderr)
	{
		if (pset.queryFoutPipe)
			pclose(pset.queryFout);
		else
			fclose(pset.queryFout);
	}

	pset.queryFout = fout;
	pset.queryFoutPipe = is_pipe;

	/* Adjust SIGPIPE handling appropriately: ignore signal if is_pipe */
	set_sigpipe_trap_state(is_pipe);
	restore_sigpipe_trap();

	return true;
}


/*
 * Variable-fetching callback for flex lexer
 *
 * If the specified variable exists, return its value as a string (malloc'd
 * and expected to be freed by the caller); else return NULL.
 *
 * If "quote" isn't PQUOTE_PLAIN, then return the value suitably quoted and
 * escaped for the specified quoting requirement.  (Failure in escaping
 * should lead to printing an error and returning NULL.)
 *
 * "passthrough" is the pointer previously given to psql_scan_set_passthrough.
 * In psql, passthrough points to a ConditionalStack, which we check to
 * determine whether variable expansion is allowed.
 */
char *
psql_get_variable(const char *varname, PsqlScanQuoteType quote,
				  void *passthrough)
{
	char	   *result = NULL;
	const char *value;

	/* In an inactive \if branch, suppress all variable substitutions */
	if (passthrough && !conditional_active((ConditionalStack) passthrough))
		return NULL;

	value = GetVariable(pset.vars, varname);
	if (!value)
		return NULL;

	switch (quote)
	{
		case PQUOTE_PLAIN:
			result = pg_strdup(value);
			break;
		case PQUOTE_SQL_LITERAL:
		case PQUOTE_SQL_IDENT:
			{
				/*
				 * For these cases, we use libpq's quoting functions, which
				 * assume the string is in the connection's client encoding.
				 */
				char	   *escaped_value;

				if (!pset.db)
				{
					pg_log_error("cannot escape without active connection");
					return NULL;
				}

				if (quote == PQUOTE_SQL_LITERAL)
					escaped_value =
						PQescapeLiteral(pset.db, value, strlen(value));
				else
					escaped_value =
						PQescapeIdentifier(pset.db, value, strlen(value));

				if (escaped_value == NULL)
				{
					const char *error = PQerrorMessage(pset.db);

					pg_log_info("%s", error);
					return NULL;
				}

				/*
				 * Rather than complicate the lexer's API with a notion of
				 * which free() routine to use, just pay the price of an extra
				 * strdup().
				 */
				result = pg_strdup(escaped_value);
				PQfreemem(escaped_value);
				break;
			}
		case PQUOTE_SHELL_ARG:
			{
				/*
				 * For this we use appendShellStringNoError, which is
				 * encoding-agnostic, which is fine since the shell probably
				 * is too.  In any case, the only special character is "'",
				 * which is not known to appear in valid multibyte characters.
				 */
				PQExpBufferData buf;

				initPQExpBuffer(&buf);
				if (!appendShellStringNoError(&buf, value))
				{
					pg_log_error("shell command argument contains a newline or carriage return: \"%s\"",
								 value);
					free(buf.data);
					return NULL;
				}
				result = buf.data;
				break;
			}

			/* No default: we want a compiler warning for missing cases */
	}

	return result;
}


/*
 * for backend Notice messages (INFO, WARNING, etc)
 */
void
NoticeProcessor(void *arg, const char *message)
{
	(void) arg;					/* not used */

	yb_set_should_log_filename(!GetVariable(pset.vars, "YB_DISABLE_ERROR_PREFIX"));

	pg_log_info("%s", message);
}


/*
 * Code to support query cancellation
 *
 * Before we start a query, we enable the SIGINT signal catcher to send a
 * cancel request to the backend.
 *
 * SIGINT is supposed to abort all long-running psql operations, not only
 * database queries.  In most places, this is accomplished by checking
 * cancel_pressed during long-running loops.  However, that won't work when
 * blocked on user input (in readline() or fgets()).  In those places, we
 * set sigint_interrupt_enabled true while blocked, instructing the signal
 * catcher to longjmp through sigint_interrupt_jmp.  We assume readline and
 * fgets are coded to handle possible interruption.
 *
 * On Windows, currently this does not work, so control-C is less useful
 * there.
 */
volatile bool sigint_interrupt_enabled = false;

sigjmp_buf	sigint_interrupt_jmp;

static void
psql_cancel_callback(void)
{
#ifndef WIN32
	/* if we are waiting for input, longjmp out of it */
	if (sigint_interrupt_enabled)
	{
		sigint_interrupt_enabled = false;
		siglongjmp(sigint_interrupt_jmp, 1);
	}
#endif

	/* else, set cancel flag to stop any long-running loops */
	cancel_pressed = true;
}

void
psql_setup_cancel_handler(void)
{
	setup_cancel_handler(psql_cancel_callback);
}


/* ConnectionUp
 *
 * Returns whether our backend connection is still there.
 */
static bool
ConnectionUp(void)
{
	return PQstatus(pset.db) != CONNECTION_BAD;
}



/* CheckConnection
 *
 * Verify that we still have a good connection to the backend, and if not,
 * see if it can be restored.
 *
 * Returns true if either the connection was still there, or it could be
 * restored successfully; false otherwise.  If, however, there was no
 * connection and the session is non-interactive, this will exit the program
 * with a code of EXIT_BADCONN.
 */
static bool
CheckConnection(void)
{
	bool		OK;

	OK = ConnectionUp();
	if (!OK)
	{
		if (!pset.cur_cmd_interactive)
		{
			pg_log_error("connection to server was lost");
			exit(EXIT_BADCONN);
		}

		fprintf(stderr, _("The connection to the server was lost. Attempting reset: "));
		PQreset(pset.db);
		OK = ConnectionUp();
		if (!OK)
		{
			fprintf(stderr, _("Failed.\n"));

			/*
			 * Transition to having no connection; but stash away the failed
			 * connection so that we can still refer to its parameters in a
			 * later \connect attempt.  Keep the state cleanup here in sync
			 * with do_connect().
			 */
			if (pset.dead_conn)
				PQfinish(pset.dead_conn);
			pset.dead_conn = pset.db;
			pset.db = NULL;
			ResetCancelConn();
			UnsyncVariables();
		}
		else
		{
			fprintf(stderr, _("Succeeded.\n"));

			/*
			 * Re-sync, just in case anything changed.  Keep this in sync with
			 * do_connect().
			 */
			SyncVariables();
			connection_warnings(false); /* Must be after SyncVariables */
		}
	}

	return OK;
}




/*
 * AcceptResult
 *
 * Checks whether a result is valid, giving an error message if necessary;
 * and ensures that the connection to the backend is still up.
 *
 * Returns true for valid result, false for error state.
 */
static bool
AcceptResult(const PGresult *result, bool show_error)
{
	bool		OK;

	if (!result)
		OK = false;
	else
		switch (PQresultStatus(result))
		{
			case PGRES_COMMAND_OK:
			case PGRES_TUPLES_OK:
			case PGRES_EMPTY_QUERY:
			case PGRES_COPY_IN:
			case PGRES_COPY_OUT:
				/* Fine, do nothing */
				OK = true;
				break;

			case PGRES_BAD_RESPONSE:
			case PGRES_NONFATAL_ERROR:
			case PGRES_FATAL_ERROR:
				OK = false;
				break;

			default:
				OK = false;
				pg_log_error("unexpected PQresultStatus: %d",
							 PQresultStatus(result));
				break;
		}

	if (!OK && show_error)
	{
		const char *error = PQerrorMessage(pset.db);

		if (strlen(error))
			pg_log_info("%s", error);

		CheckConnection();
	}

	return OK;
}


/*
 * Set special variables from a query result
 * - ERROR: true/false, whether an error occurred on this query
 * - SQLSTATE: code of error, or "00000" if no error, or "" if unknown
 * - ROW_COUNT: how many rows were returned or affected, or "0"
 * - LAST_ERROR_SQLSTATE: same for last error
 * - LAST_ERROR_MESSAGE: message of last error
 *
 * Note: current policy is to apply this only to the results of queries
 * entered by the user, not queries generated by slash commands.
 */
static void
SetResultVariables(PGresult *result, bool success)
{
	if (success)
	{
		const char *ntuples = PQcmdTuples(result);

		SetVariable(pset.vars, "ERROR", "false");
		SetVariable(pset.vars, "SQLSTATE", "00000");
		SetVariable(pset.vars, "ROW_COUNT", *ntuples ? ntuples : "0");
	}
	else
	{
		const char *code = PQresultErrorField(result, PG_DIAG_SQLSTATE);
		const char *mesg = PQresultErrorField(result, PG_DIAG_MESSAGE_PRIMARY);

		SetVariable(pset.vars, "ERROR", "true");

		/*
		 * If there is no SQLSTATE code, use an empty string.  This can happen
		 * for libpq-detected errors (e.g., lost connection, ENOMEM).
		 */
		if (code == NULL)
			code = "";
		SetVariable(pset.vars, "SQLSTATE", code);
		SetVariable(pset.vars, "ROW_COUNT", "0");
		SetVariable(pset.vars, "LAST_ERROR_SQLSTATE", code);
		SetVariable(pset.vars, "LAST_ERROR_MESSAGE", mesg ? mesg : "");
	}
}


/*
 * ClearOrSaveResult
 *
 * If the result represents an error, remember it for possible display by
 * \errverbose.  Otherwise, just PQclear() it.
 *
 * Note: current policy is to apply this to the results of all queries,
 * including "back door" queries, for debugging's sake.  It's OK to use
 * PQclear() directly on results known to not be error results, however.
 */
static void
ClearOrSaveResult(PGresult *result)
{
	if (result)
	{
		switch (PQresultStatus(result))
		{
			case PGRES_NONFATAL_ERROR:
			case PGRES_FATAL_ERROR:
				if (pset.last_error_result)
					PQclear(pset.last_error_result);
				pset.last_error_result = result;
				break;

			default:
				PQclear(result);
				break;
		}
	}
}


/*
 * Consume all results
 */
static void
ClearOrSaveAllResults(void)
{
	PGresult   *result;

	while ((result = PQgetResult(pset.db)) != NULL)
		ClearOrSaveResult(result);
}


/*
 * Print microtiming output.  Always print raw milliseconds; if the interval
 * is >= 1 second, also break it down into days/hours/minutes/seconds.
 */
static void
PrintTiming(double elapsed_msec)
{
	double		seconds;
	double		minutes;
	double		hours;
	double		days;

	if (elapsed_msec < 1000.0)
	{
		/* This is the traditional (pre-v10) output format */
		printf(_("Time: %.3f ms\n"), elapsed_msec);
		return;
	}

	/*
	 * Note: we could print just seconds, in a format like %06.3f, when the
	 * total is less than 1min.  But that's hard to interpret unless we tack
	 * on "s" or otherwise annotate it.  Forcing the display to include
	 * minutes seems like a better solution.
	 */
	seconds = elapsed_msec / 1000.0;
	minutes = floor(seconds / 60.0);
	seconds -= 60.0 * minutes;
	if (minutes < 60.0)
	{
		printf(_("Time: %.3f ms (%02d:%06.3f)\n"),
			   elapsed_msec, (int) minutes, seconds);
		return;
	}

	hours = floor(minutes / 60.0);
	minutes -= 60.0 * hours;
	if (hours < 24.0)
	{
		printf(_("Time: %.3f ms (%02d:%02d:%06.3f)\n"),
			   elapsed_msec, (int) hours, (int) minutes, seconds);
		return;
	}

	days = floor(hours / 24.0);
	hours -= 24.0 * days;
	printf(_("Time: %.3f ms (%.0f d %02d:%02d:%06.3f)\n"),
		   elapsed_msec, days, (int) hours, (int) minutes, seconds);
}


/*
 * PSQLexec
 *
 * This is the way to send "backdoor" queries (those not directly entered
 * by the user). It is subject to -E but not -e.
 *
 * Caller is responsible for handling the ensuing processing if a COPY
 * command is sent.
 *
 * Note: we don't bother to check PQclientEncoding; it is assumed that no
 * caller uses this path to issue "SET CLIENT_ENCODING".
 */
PGresult *
PSQLexec(const char *query)
{
	PGresult   *res;

	if (!pset.db)
	{
		pg_log_error("You are currently not connected to a database.");
		return NULL;
	}

	if (pset.echo_hidden != PSQL_ECHO_HIDDEN_OFF)
	{
		printf(_("********* QUERY **********\n"
				 "%s\n"
				 "**************************\n\n"), query);
		fflush(stdout);
		if (pset.logfile)
		{
			fprintf(pset.logfile,
					_("********* QUERY **********\n"
					  "%s\n"
					  "**************************\n\n"), query);
			fflush(pset.logfile);
		}

		if (pset.echo_hidden == PSQL_ECHO_HIDDEN_NOEXEC)
			return NULL;
	}

	SetCancelConn(pset.db);

	res = PQexec(pset.db, query);

	ResetCancelConn();

	if (!AcceptResult(res, true))
	{
		ClearOrSaveResult(res);
		res = NULL;
	}

	return res;
}


/*
 * PSQLexecWatch
 *
 * This function is used for \watch command to send the query to
 * the server and print out the result.
 *
 * Returns 1 if the query executed successfully, 0 if it cannot be repeated,
 * e.g., because of the interrupt, -1 on error.
 */
int
PSQLexecWatch(const char *query, const printQueryOpt *opt, FILE *printQueryFout)
{
	bool		timing = pset.timing;
	double		elapsed_msec = 0;
	int			res;

	if (!pset.db)
	{
		pg_log_error("You are currently not connected to a database.");
		return 0;
	}

	SetCancelConn(pset.db);

	res = ExecQueryAndProcessResults(query, &elapsed_msec, NULL, true, opt, printQueryFout);

	ResetCancelConn();

	/* Possible microtiming output */
	if (timing)
		PrintTiming(elapsed_msec);

	return res;
}


/*
 * PrintNotifications: check for asynchronous notifications, and print them out
 */
static void
PrintNotifications(void)
{
	PGnotify   *notify;

	PQconsumeInput(pset.db);
	while ((notify = PQnotifies(pset.db)) != NULL)
	{
		/* for backward compatibility, only show payload if nonempty */
		if (notify->extra[0])
			fprintf(pset.queryFout, _("Asynchronous notification \"%s\" with payload \"%s\" received from server process with PID %d.\n"),
					notify->relname, notify->extra, notify->be_pid);
		else
			fprintf(pset.queryFout, _("Asynchronous notification \"%s\" received from server process with PID %d.\n"),
					notify->relname, notify->be_pid);
		fflush(pset.queryFout);
		PQfreemem(notify);
		PQconsumeInput(pset.db);
	}
}


/*
 * PrintQueryTuples: assuming query result is OK, print its tuples
 *
 * We use the options given by opt unless that's NULL, in which case
 * we use pset.popt.
 *
 * Output is to printQueryFout unless that's NULL, in which case
 * we use pset.queryFout.
 *
 * Returns true if successful, false otherwise.
 */
static bool
PrintQueryTuples(const PGresult *result, const printQueryOpt *opt,
				 FILE *printQueryFout)
{
	bool		ok = true;
	FILE	   *fout = printQueryFout ? printQueryFout : pset.queryFout;

	printQuery(result, opt ? opt : &pset.popt, fout, false, pset.logfile);
	fflush(fout);
	if (ferror(fout))
	{
		pg_log_error("could not print result table: %m");
		ok = false;
	}

	return ok;
}


/*
 * StoreQueryTuple: assuming query result is OK, save data into variables
 *
 * Returns true if successful, false otherwise.
 */
static bool
StoreQueryTuple(const PGresult *result)
{
	bool		success = true;

	if (PQntuples(result) < 1)
	{
		pg_log_error("no rows returned for \\gset");
		success = false;
	}
	else if (PQntuples(result) > 1)
	{
		pg_log_error("more than one row returned for \\gset");
		success = false;
	}
	else
	{
		int			i;

		for (i = 0; i < PQnfields(result); i++)
		{
			char	   *colname = PQfname(result, i);
			char	   *varname;
			char	   *value;

			/* concatenate prefix and column name */
			varname = psprintf("%s%s", pset.gset_prefix, colname);

			if (VariableHasHook(pset.vars, varname))
			{
				/*
				 * YB: the following code is commented out since we don't have
				 * pg_log_warning macro. Porting over pg_log_warning would
				 * require other commits which can be done later.
				 *
				 *	pg_log_warning("attempt to \\gset into specially treated variable \"%s\" ignored",
				 *				   varname);
				 */
				continue;
			}

			if (!PQgetisnull(result, 0, i))
				value = PQgetvalue(result, 0, i);
			else
			{
				/* for NULL value, unset rather than set the variable */
				value = NULL;
			}

			if (!SetVariable(pset.vars, varname, value))
			{
				free(varname);
				success = false;
				break;
			}

			free(varname);
		}
	}

	return success;
}


/*
 * ExecQueryTuples: assuming query result is OK, execute each query
 * result field as a SQL statement
 *
 * Returns true if successful, false otherwise.
 */
static bool
ExecQueryTuples(const PGresult *result)
{
	bool		success = true;
	int			nrows = PQntuples(result);
	int			ncolumns = PQnfields(result);
	int			r,
				c;

	/*
	 * We must turn off gexec_flag to avoid infinite recursion.  Note that
	 * this allows ExecQueryUsingCursor to be applied to the individual query
	 * results.  SendQuery prevents it from being applied when fetching the
	 * queries-to-execute, because it can't handle recursion either.
	 */
	pset.gexec_flag = false;

	for (r = 0; r < nrows; r++)
	{
		for (c = 0; c < ncolumns; c++)
		{
			if (!PQgetisnull(result, r, c))
			{
				const char *query = PQgetvalue(result, r, c);

				/* Abandon execution if cancel_pressed */
				if (cancel_pressed)
					goto loop_exit;

				/*
				 * ECHO_ALL mode should echo these queries, but SendQuery
				 * assumes that MainLoop did that, so we have to do it here.
				 */
				if (pset.echo == PSQL_ECHO_ALL && !pset.singlestep)
				{
					puts(query);
					fflush(stdout);
				}

				if (!SendQuery(query))
				{
					/* Error - abandon execution if ON_ERROR_STOP */
					success = false;
					if (pset.on_error_stop)
						goto loop_exit;
				}
			}
		}
	}

loop_exit:

	/*
	 * Restore state.  We know gexec_flag was on, else we'd not be here. (We
	 * also know it'll get turned off at end of command, but that's not ours
	 * to do here.)
	 */
	pset.gexec_flag = true;

	/* Return true if all queries were successful */
	return success;
}


/*
 * Marshal the COPY data.  Either path will get the
 * connection out of its COPY state, then call PQresultStatus()
 * once and report any error.  Return whether all was ok.
 *
 * For COPY OUT, direct the output to copystream, or discard if that's NULL.
 * For COPY IN, use pset.copyStream as data source if it's set,
 * otherwise cur_cmd_source.
 *
 * Update *resultp if further processing is necessary; set to NULL otherwise.
 * Return a result when queryFout can safely output a result status: on COPY
 * IN, or on COPY OUT if written to something other than pset.queryFout.
 * Returning NULL prevents the command status from being printed, which we
 * want if the status line doesn't get taken as part of the COPY data.
 */
static bool
HandleCopyResult(PGresult **resultp, FILE *copystream)
{
	bool		success;
	PGresult   *copy_result;
	ExecStatusType result_status = PQresultStatus(*resultp);

	Assert(result_status == PGRES_COPY_OUT ||
		   result_status == PGRES_COPY_IN);

	SetCancelConn(pset.db);

	if (result_status == PGRES_COPY_OUT)
	{
		success = handleCopyOut(pset.db,
								copystream,
								&copy_result)
			&& (copystream != NULL);

		/*
		 * Suppress status printing if the report would go to the same place
		 * as the COPY data just went.  Note this doesn't prevent error
		 * reporting, since handleCopyOut did that.
		 */
		if (copystream == pset.queryFout)
		{
			PQclear(copy_result);
			copy_result = NULL;
		}
	}
	else
	{
		/* COPY IN */
		/* Ignore the copystream argument passed to the function */
		copystream = pset.copyStream ? pset.copyStream : pset.cur_cmd_source;
		success = handleCopyIn(pset.db,
							   copystream,
							   PQbinaryTuples(*resultp),
							   &copy_result);
	}
	ResetCancelConn();

	/*
	 * Replace the PGRES_COPY_OUT/IN result with COPY command's exit status,
	 * or with NULL if we want to suppress printing anything.
	 */
	PQclear(*resultp);
	*resultp = copy_result;

	return success;
}

/*
 * PrintQueryStatus: report command status as required
 *
 * Note: Utility function for use by PrintQueryResult() only.
 */
static void
PrintQueryStatus(PGresult *result, FILE *printQueryFout)
{
	char		buf[16];
	FILE	   *fout = printQueryFout ? printQueryFout : pset.queryFout;

	if (!pset.quiet)
	{
		if (pset.popt.topt.format == PRINT_HTML)
		{
			fputs("<p>", fout);
			html_escaped_print(PQcmdStatus(result), fout);
			fputs("</p>\n", fout);
		}
		else
			fprintf(fout, "%s\n", PQcmdStatus(result));
		fflush(fout);
	}

	if (pset.logfile)
		fprintf(pset.logfile, "%s\n", PQcmdStatus(result));

	snprintf(buf, sizeof(buf), "%u", (unsigned int) PQoidValue(result));
	SetVariable(pset.vars, "LASTOID", buf);
}


/*
 * PrintQueryResult: print out (or store or execute) query result as required
 *
 * Note: Utility function for use by SendQuery() only.
 *
 * last is true if this is the last result of a command string.
 * opt and printQueryFout are defined as for PrintQueryTuples.
 * printStatusFout is where to send command status; NULL means pset.queryFout.
 *
 * Returns true if the query executed successfully, false otherwise.
 */
static bool
PrintQueryResult(PGresult *result, bool last,
				 const printQueryOpt *opt, FILE *printQueryFout,
				 FILE *printStatusFout)
{
	bool		success;
	const char *cmdstatus;

	if (!result)
		return false;

	switch (PQresultStatus(result))
	{
		case PGRES_TUPLES_OK:
			/* store or execute or print the data ... */
			if (last && pset.gset_prefix)
				success = StoreQueryTuple(result);
			else if (last && pset.gexec_flag)
				success = ExecQueryTuples(result);
			else if (last && pset.crosstab_flag)
				success = PrintResultInCrosstab(result);
			else if (last || pset.show_all_results)
				success = PrintQueryTuples(result, opt, printQueryFout);
			else
				success = true;

			/* if it's INSERT/UPDATE/DELETE RETURNING, also print status */
			if (last || pset.show_all_results)
			{
				cmdstatus = PQcmdStatus(result);
				if (strncmp(cmdstatus, "INSERT", 6) == 0 ||
					strncmp(cmdstatus, "UPDATE", 6) == 0 ||
					strncmp(cmdstatus, "DELETE", 6) == 0)
					PrintQueryStatus(result, printStatusFout);
			}

			break;

		case PGRES_COMMAND_OK:
			if (last || pset.show_all_results)
				PrintQueryStatus(result, printStatusFout);
			success = true;
			break;

		case PGRES_EMPTY_QUERY:
			success = true;
			break;

		case PGRES_COPY_OUT:
		case PGRES_COPY_IN:
			/* nothing to do here: already processed */
			success = true;
			break;

		case PGRES_BAD_RESPONSE:
		case PGRES_NONFATAL_ERROR:
		case PGRES_FATAL_ERROR:
			success = false;
			break;

		default:
			success = false;
			pg_log_error("unexpected PQresultStatus: %d",
						 PQresultStatus(result));
			break;
	}

	return success;
}

/*
 * SendQuery: send the query string to the backend
 * (and print out result)
 *
 * Note: This is the "front door" way to send a query. That is, use it to
 * send queries actually entered by the user. These queries will be subject to
 * single step mode.
 * To send "back door" queries (generated by slash commands, etc.) in a
 * controlled way, use PSQLexec().
 *
 * Returns true if the query executed successfully, false otherwise.
 */
bool
SendQuery(const char *query)
{
	bool		timing = pset.timing;
	PGTransactionStatusType transaction_status;
	double		elapsed_msec = 0;
	bool		OK = false;
	int			i;
	bool		on_error_rollback_savepoint = false;
	bool		svpt_gone = false;

	if (!pset.db)
	{
		pg_log_error("You are currently not connected to a database.");
		goto sendquery_cleanup;
	}

	if (pset.singlestep)
	{
		char		buf[3];

		fflush(stderr);
		printf(_("***(Single step mode: verify command)*******************************************\n"
				 "%s\n"
				 "***(press return to proceed or enter x and return to cancel)********************\n"),
			   query);
		fflush(stdout);
		if (fgets(buf, sizeof(buf), stdin) != NULL)
			if (buf[0] == 'x')
				goto sendquery_cleanup;
		if (cancel_pressed)
			goto sendquery_cleanup;
	}
	else if (pset.echo == PSQL_ECHO_QUERIES)
	{
		puts(query);
		fflush(stdout);
	}

	if (pset.logfile)
	{
		fprintf(pset.logfile,
				_("********* QUERY **********\n"
				  "%s\n"
				  "**************************\n\n"), query);
		fflush(pset.logfile);
	}

	SetCancelConn(pset.db);

	transaction_status = PQtransactionStatus(pset.db);

	if (transaction_status == PQTRANS_IDLE &&
		!pset.autocommit &&
		!command_no_begin(query))
	{
		PGresult   *result;

		result = PQexec(pset.db, "BEGIN");
		if (PQresultStatus(result) != PGRES_COMMAND_OK)
		{
			pg_log_info("%s", PQerrorMessage(pset.db));
			ClearOrSaveResult(result);
			goto sendquery_cleanup;
		}
		ClearOrSaveResult(result);
		transaction_status = PQtransactionStatus(pset.db);
	}

	if (transaction_status == PQTRANS_INTRANS &&
		pset.on_error_rollback != PSQL_ERROR_ROLLBACK_OFF &&
		(pset.cur_cmd_interactive ||
		 pset.on_error_rollback == PSQL_ERROR_ROLLBACK_ON))
	{
		PGresult   *result;

		result = PQexec(pset.db, "SAVEPOINT pg_psql_temporary_savepoint");
		if (PQresultStatus(result) != PGRES_COMMAND_OK)
		{
			pg_log_info("%s", PQerrorMessage(pset.db));
			ClearOrSaveResult(result);
			goto sendquery_cleanup;
		}
		ClearOrSaveResult(result);
		on_error_rollback_savepoint = true;
	}

	if (pset.gdesc_flag)
	{
		/* Describe query's result columns, without executing it */
		OK = DescribeQuery(query, &elapsed_msec);
	}
	else if (pset.fetch_count <= 0 || pset.gexec_flag ||
			 pset.crosstab_flag || !is_select_command(query))
	{
		/* Default fetch-it-all-and-print mode */
		OK = (ExecQueryAndProcessResults(query, &elapsed_msec, &svpt_gone, false, NULL, NULL) > 0);
	}
	else
	{
		/* Fetch-in-segments mode */
		OK = ExecQueryUsingCursor(query, &elapsed_msec);
	}

	if (!OK && pset.echo == PSQL_ECHO_ERRORS)
		pg_log_info("STATEMENT:  %s", query);

	/* If we made a temporary savepoint, possibly release/rollback */
	if (on_error_rollback_savepoint)
	{
		const char *svptcmd = NULL;

		transaction_status = PQtransactionStatus(pset.db);

		switch (transaction_status)
		{
			case PQTRANS_INERROR:
				/* We always rollback on an error */
				svptcmd = "ROLLBACK TO pg_psql_temporary_savepoint";
				break;

			case PQTRANS_IDLE:
				/* If they are no longer in a transaction, then do nothing */
				break;

			case PQTRANS_INTRANS:

				/*
				 * Release our savepoint, but do nothing if they are messing
				 * with savepoints themselves
				 */
				if (!svpt_gone)
					svptcmd = "RELEASE pg_psql_temporary_savepoint";
				break;

			case PQTRANS_ACTIVE:
			case PQTRANS_UNKNOWN:
			default:
				OK = false;
				/* PQTRANS_UNKNOWN is expected given a broken connection. */
				if (transaction_status != PQTRANS_UNKNOWN || ConnectionUp())
					pg_log_error("unexpected transaction status (%d)",
								 transaction_status);
				break;
		}

		if (svptcmd)
		{
			PGresult   *svptres;

			svptres = PQexec(pset.db, svptcmd);
			if (PQresultStatus(svptres) != PGRES_COMMAND_OK)
			{
				pg_log_info("%s", PQerrorMessage(pset.db));
				ClearOrSaveResult(svptres);
				OK = false;

				goto sendquery_cleanup;
			}
			PQclear(svptres);
		}
	}

	/* Possible microtiming output */
	if (timing)
		PrintTiming(elapsed_msec);

	/* check for events that may occur during query execution */

	if (pset.encoding != PQclientEncoding(pset.db) &&
		PQclientEncoding(pset.db) >= 0)
	{
		/* track effects of SET CLIENT_ENCODING */
		pset.encoding = PQclientEncoding(pset.db);
		pset.popt.topt.encoding = pset.encoding;
		SetVariable(pset.vars, "ENCODING",
					pg_encoding_to_char(pset.encoding));
	}

	PrintNotifications();

	/* perform cleanup that should occur after any attempted query */

sendquery_cleanup:

	/* global cancellation reset */
	ResetCancelConn();

	/* reset \g's output-to-filename trigger */
	if (pset.gfname)
	{
		free(pset.gfname);
		pset.gfname = NULL;
	}

	/* restore print settings if \g changed them */
	if (pset.gsavepopt)
	{
		restorePsetInfo(&pset.popt, pset.gsavepopt);
		pset.gsavepopt = NULL;
	}

	/* reset \gset trigger */
	if (pset.gset_prefix)
	{
		free(pset.gset_prefix);
		pset.gset_prefix = NULL;
	}

	/* reset \gdesc trigger */
	pset.gdesc_flag = false;

	/* reset \gexec trigger */
	pset.gexec_flag = false;

	/* reset \crosstabview trigger */
	pset.crosstab_flag = false;
	for (i = 0; i < lengthof(pset.ctv_args); i++)
	{
		pg_free(pset.ctv_args[i]);
		pset.ctv_args[i] = NULL;
	}

	return OK;
}


/*
 * DescribeQuery: describe the result columns of a query, without executing it
 *
 * Returns true if the operation executed successfully, false otherwise.
 *
 * If pset.timing is on, total query time (exclusive of result-printing) is
 * stored into *elapsed_msec.
 */
static bool
DescribeQuery(const char *query, double *elapsed_msec)
{
	bool		timing = pset.timing;
	PGresult   *result;
	bool		OK;
	instr_time	before,
				after;

	*elapsed_msec = 0;

	if (timing)
		INSTR_TIME_SET_CURRENT(before);

	/*
	 * To parse the query but not execute it, we prepare it, using the unnamed
	 * prepared statement.  This is invisible to psql users, since there's no
	 * way to access the unnamed prepared statement from psql user space. The
	 * next Parse or Query protocol message would overwrite the statement
	 * anyway.  (So there's no great need to clear it when done, which is a
	 * good thing because libpq provides no easy way to do that.)
	 */
	result = PQprepare(pset.db, "", query, 0, NULL);
	if (PQresultStatus(result) != PGRES_COMMAND_OK)
	{
		pg_log_info("%s", PQerrorMessage(pset.db));
		SetResultVariables(result, false);
		ClearOrSaveResult(result);
		return false;
	}
	PQclear(result);

	result = PQdescribePrepared(pset.db, "");
	OK = AcceptResult(result, true) &&
		(PQresultStatus(result) == PGRES_COMMAND_OK);
	if (OK && result)
	{
		if (PQnfields(result) > 0)
		{
			PQExpBufferData buf;
			int			i;

			initPQExpBuffer(&buf);

			printfPQExpBuffer(&buf,
							  "SELECT name AS \"%s\", pg_catalog.format_type(tp, tpm) AS \"%s\"\n"
							  "FROM (VALUES ",
							  gettext_noop("Column"),
							  gettext_noop("Type"));

			for (i = 0; i < PQnfields(result); i++)
			{
				const char *name;
				char	   *escname;

				if (i > 0)
					appendPQExpBufferStr(&buf, ",");

				name = PQfname(result, i);
				escname = PQescapeLiteral(pset.db, name, strlen(name));

				if (escname == NULL)
				{
					pg_log_info("%s", PQerrorMessage(pset.db));
					PQclear(result);
					termPQExpBuffer(&buf);
					return false;
				}

				appendPQExpBuffer(&buf, "(%s, '%u'::pg_catalog.oid, %d)",
								  escname,
								  PQftype(result, i),
								  PQfmod(result, i));

				PQfreemem(escname);
			}

			appendPQExpBufferStr(&buf, ") s(name, tp, tpm)");
			PQclear(result);

			result = PQexec(pset.db, buf.data);
			OK = AcceptResult(result, true);

			if (timing)
			{
				INSTR_TIME_SET_CURRENT(after);
				INSTR_TIME_SUBTRACT(after, before);
				*elapsed_msec += INSTR_TIME_GET_MILLISEC(after);
			}

			if (OK && result)
				OK = PrintQueryResult(result, true, NULL, NULL, NULL);

			termPQExpBuffer(&buf);
		}
		else
			fprintf(pset.queryFout,
					_("The command has no result, or the result has no columns.\n"));
	}

	SetResultVariables(result, OK);
	ClearOrSaveResult(result);

	return OK;
}


/*
 * ExecQueryAndProcessResults: utility function for use by SendQuery()
 * and PSQLexecWatch().
 *
 * Sends query and cycles through PGresult objects.
 *
 * If our command string contained a COPY FROM STDIN or COPY TO STDOUT, the
 * PGresult associated with these commands must be processed by providing an
 * input or output stream.  In that event, we'll marshal data for the COPY.
 *
 * For other commands, the results are processed normally, depending on their
 * status.
 *
 * Returns 1 on complete success, 0 on interrupt and -1 or errors.  Possible
 * failure modes include purely client-side problems; check the transaction
 * status for the server-side opinion.
 *
 * Note that on a combined query, failure does not mean that nothing was
 * committed.
 */
static int
ExecQueryAndProcessResults(const char *query,
						   double *elapsed_msec, bool *svpt_gone_p,
						   bool is_watch,
						   const printQueryOpt *opt, FILE *printQueryFout)
{
	bool		timing = pset.timing;
	bool		success;
	instr_time	before,
				after;
	PGresult   *result;
	FILE	   *gfile_fout = NULL;
	bool		gfile_is_pipe = false;

	if (timing)
		INSTR_TIME_SET_CURRENT(before);

	success = PQsendQuery(pset.db, query);

	if (!success)
	{
		const char *error = PQerrorMessage(pset.db);

		if (strlen(error))
			pg_log_info("%s", error);

		CheckConnection();

		return -1;
	}

	/*
	 * If SIGINT is sent while the query is processing, the interrupt will be
	 * consumed.  The user's intention, though, is to cancel the entire watch
	 * process, so detect a sent cancellation request and exit in this case.
	 */
	if (is_watch && cancel_pressed)
	{
		ClearOrSaveAllResults();
		return 0;
	}

	/* first result */
	result = PQgetResult(pset.db);

	while (result != NULL)
	{
		ExecStatusType result_status;
		PGresult   *next_result;
		bool		last;

		if (!AcceptResult(result, false))
		{
			/*
			 * Some error occured, either a server-side failure or a failure
			 * to submit the command string.  Record that.
			 */
			const char *error = PQresultErrorMessage(result);

			if (strlen(error))
				pg_log_info("%s", error);

			CheckConnection();
			if (!is_watch)
				SetResultVariables(result, false);

			/* keep the result status before clearing it */
			result_status = PQresultStatus(result);
			ClearOrSaveResult(result);
			success = false;

			/*
			 * switch to next result
			 */
			if (result_status == PGRES_COPY_BOTH ||
				result_status == PGRES_COPY_OUT ||
				result_status == PGRES_COPY_IN)

				/*
				 * For some obscure reason PQgetResult does *not* return a
				 * NULL in copy cases despite the result having been cleared,
				 * but keeps returning an "empty" result that we have to
				 * ignore manually.
				 */
				result = NULL;
			else
				result = PQgetResult(pset.db);

			/*
			 * Get current timing measure in case an error occurs
			 */
			if (timing)
			{
				INSTR_TIME_SET_CURRENT(after);
				INSTR_TIME_SUBTRACT(after, before);
				*elapsed_msec = INSTR_TIME_GET_MILLISEC(after);
			}

			continue;
		}
		else if (svpt_gone_p && !*svpt_gone_p)
		{
			/*
			 * Check if the user ran any command that would destroy our
			 * internal savepoint: If the user did COMMIT AND CHAIN, RELEASE
			 * or ROLLBACK, our savepoint is gone. If they issued a SAVEPOINT,
			 * releasing ours would remove theirs.
			 */
			const char *cmd = PQcmdStatus(result);

			*svpt_gone_p = (strcmp(cmd, "COMMIT") == 0 ||
							strcmp(cmd, "SAVEPOINT") == 0 ||
							strcmp(cmd, "RELEASE") == 0 ||
							strcmp(cmd, "ROLLBACK") == 0);
		}

		result_status = PQresultStatus(result);

		/* must handle COPY before changing the current result */
		Assert(result_status != PGRES_COPY_BOTH);
		if (result_status == PGRES_COPY_IN ||
			result_status == PGRES_COPY_OUT)
		{
			FILE	   *copy_stream = NULL;

			/*
			 * For COPY OUT, direct the output to the default place (probably
			 * a pager pipe) for \watch, or to pset.copyStream for \copy,
			 * otherwise to pset.gfname if that's set, otherwise to
			 * pset.queryFout.
			 */
			if (result_status == PGRES_COPY_OUT)
			{
				if (is_watch)
				{
					/* invoked by \watch */
					copy_stream = printQueryFout ? printQueryFout : pset.queryFout;
				}
				else if (pset.copyStream)
				{
					/* invoked by \copy */
					copy_stream = pset.copyStream;
				}
				else if (pset.gfname)
				{
					/* send to \g file, which we may have opened already */
					if (gfile_fout == NULL)
					{
						if (openQueryOutputFile(pset.gfname,
												&gfile_fout, &gfile_is_pipe))
						{
							if (gfile_is_pipe)
								disable_sigpipe_trap();
							copy_stream = gfile_fout;
						}
						else
							success = false;
					}
					else
						copy_stream = gfile_fout;
				}
				else
				{
					/* fall back to the generic query output stream */
					copy_stream = pset.queryFout;
				}
			}

			/*
			 * Even if the output stream could not be opened, we call
			 * HandleCopyResult() with a NULL output stream to collect and
			 * discard the COPY data.
			 */
			success &= HandleCopyResult(&result, copy_stream);
		}

		/*
		 * Check PQgetResult() again.  In the typical case of a single-command
		 * string, it will return NULL.  Otherwise, we'll have other results
		 * to process.  We need to do that to check whether this is the last.
		 */
		next_result = PQgetResult(pset.db);
		last = (next_result == NULL);

		/*
		 * Update current timing measure.
		 *
		 * It will include the display of previous results, if any. This
		 * cannot be helped because the server goes on processing further
		 * queries anyway while the previous ones are being displayed. The
		 * parallel execution of the client display hides the server time when
		 * it is shorter.
		 *
		 * With combined queries, timing must be understood as an upper bound
		 * of the time spent processing them.
		 */
		if (timing)
		{
			INSTR_TIME_SET_CURRENT(after);
			INSTR_TIME_SUBTRACT(after, before);
			*elapsed_msec = INSTR_TIME_GET_MILLISEC(after);
		}

		/* this may or may not print something depending on settings */
		if (result != NULL)
		{
			/*
			 * If results need to be printed into the file specified by \g,
			 * open it, unless we already did.  Note that when pset.gfname is
			 * set, the passed-in value of printQueryFout is not used for
			 * tuple output, but it's still used for status output.
			 */
			FILE	   *tuples_fout = printQueryFout;
			bool		do_print = true;

			if (PQresultStatus(result) == PGRES_TUPLES_OK &&
				pset.gfname)
			{
				if (gfile_fout == NULL)
				{
					if (openQueryOutputFile(pset.gfname,
											&gfile_fout, &gfile_is_pipe))
					{
						if (gfile_is_pipe)
							disable_sigpipe_trap();
					}
					else
						success = do_print = false;
				}
				tuples_fout = gfile_fout;
			}
			if (do_print)
				success &= PrintQueryResult(result, last, opt,
											tuples_fout, printQueryFout);
		}

		/* set variables on last result if all went well */
		if (!is_watch && last && success)
			SetResultVariables(result, true);

		ClearOrSaveResult(result);
		result = next_result;

		if (cancel_pressed)
		{
			ClearOrSaveAllResults();
			break;
		}
	}

	/* close \g file if we opened it */
	if (gfile_fout)
	{
		if (gfile_is_pipe)
		{
			pclose(gfile_fout);
			restore_sigpipe_trap();
		}
		else
			fclose(gfile_fout);
	}

	/* may need this to recover from conn loss during COPY */
	if (!CheckConnection())
		return -1;

	return cancel_pressed ? 0 : success ? 1 : -1;
}


/*
 * ExecQueryUsingCursor: run a SELECT-like query using a cursor
 *
 * This feature allows result sets larger than RAM to be dealt with.
 *
 * Returns true if the query executed successfully, false otherwise.
 *
 * If pset.timing is on, total query time (exclusive of result-printing) is
 * stored into *elapsed_msec.
 */
static bool
ExecQueryUsingCursor(const char *query, double *elapsed_msec)
{
	bool		OK = true;
	PGresult   *result;
	PQExpBufferData buf;
	printQueryOpt my_popt = pset.popt;
	bool		timing = pset.timing;
	FILE	   *fout;
	bool		is_pipe;
	bool		is_pager = false;
	bool		started_txn = false;
	int64		total_tuples = 0;
	int			ntuples;
	int			fetch_count;
	char		fetch_cmd[64];
	instr_time	before,
				after;
	int			flush_error;

	*elapsed_msec = 0;

	/* initialize print options for partial table output */
	my_popt.topt.start_table = true;
	my_popt.topt.stop_table = false;
	my_popt.topt.prior_records = 0;

	if (timing)
		INSTR_TIME_SET_CURRENT(before);

	/* if we're not in a transaction, start one */
	if (PQtransactionStatus(pset.db) == PQTRANS_IDLE)
	{
		result = PQexec(pset.db, "BEGIN");
		OK = AcceptResult(result, true) &&
			(PQresultStatus(result) == PGRES_COMMAND_OK);
		ClearOrSaveResult(result);
		if (!OK)
			return false;
		started_txn = true;
	}

	/* Send DECLARE CURSOR */
	initPQExpBuffer(&buf);
	appendPQExpBuffer(&buf, "DECLARE _psql_cursor NO SCROLL CURSOR FOR\n%s",
					  query);

	result = PQexec(pset.db, buf.data);
	OK = AcceptResult(result, true) &&
		(PQresultStatus(result) == PGRES_COMMAND_OK);
	if (!OK)
		SetResultVariables(result, OK);
	ClearOrSaveResult(result);
	termPQExpBuffer(&buf);
	if (!OK)
		goto cleanup;

	if (timing)
	{
		INSTR_TIME_SET_CURRENT(after);
		INSTR_TIME_SUBTRACT(after, before);
		*elapsed_msec += INSTR_TIME_GET_MILLISEC(after);
	}

	/*
	 * In \gset mode, we force the fetch count to be 2, so that we will throw
	 * the appropriate error if the query returns more than one row.
	 */
	if (pset.gset_prefix)
		fetch_count = 2;
	else
		fetch_count = pset.fetch_count;

	snprintf(fetch_cmd, sizeof(fetch_cmd),
			 "FETCH FORWARD %d FROM _psql_cursor",
			 fetch_count);

	/* prepare to write output to \g argument, if any */
	if (pset.gfname)
	{
		if (!openQueryOutputFile(pset.gfname, &fout, &is_pipe))
		{
			OK = false;
			goto cleanup;
		}
		if (is_pipe)
			disable_sigpipe_trap();
	}
	else
	{
		fout = pset.queryFout;
		is_pipe = false;		/* doesn't matter */
	}

	/* clear any pre-existing error indication on the output stream */
	clearerr(fout);

	for (;;)
	{
		if (timing)
			INSTR_TIME_SET_CURRENT(before);

		/* get fetch_count tuples at a time */
		result = PQexec(pset.db, fetch_cmd);

		if (timing)
		{
			INSTR_TIME_SET_CURRENT(after);
			INSTR_TIME_SUBTRACT(after, before);
			*elapsed_msec += INSTR_TIME_GET_MILLISEC(after);
		}

		if (PQresultStatus(result) != PGRES_TUPLES_OK)
		{
			/* shut down pager before printing error message */
			if (is_pager)
			{
				ClosePager(fout);
				is_pager = false;
			}

			OK = AcceptResult(result, true);
			Assert(!OK);
			SetResultVariables(result, OK);
			ClearOrSaveResult(result);
			break;
		}

		if (pset.gset_prefix)
		{
			/* StoreQueryTuple will complain if not exactly one row */
			OK = StoreQueryTuple(result);
			ClearOrSaveResult(result);
			break;
		}

		/*
		 * Note we do not deal with \gdesc, \gexec or \crosstabview modes here
		 */

		ntuples = PQntuples(result);
		total_tuples += ntuples;

		if (ntuples < fetch_count)
		{
			/* this is the last result set, so allow footer decoration */
			my_popt.topt.stop_table = true;
		}
		else if (fout == stdout && !is_pager)
		{
			/*
			 * If query requires multiple result sets, hack to ensure that
			 * only one pager instance is used for the whole mess
			 */
			fout = PageOutput(INT_MAX, &(my_popt.topt));
			is_pager = true;
		}

		printQuery(result, &my_popt, fout, is_pager, pset.logfile);

		ClearOrSaveResult(result);

		/* after the first result set, disallow header decoration */
		my_popt.topt.start_table = false;
		my_popt.topt.prior_records += ntuples;

		/*
		 * Make sure to flush the output stream, so intermediate results are
		 * visible to the client immediately.  We check the results because if
		 * the pager dies/exits/etc, there's no sense throwing more data at
		 * it.
		 */
		flush_error = fflush(fout);

		/*
		 * Check if we are at the end, if a cancel was pressed, or if there
		 * were any errors either trying to flush out the results, or more
		 * generally on the output stream at all.  If we hit any errors
		 * writing things to the stream, we presume $PAGER has disappeared and
		 * stop bothering to pull down more data.
		 */
		if (ntuples < fetch_count || cancel_pressed || flush_error ||
			ferror(fout))
			break;
	}

	if (pset.gfname)
	{
		/* close \g argument file/pipe */
		if (is_pipe)
		{
			pclose(fout);
			restore_sigpipe_trap();
		}
		else
			fclose(fout);
	}
	else if (is_pager)
	{
		/* close transient pager */
		ClosePager(fout);
	}

	if (OK)
	{
		/*
		 * We don't have a PGresult here, and even if we did it wouldn't have
		 * the right row count, so fake SetResultVariables().  In error cases,
		 * we already set the result variables above.
		 */
		char		buf[32];

		SetVariable(pset.vars, "ERROR", "false");
		SetVariable(pset.vars, "SQLSTATE", "00000");
		snprintf(buf, sizeof(buf), INT64_FORMAT, total_tuples);
		SetVariable(pset.vars, "ROW_COUNT", buf);
	}

cleanup:
	if (timing)
		INSTR_TIME_SET_CURRENT(before);

	/*
	 * We try to close the cursor on either success or failure, but on failure
	 * ignore the result (it's probably just a bleat about being in an aborted
	 * transaction)
	 */
	result = PQexec(pset.db, "CLOSE _psql_cursor");
	if (OK)
	{
		OK = AcceptResult(result, true) &&
			(PQresultStatus(result) == PGRES_COMMAND_OK);
		ClearOrSaveResult(result);
	}
	else
		PQclear(result);

	if (started_txn)
	{
		result = PQexec(pset.db, OK ? "COMMIT" : "ROLLBACK");
		OK &= AcceptResult(result, true) &&
			(PQresultStatus(result) == PGRES_COMMAND_OK);
		ClearOrSaveResult(result);
	}

	if (timing)
	{
		INSTR_TIME_SET_CURRENT(after);
		INSTR_TIME_SUBTRACT(after, before);
		*elapsed_msec += INSTR_TIME_GET_MILLISEC(after);
	}

	return OK;
}


/*
 * Advance the given char pointer over white space and SQL comments.
 */
static const char *
skip_white_space(const char *query)
{
	int			cnestlevel = 0; /* slash-star comment nest level */

	while (*query)
	{
		int			mblen = PQmblenBounded(query, pset.encoding);

		/*
		 * Note: we assume the encoding is a superset of ASCII, so that for
		 * example "query[0] == '/'" is meaningful.  However, we do NOT assume
		 * that the second and subsequent bytes of a multibyte character
		 * couldn't look like ASCII characters; so it is critical to advance
		 * by mblen, not 1, whenever we haven't exactly identified the
		 * character we are skipping over.
		 */
		if (isspace((unsigned char) *query))
			query += mblen;
		else if (query[0] == '/' && query[1] == '*')
		{
			cnestlevel++;
			query += 2;
		}
		else if (cnestlevel > 0 && query[0] == '*' && query[1] == '/')
		{
			cnestlevel--;
			query += 2;
		}
		else if (cnestlevel == 0 && query[0] == '-' && query[1] == '-')
		{
			query += 2;

			/*
			 * We have to skip to end of line since any slash-star inside the
			 * -- comment does NOT start a slash-star comment.
			 */
			while (*query)
			{
				if (*query == '\n')
				{
					query++;
					break;
				}
				query += PQmblenBounded(query, pset.encoding);
			}
		}
		else if (cnestlevel > 0)
			query += mblen;
		else
			break;				/* found first token */
	}

	return query;
}


/*
 * Check whether a command is one of those for which we should NOT start
 * a new transaction block (ie, send a preceding BEGIN).
 *
 * These include the transaction control statements themselves, plus
 * certain statements that the backend disallows inside transaction blocks.
 */
static bool
command_no_begin(const char *query)
{
	int			wordlen;

	/*
	 * First we must advance over any whitespace and comments.
	 */
	query = skip_white_space(query);

	/*
	 * Check word length (since "beginx" is not "begin").
	 */
	wordlen = 0;
	while (isalpha((unsigned char) query[wordlen]))
		wordlen += PQmblenBounded(&query[wordlen], pset.encoding);

	/*
	 * Transaction control commands.  These should include every keyword that
	 * gives rise to a TransactionStmt in the backend grammar, except for the
	 * savepoint-related commands.
	 *
	 * (We assume that START must be START TRANSACTION, since there is
	 * presently no other "START foo" command.)
	 */
	if (wordlen == 5 && pg_strncasecmp(query, "abort", 5) == 0)
		return true;
	if (wordlen == 5 && pg_strncasecmp(query, "begin", 5) == 0)
		return true;
	if (wordlen == 5 && pg_strncasecmp(query, "start", 5) == 0)
		return true;
	if (wordlen == 6 && pg_strncasecmp(query, "commit", 6) == 0)
		return true;
	if (wordlen == 3 && pg_strncasecmp(query, "end", 3) == 0)
		return true;
	if (wordlen == 8 && pg_strncasecmp(query, "rollback", 8) == 0)
		return true;
	if (wordlen == 7 && pg_strncasecmp(query, "prepare", 7) == 0)
	{
		/* PREPARE TRANSACTION is a TC command, PREPARE foo is not */
		query += wordlen;

		query = skip_white_space(query);

		wordlen = 0;
		while (isalpha((unsigned char) query[wordlen]))
			wordlen += PQmblenBounded(&query[wordlen], pset.encoding);

		if (wordlen == 11 && pg_strncasecmp(query, "transaction", 11) == 0)
			return true;
		return false;
	}

	/*
	 * Commands not allowed within transactions.  The statements checked for
	 * here should be exactly those that call PreventInTransactionBlock() in
	 * the backend.
	 */
	if (wordlen == 6 && pg_strncasecmp(query, "vacuum", 6) == 0)
		return true;
	if (wordlen == 7 && pg_strncasecmp(query, "cluster", 7) == 0)
	{
		/* CLUSTER with any arguments is allowed in transactions */
		query += wordlen;

		query = skip_white_space(query);

		if (isalpha((unsigned char) query[0]))
			return false;		/* has additional words */
		return true;			/* it's CLUSTER without arguments */
	}

	if (wordlen == 6 && pg_strncasecmp(query, "create", 6) == 0)
	{
		query += wordlen;

		query = skip_white_space(query);

		wordlen = 0;
		while (isalpha((unsigned char) query[wordlen]))
			wordlen += PQmblenBounded(&query[wordlen], pset.encoding);

		if (wordlen == 8 && pg_strncasecmp(query, "database", 8) == 0)
			return true;
		if (wordlen == 10 && pg_strncasecmp(query, "tablespace", 10) == 0)
			return true;

		/* CREATE [UNIQUE] INDEX CONCURRENTLY isn't allowed in xacts */
		if (wordlen == 6 && pg_strncasecmp(query, "unique", 6) == 0)
		{
			query += wordlen;

			query = skip_white_space(query);

			wordlen = 0;
			while (isalpha((unsigned char) query[wordlen]))
				wordlen += PQmblenBounded(&query[wordlen], pset.encoding);
		}

		if (wordlen == 5 && pg_strncasecmp(query, "index", 5) == 0)
		{
			query += wordlen;

			query = skip_white_space(query);

			wordlen = 0;
			while (isalpha((unsigned char) query[wordlen]))
				wordlen += PQmblenBounded(&query[wordlen], pset.encoding);

			if (wordlen == 12 && pg_strncasecmp(query, "concurrently", 12) == 0)
				return true;
		}

		return false;
	}

	if (wordlen == 5 && pg_strncasecmp(query, "alter", 5) == 0)
	{
		query += wordlen;

		query = skip_white_space(query);

		wordlen = 0;
		while (isalpha((unsigned char) query[wordlen]))
			wordlen += PQmblenBounded(&query[wordlen], pset.encoding);

		/* ALTER SYSTEM isn't allowed in xacts */
		if (wordlen == 6 && pg_strncasecmp(query, "system", 6) == 0)
			return true;

		return false;
	}

	/*
	 * Note: these tests will match DROP SYSTEM and REINDEX TABLESPACE, which
	 * aren't really valid commands so we don't care much. The other four
	 * possible matches are correct.
	 */
	if ((wordlen == 4 && pg_strncasecmp(query, "drop", 4) == 0) ||
		(wordlen == 7 && pg_strncasecmp(query, "reindex", 7) == 0))
	{
		query += wordlen;

		query = skip_white_space(query);

		wordlen = 0;
		while (isalpha((unsigned char) query[wordlen]))
			wordlen += PQmblenBounded(&query[wordlen], pset.encoding);

		if (wordlen == 8 && pg_strncasecmp(query, "database", 8) == 0)
			return true;
		if (wordlen == 6 && pg_strncasecmp(query, "system", 6) == 0)
			return true;
		if (wordlen == 10 && pg_strncasecmp(query, "tablespace", 10) == 0)
			return true;
		if (wordlen == 5 && (pg_strncasecmp(query, "index", 5) == 0 ||
							 pg_strncasecmp(query, "table", 5) == 0))
		{
			query += wordlen;
			query = skip_white_space(query);
			wordlen = 0;
			while (isalpha((unsigned char) query[wordlen]))
				wordlen += PQmblenBounded(&query[wordlen], pset.encoding);

			/*
			 * REINDEX [ TABLE | INDEX ] CONCURRENTLY are not allowed in
			 * xacts.
			 */
			if (wordlen == 12 && pg_strncasecmp(query, "concurrently", 12) == 0)
				return true;
		}

		/* DROP INDEX CONCURRENTLY isn't allowed in xacts */
		if (wordlen == 5 && pg_strncasecmp(query, "index", 5) == 0)
		{
			query += wordlen;

			query = skip_white_space(query);

			wordlen = 0;
			while (isalpha((unsigned char) query[wordlen]))
				wordlen += PQmblenBounded(&query[wordlen], pset.encoding);

			if (wordlen == 12 && pg_strncasecmp(query, "concurrently", 12) == 0)
				return true;

			return false;
		}

		return false;
	}

	/* DISCARD ALL isn't allowed in xacts, but other variants are allowed. */
	if (wordlen == 7 && pg_strncasecmp(query, "discard", 7) == 0)
	{
		query += wordlen;

		query = skip_white_space(query);

		wordlen = 0;
		while (isalpha((unsigned char) query[wordlen]))
			wordlen += PQmblenBounded(&query[wordlen], pset.encoding);

		if (wordlen == 3 && pg_strncasecmp(query, "all", 3) == 0)
			return true;
		return false;
	}

	return false;
}


/*
 * Check whether the specified command is a SELECT (or VALUES).
 */
static bool
is_select_command(const char *query)
{
	int			wordlen;

	/*
	 * First advance over any whitespace, comments and left parentheses.
	 */
	for (;;)
	{
		query = skip_white_space(query);
		if (query[0] == '(')
			query++;
		else
			break;
	}

	/*
	 * Check word length (since "selectx" is not "select").
	 */
	wordlen = 0;
	while (isalpha((unsigned char) query[wordlen]))
		wordlen += PQmblenBounded(&query[wordlen], pset.encoding);

	if (wordlen == 6 && pg_strncasecmp(query, "select", 6) == 0)
		return true;

	if (wordlen == 6 && pg_strncasecmp(query, "values", 6) == 0)
		return true;

	return false;
}


/*
 * Test if the current user is a database superuser.
 */
bool
is_superuser(void)
{
	const char *val;

	if (!pset.db)
		return false;

	val = PQparameterStatus(pset.db, "is_superuser");

	if (val && strcmp(val, "on") == 0)
		return true;

	return false;
}


/*
 * Test if the current session uses standard string literals.
 */
bool
standard_strings(void)
{
	const char *val;

	if (!pset.db)
		return false;

	val = PQparameterStatus(pset.db, "standard_conforming_strings");

	if (val && strcmp(val, "on") == 0)
		return true;

	return false;
}


/*
 * Return the session user of the current connection.
 */
const char *
session_username(void)
{
	const char *val;

	if (!pset.db)
		return NULL;

	val = PQparameterStatus(pset.db, "session_authorization");
	if (val)
		return val;
	else
		return PQuser(pset.db);
}


/* expand_tilde
 *
 * substitute '~' with HOME or '~username' with username's home dir
 *
 */
void
expand_tilde(char **filename)
{
	if (!filename || !(*filename))
		return;

	/*
	 * WIN32 doesn't use tilde expansion for file names. Also, it uses tilde
	 * for short versions of long file names, though the tilde is usually
	 * toward the end, not at the beginning.
	 */
#ifndef WIN32

	/* try tilde expansion */
	if (**filename == '~')
	{
		char	   *fn;
		char		oldp,
				   *p;
		struct passwd *pw;
		char		home[MAXPGPATH];

		fn = *filename;
		*home = '\0';

		p = fn + 1;
		while (*p != '/' && *p != '\0')
			p++;

		oldp = *p;
		*p = '\0';

		if (*(fn + 1) == '\0')
			get_home_path(home);	/* ~ or ~/ only */
		else if ((pw = getpwnam(fn + 1)) != NULL)
			strlcpy(home, pw->pw_dir, sizeof(home));	/* ~user */

		*p = oldp;
		if (strlen(home) != 0)
		{
			char	   *newfn;

			newfn = psprintf("%s%s", home, p);
			free(fn);
			*filename = newfn;
		}
	}
#endif
}

/*
 * Checks if connection string starts with either of the valid URI prefix
 * designators.
 *
 * Returns the URI prefix length, 0 if the string doesn't contain a URI prefix.
 *
 * XXX This is a duplicate of the eponymous libpq function.
 */
static int
uri_prefix_length(const char *connstr)
{
	/* The connection URI must start with either of the following designators: */
	static const char uri_designator[] = "postgresql://";
	static const char short_uri_designator[] = "postgres://";

	if (strncmp(connstr, uri_designator,
				sizeof(uri_designator) - 1) == 0)
		return sizeof(uri_designator) - 1;

	if (strncmp(connstr, short_uri_designator,
				sizeof(short_uri_designator) - 1) == 0)
		return sizeof(short_uri_designator) - 1;

	return 0;
}

/*
 * Recognized connection string either starts with a valid URI prefix or
 * contains a "=" in it.
 *
 * Must be consistent with parse_connection_string: anything for which this
 * returns true should at least look like it's parseable by that routine.
 *
 * XXX This is a duplicate of the eponymous libpq function.
 */
bool
recognized_connection_string(const char *connstr)
{
	return uri_prefix_length(connstr) != 0 || strchr(connstr, '=') != NULL;
}
