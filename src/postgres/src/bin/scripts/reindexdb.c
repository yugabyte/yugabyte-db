/*-------------------------------------------------------------------------
 *
 * reindexdb
 *
 * Portions Copyright (c) 1996-2018, PostgreSQL Global Development Group
 *
 * src/bin/scripts/reindexdb.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres_fe.h"
#include "common.h"
#include "fe_utils/simple_list.h"
#include "fe_utils/string_utils.h"


static void reindex_one_database(const ConnParams *cparams,
								 const char *type, const char *name,
								 const char *progname,
					 bool echo, bool verbose);
static void reindex_all_databases(ConnParams *cparams,
					  const char *progname, bool echo,
					  bool quiet, bool verbose);
static void reindex_system_catalogs(const ConnParams *cparams,
						const char *progname, bool echo, bool verbose);
static void help(const char *progname);

int
main(int argc, char *argv[])
{
	static struct option long_options[] = {
		{"host", required_argument, NULL, 'h'},
		{"port", required_argument, NULL, 'p'},
		{"username", required_argument, NULL, 'U'},
		{"no-password", no_argument, NULL, 'w'},
		{"password", no_argument, NULL, 'W'},
		{"echo", no_argument, NULL, 'e'},
		{"quiet", no_argument, NULL, 'q'},
		{"schema", required_argument, NULL, 'S'},
		{"dbname", required_argument, NULL, 'd'},
		{"all", no_argument, NULL, 'a'},
		{"system", no_argument, NULL, 's'},
		{"table", required_argument, NULL, 't'},
		{"index", required_argument, NULL, 'i'},
		{"verbose", no_argument, NULL, 'v'},
		{"maintenance-db", required_argument, NULL, 2},
		{NULL, 0, NULL, 0}
	};

	const char *progname;
	int			optindex;
	int			c;

	const char *dbname = NULL;
	const char *maintenance_db = NULL;
	const char *host = NULL;
	const char *port = NULL;
	const char *username = NULL;
	enum trivalue prompt_password = TRI_DEFAULT;
	ConnParams	cparams;
	bool		syscatalog = false;
	bool		alldb = false;
	bool		echo = false;
	bool		quiet = false;
	bool		verbose = false;
	SimpleStringList indexes = {NULL, NULL};
	SimpleStringList tables = {NULL, NULL};
	SimpleStringList schemas = {NULL, NULL};

	progname = get_progname(argv[0]);
	set_pglocale_pgservice(argv[0], PG_TEXTDOMAIN("pgscripts"));

	handle_help_version_opts(argc, argv, "reindexdb", help);

	/* process command-line options */
	while ((c = getopt_long(argc, argv, "h:p:U:wWeqS:d:ast:i:v", long_options, &optindex)) != -1)
	{
		switch (c)
		{
			case 'h':
				host = pg_strdup(optarg);
				break;
			case 'p':
				port = pg_strdup(optarg);
				break;
			case 'U':
				username = pg_strdup(optarg);
				break;
			case 'w':
				prompt_password = TRI_NO;
				break;
			case 'W':
				prompt_password = TRI_YES;
				break;
			case 'e':
				echo = true;
				break;
			case 'q':
				quiet = true;
				break;
			case 'S':
				simple_string_list_append(&schemas, optarg);
				break;
			case 'd':
				dbname = pg_strdup(optarg);
				break;
			case 'a':
				alldb = true;
				break;
			case 's':
				syscatalog = true;
				break;
			case 't':
				simple_string_list_append(&tables, optarg);
				break;
			case 'i':
				simple_string_list_append(&indexes, optarg);
				break;
			case 'v':
				verbose = true;
				break;
			case 2:
				maintenance_db = pg_strdup(optarg);
				break;
			default:
				fprintf(stderr, _("Try \"%s --help\" for more information.\n"), progname);
				exit(1);
		}
	}

	/*
	 * Non-option argument specifies database name as long as it wasn't
	 * already specified with -d / --dbname
	 */
	if (optind < argc && dbname == NULL)
	{
		dbname = argv[optind];
		optind++;
	}

	if (optind < argc)
	{
		fprintf(stderr, _("%s: too many command-line arguments (first is \"%s\")\n"),
				progname, argv[optind]);
		fprintf(stderr, _("Try \"%s --help\" for more information.\n"), progname);
		exit(1);
	}

	/* fill cparams except for dbname, which is set below */
	cparams.pghost = host;
	cparams.pgport = port;
	cparams.pguser = username;
	cparams.prompt_password = prompt_password;
	cparams.override_dbname = NULL;

	setup_cancel_handler();

	if (alldb)
	{
		if (dbname)
		{
			fprintf(stderr, _("%s: cannot reindex all databases and a specific one at the same time\n"), progname);
			exit(1);
		}
		if (syscatalog)
		{
			fprintf(stderr, _("%s: cannot reindex all databases and system catalogs at the same time\n"), progname);
			exit(1);
		}
		if (schemas.head != NULL)
		{
			fprintf(stderr, _("%s: cannot reindex specific schema(s) in all databases\n"), progname);
			exit(1);
		}
		if (tables.head != NULL)
		{
			fprintf(stderr, _("%s: cannot reindex specific table(s) in all databases\n"), progname);
			exit(1);
		}
		if (indexes.head != NULL)
		{
			fprintf(stderr, _("%s: cannot reindex specific index(es) in all databases\n"), progname);
			exit(1);
		}

		cparams.dbname = maintenance_db;

		reindex_all_databases(&cparams,
							  progname, echo, quiet, verbose);
	}
	else if (syscatalog)
	{
		if (schemas.head != NULL)
		{
			fprintf(stderr, _("%s: cannot reindex specific schema(s) and system catalogs at the same time\n"), progname);
			exit(1);
		}
		if (tables.head != NULL)
		{
			fprintf(stderr, _("%s: cannot reindex specific table(s) and system catalogs at the same time\n"), progname);
			exit(1);
		}
		if (indexes.head != NULL)
		{
			fprintf(stderr, _("%s: cannot reindex specific index(es) and system catalogs at the same time\n"), progname);
			exit(1);
		}

		if (dbname == NULL)
		{
			if (getenv("PGDATABASE"))
				dbname = getenv("PGDATABASE");
			else if (getenv("PGUSER"))
				dbname = getenv("PGUSER");
			else
				dbname = get_user_name_or_exit(progname);
		}

		cparams.dbname = dbname;

		reindex_system_catalogs(&cparams,
								progname, echo, verbose);
	}
	else
	{
		if (dbname == NULL)
		{
			if (getenv("PGDATABASE"))
				dbname = getenv("PGDATABASE");
			else if (getenv("PGUSER"))
				dbname = getenv("PGUSER");
			else
				dbname = get_user_name_or_exit(progname);
		}

		cparams.dbname = dbname;

		if (schemas.head != NULL)
		{
			SimpleStringListCell *cell;

			for (cell = schemas.head; cell; cell = cell->next)
			{
				reindex_one_database(&cparams, "SCHEMA", cell->val,
									 progname, echo, verbose);
			}
		}

		if (indexes.head != NULL)
		{
			SimpleStringListCell *cell;

			for (cell = indexes.head; cell; cell = cell->next)
			{
				reindex_one_database(&cparams, "INDEX", cell->val,
									 progname, echo, verbose);
			}
		}
		if (tables.head != NULL)
		{
			SimpleStringListCell *cell;

			for (cell = tables.head; cell; cell = cell->next)
			{
				reindex_one_database(&cparams, "TABLE", cell->val,
									 progname, echo, verbose);
			}
		}

		/*
		 * reindex database only if neither index nor table nor schema is
		 * specified
		 */
		if (indexes.head == NULL && tables.head == NULL && schemas.head == NULL)
			reindex_one_database(&cparams, "DATABASE", NULL,
								 progname, echo, verbose);
	}

	exit(0);
}

