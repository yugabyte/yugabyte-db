/*
 *	dump.c
 *
 *	dump functions
 *
 *	Copyright (c) 2010-2022, PostgreSQL Global Development Group
 *	src/bin/pg_upgrade/dump.c
 */

#include "postgres_fe.h"

#include "fe_utils/string_utils.h"
#include "pg_upgrade.h"

void
generate_old_dump(void)
{
	int			dbnum;

	prep_status("Creating dump of global objects");

	/* run new pg_dumpall binary for globals */
	exec_prog(UTILITY_LOG_FILE, NULL, true, true,
			  "\"%s/ysql_dumpall\" %s --globals-only --quote-all-identifiers "
			  "--binary-upgrade %s -f \"%s/%s\"",
			  new_cluster.bindir, cluster_conn_opts(&old_cluster),
			  log_opts.verbose ? "--verbose" : "",
			  log_opts.dumpdir,
			  GLOBALS_DUMP_FILE);
	check_ok();

	prep_status_progress("Creating dump of database schemas");

	/* create per-db dump files */
	for (dbnum = 0; dbnum < old_cluster.dbarr.ndbs; dbnum++)
	{
		char		sql_file_name[MAXPGPATH],
					log_file_name[MAXPGPATH];
		DbInfo	   *old_db = &old_cluster.dbarr.dbs[dbnum];
		PQExpBufferData connstr,
					escaped_connstr;

		initPQExpBuffer(&connstr);
		appendPQExpBufferStr(&connstr, "dbname=");
		appendConnStrVal(&connstr, old_db->db_name);
		initPQExpBuffer(&escaped_connstr);
		appendShellString(&escaped_connstr, connstr.data);
		termPQExpBuffer(&connstr);

		pg_log(PG_STATUS, "%s", old_db->db_name);
		snprintf(sql_file_name, sizeof(sql_file_name), DB_DUMP_FILE_MASK, old_db->db_oid);
		snprintf(log_file_name, sizeof(log_file_name), DB_DUMP_LOG_FILE_MASK, old_db->db_oid);

		parallel_exec_prog(log_file_name, NULL,
						   "\"%s/ysql_dump\" %s --no-data %s --quote-all-identifiers "
						   "--binary-upgrade --format=custom %s --file=\"%s/%s\" %s",
						   new_cluster.bindir, cluster_conn_opts(&old_cluster),
						   log_opts.verbose ? "--verbose" : "",
						   user_opts.do_statistics ? "--with-statistics" : "--no-statistics",
						   log_opts.dumpdir,
						   sql_file_name, escaped_connstr.data);

		termPQExpBuffer(&escaped_connstr);
	}

	/* reap all children */
	while (reap_child(true) == true)
		;

	end_progress_output();
	check_ok();
}

/* YB: the dump carries role password verifiers, keep it readable only by the owner */
static void
yb_restrict_schema_dump_file(const char *file_name)
{
	if (chmod(file_name, S_IRUSR | S_IWUSR) != 0)
		pg_fatal("could not set permissions on file \"%s\": %m\n", file_name);
}

/* YB: collect a plain-text schema dump of the old cluster */
void
yb_generate_schema_dump(void)
{
	char		dump_dir[MAXPGPATH];
	char		staging_dir[MAXPGPATH];
	char		file_name[MAXPGPATH];
	int			len;

	if (!user_opts.yb_collect_schema_dump)
		return;

	len = snprintf(dump_dir, sizeof(dump_dir), "%s/%s", log_opts.rootdir,
				   YB_SCHEMA_DUMP_OUTPUTDIR);
	if (len >= MAXPGPATH)
		pg_fatal("directory path for the schema dump is too long\n");

	len = snprintf(staging_dir, sizeof(staging_dir), "%s%s", dump_dir,
				   YB_SCHEMA_DUMP_STAGING_SUFFIX);
	if (len >= MAXPGPATH)
		pg_fatal("directory path for the schema dump is too long\n");

	/* Both outlive the run, so clear them before collecting a new set. */
	(void) rmtree(dump_dir, true);
	(void) rmtree(staging_dir, true);

	if (mkdir(staging_dir, S_IRWXU) < 0)
		pg_fatal("could not create directory \"%s\": %m\n", staging_dir);

	/* Flags must match generate_old_dump(), or the dump stops being representative. */
	prep_status("Collecting schema dump of global objects");

	snprintf(file_name, sizeof(file_name), "%s/%s", staging_dir,
			 YB_SCHEMA_DUMP_GLOBALS_FILE);
	exec_prog(UTILITY_LOG_FILE, NULL, true, true,
			  "\"%s/ysql_dumpall\" %s --globals-only --quote-all-identifiers "
			  "--binary-upgrade %s -f \"%s\"",
			  new_cluster.bindir, cluster_conn_opts(&old_cluster),
			  log_opts.verbose ? "--verbose" : "",
			  file_name);
	yb_restrict_schema_dump_file(file_name);
	check_ok();

	prep_status("Collecting schema dump of all databases");

	snprintf(file_name, sizeof(file_name), "%s/%s", staging_dir,
			 YB_SCHEMA_DUMP_DATABASES_FILE);
	exec_prog(UTILITY_LOG_FILE, NULL, true, true,
			  "\"%s/ysql_dumpall\" %s --no-data --quote-all-identifiers "
			  "--binary-upgrade %s %s -f \"%s\"",
			  new_cluster.bindir, cluster_conn_opts(&old_cluster),
			  user_opts.do_statistics ? "--with-statistics" : "--no-statistics",
			  log_opts.verbose ? "--verbose" : "",
			  file_name);
	yb_restrict_schema_dump_file(file_name);
	check_ok();

	prep_status("Recording the old cluster version");

	snprintf(file_name, sizeof(file_name), "%s/%s", staging_dir,
			 YB_SCHEMA_DUMP_VERSION_FILE);
	exec_prog(UTILITY_LOG_FILE, NULL, true, true,
			  "\"%s/ysqlsh\" %s --dbname=template1 --no-psqlrc --tuples-only --no-align "
			  "--command \"SELECT version();\" --output \"%s\"",
			  new_cluster.bindir, cluster_conn_opts(&old_cluster),
			  file_name);
	yb_restrict_schema_dump_file(file_name);
	check_ok();

	/* One rename, so a collector sees the whole set or none of it. */
	if (rename(staging_dir, dump_dir) != 0)
		pg_fatal("could not rename directory \"%s\" to \"%s\": %m\n",
				 staging_dir, dump_dir);

	pg_log(PG_REPORT, "\nSchema dump collected in %s\n", dump_dir);
}
