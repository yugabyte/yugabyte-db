/*
 *	tablespace.c
 *
 *	tablespace functions
 *
 *	Copyright (c) 2010-2022, PostgreSQL Global Development Group
 *	src/bin/pg_upgrade/tablespace.c
 */

#include "postgres_fe.h"

#include "pg_upgrade.h"

static void get_tablespace_paths(void);
static void set_tablespace_directory_suffix(ClusterInfo *cluster);


void
init_tablespaces(void)
{
	get_tablespace_paths();

	set_tablespace_directory_suffix(&old_cluster);
	set_tablespace_directory_suffix(&new_cluster);

	if (os_info.num_old_tablespaces > 0 &&
		strcmp(old_cluster.tablespace_suffix, new_cluster.tablespace_suffix) == 0)
		pg_fatal("Cannot upgrade to/from the same system catalog version when\n"
				 "using tablespaces.\n");
}


/*
 * get_tablespace_paths()
 *
 * Scans pg_tablespace and returns a malloc'ed array of all tablespace
 * paths. It's the caller's responsibility to free the array.
 */
static void
get_tablespace_paths(void)
{
	PGconn	   *conn = connectToServer(&old_cluster, "template1");
	PGresult   *res;
	int			tblnum;
	int			i_spclocation;
	char		query[QUERY_ALLOC];

	snprintf(query, sizeof(query),
			 "SELECT pg_catalog.pg_tablespace_location(oid) AS spclocation "
			 "FROM	pg_catalog.pg_tablespace "
			 "WHERE	spcname != 'pg_default' AND "
			 "		spcname != 'pg_global'");

	res = executeQueryOrDie(conn, "%s", query);

	if ((os_info.num_old_tablespaces = PQntuples(res)) != 0)
		os_info.old_tablespaces =
			(char **) pg_malloc(os_info.num_old_tablespaces * sizeof(char *));
	else
		os_info.old_tablespaces = NULL;

	i_spclocation = PQfnumber(res, "spclocation");

	for (tblnum = 0; tblnum < os_info.num_old_tablespaces; tblnum++)
	{
		struct stat statBuf;

		os_info.old_tablespaces[tblnum] = pg_strdup(PQgetvalue(res, tblnum, i_spclocation));

		/*
		 * Check that the tablespace path exists and is a directory.
		 * Effectively, this is checking only for tables/indexes in
		 * non-existent tablespace directories.  Databases located in
		 * non-existent tablespaces already throw a backend error.
		 * Non-existent tablespace directories can occur when a data directory
		 * that contains user tablespaces is moved as part of pg_upgrade
		 * preparation and the symbolic links are not updated.
		 */
		if (stat(os_info.old_tablespaces[tblnum], &statBuf) != 0)
		{
			if (errno == ENOENT)
				report_status(PG_FATAL,
							  "tablespace directory \"%s\" does not exist\n",
							  os_info.old_tablespaces[tblnum]);
			else
				report_status(PG_FATAL,
							  "could not stat tablespace directory \"%s\": %s\n",
							  os_info.old_tablespaces[tblnum], strerror(errno));
		}
		if (!S_ISDIR(statBuf.st_mode))
			report_status(PG_FATAL,
						  "tablespace path \"%s\" is not a directory\n",
						  os_info.old_tablespaces[tblnum]);
	}

	PQclear(res);

	PQfinish(conn);
}


static void
set_tablespace_directory_suffix(ClusterInfo *cluster)
{
	/* This cluster has a version-specific subdirectory */

	/* The leading slash is needed to start a new directory. */
	cluster->tablespace_suffix = psprintf("/PG_%s_%d",
										  cluster->major_version_str,
										  cluster->controldata.cat_ver);
}