static void
reindex_one_database(const ConnParams *cparams,
					 const char *type, const char *name,
					 const char *progname,
					 bool echo, bool verbose)
{
	PQExpBufferData sql;

	PGconn	   *conn;

	conn = connectDatabase(cparams, progname, echo, false, false);

	initPQExpBuffer(&sql);

	appendPQExpBufferStr(&sql, "REINDEX ");

	if (verbose)
		appendPQExpBufferStr(&sql, "(VERBOSE) ");

	appendPQExpBufferStr(&sql, type);
	appendPQExpBufferChar(&sql, ' ');
	if (strcmp(type, "TABLE") == 0 ||
		strcmp(type, "INDEX") == 0)
		appendQualifiedRelation(&sql, name, conn, progname, echo);
	else if (strcmp(type, "SCHEMA") == 0)
		appendPQExpBufferStr(&sql, name);
	else if (strcmp(type, "DATABASE") == 0)
		appendPQExpBufferStr(&sql, fmtIdEnc(PQdb(conn), PQclientEncoding(conn)));
	appendPQExpBufferChar(&sql, ';');

	if (!executeMaintenanceCommand(conn, sql.data, echo))
	{
		if (strcmp(type, "TABLE") == 0)
			fprintf(stderr, _("%s: reindexing of table \"%s\" in database \"%s\" failed: %s"),
					progname, name, PQdb(conn), PQerrorMessage(conn));
		if (strcmp(type, "INDEX") == 0)
			fprintf(stderr, _("%s: reindexing of index \"%s\" in database \"%s\" failed: %s"),
					progname, name, PQdb(conn), PQerrorMessage(conn));
		if (strcmp(type, "SCHEMA") == 0)
			fprintf(stderr, _("%s: reindexing of schema \"%s\" in database \"%s\" failed: %s"),
					progname, name, PQdb(conn), PQerrorMessage(conn));
		else
			fprintf(stderr, _("%s: reindexing of database \"%s\" failed: %s"),
					progname, PQdb(conn), PQerrorMessage(conn));
		PQfinish(conn);
		exit(1);
	}

	PQfinish(conn);
	termPQExpBuffer(&sql);
}

static void
reindex_all_databases(ConnParams *cparams,
					  const char *progname, bool echo, bool quiet, bool verbose)
{
	PGconn	   *conn;
	PGresult   *result;
	int			i;

	conn = connectMaintenanceDatabase(cparams, progname, echo);
	result = executeQuery(conn, "SELECT datname FROM pg_database WHERE datallowconn ORDER BY 1;", progname, echo);
	PQfinish(conn);

	for (i = 0; i < PQntuples(result); i++)
	{
		char	   *dbname = PQgetvalue(result, i, 0);

		if (!quiet)
		{
			printf(_("%s: reindexing database \"%s\"\n"), progname, dbname);
			fflush(stdout);
		}

		cparams->override_dbname = dbname;

		reindex_one_database(cparams, "DATABASE", NULL,
							 progname, echo, verbose);
	}

	PQclear(result);
}

static void
reindex_system_catalogs(const ConnParams *cparams,
						const char *progname, bool echo, bool verbose)
{
	PGconn	   *conn;
	PQExpBufferData sql;

	conn = connectDatabase(cparams, progname, echo, false, false);

	initPQExpBuffer(&sql);

	appendPQExpBuffer(&sql, "REINDEX");

	if (verbose)
		appendPQExpBuffer(&sql, " (VERBOSE)");

	appendPQExpBuffer(&sql, " SYSTEM %s;", fmtIdEnc(PQdb(conn), PQclientEncoding(conn)));

	if (!executeMaintenanceCommand(conn, sql.data, echo))
	{
		fprintf(stderr, _("%s: reindexing of system catalogs failed: %s"),
				progname, PQerrorMessage(conn));
		PQfinish(conn);
		exit(1);
	}
	PQfinish(conn);
	termPQExpBuffer(&sql);
}

static void
help(const char *progname)
{
	printf(_("%s reindexes a PostgreSQL database.\n\n"), progname);
	printf(_("Usage:\n"));
	printf(_("  %s [OPTION]... [DBNAME]\n"), progname);
	printf(_("\nOptions:\n"));
	printf(_("  -a, --all                 reindex all databases\n"));
	printf(_("  -d, --dbname=DBNAME       database to reindex\n"));
	printf(_("  -e, --echo                show the commands being sent to the server\n"));
	printf(_("  -i, --index=INDEX         recreate specific index(es) only\n"));
	printf(_("  -q, --quiet               don't write any messages\n"));
	printf(_("  -s, --system              reindex system catalogs\n"));
	printf(_("  -S, --schema=SCHEMA       reindex specific schema(s) only\n"));
	printf(_("  -t, --table=TABLE         reindex specific table(s) only\n"));
	printf(_("  -v, --verbose             write a lot of output\n"));
	printf(_("  -V, --version             output version information, then exit\n"));
	printf(_("  -?, --help                show this help, then exit\n"));
	printf(_("\nConnection options:\n"));
	printf(_("  -h, --host=HOSTNAME       database server host or socket directory\n"));
	printf(_("  -p, --port=PORT           database server port\n"));
	printf(_("  -U, --username=USERNAME   user name to connect as\n"));
	printf(_("  -w, --no-password         never prompt for password\n"));
	printf(_("  -W, --password            force password prompt\n"));
	printf(_("  --maintenance-db=DBNAME   alternate maintenance database\n"));
	printf(_("\nRead the description of the SQL command REINDEX for details.\n"));
	printf(_("\nReport bugs to <pgsql-bugs@postgresql.org>.\n"));
}
