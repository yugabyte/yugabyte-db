/*
 * compat.h
 *		Definitions for function wrappers compatible between PG versions.
 *
 * Copyright (c) 2015-2022, Postgres Professional
 *
 * IDENTIFICATION
 *	  contrib/pg_wait_sampling/compat.h
 */
#ifndef __COMPAT_H__
#define __COMPAT_H__

#include "postgres.h"

#include "access/tupdesc.h"
#include "miscadmin.h"
#include "storage/shm_mq.h"
#include "utils/guc_tables.h"

static inline TupleDesc
CreateTemplateTupleDescCompat(int nattrs, bool hasoid)
{
#if PG_VERSION_NUM >= 120000
	return CreateTemplateTupleDesc(nattrs);
#else
	return CreateTemplateTupleDesc(nattrs, hasoid);
#endif
}

static inline void
shm_mq_detach_compat(shm_mq_handle *mqh, shm_mq *mq)
{
#if PG_VERSION_NUM >= 100000
	shm_mq_detach(mqh);
#else
	shm_mq_detach(mq);
#endif
}

static inline shm_mq_result
shm_mq_send_compat(shm_mq_handle *mqh, Size nbytes, const void *data,
				   bool nowait, bool force_flush)
{
#if PG_VERSION_NUM >= 150000
	return shm_mq_send(mqh, nbytes, data, nowait, force_flush);
#else
	return shm_mq_send(mqh, nbytes, data, nowait);
#endif
}

static inline void
InitPostgresCompat(const char *in_dbname, Oid dboid,
				   const char *username, Oid useroid,
				   bool load_session_libraries,
				   bool override_allow_connections,
				   char *out_dbname)
{
#if PG_VERSION_NUM >= 150000
	InitPostgres(in_dbname, dboid, username, useroid, load_session_libraries,
				 override_allow_connections, out_dbname);
#elif PG_VERSION_NUM >= 110000
	InitPostgres(in_dbname, dboid, username, useroid, out_dbname,
				 override_allow_connections);
#else
	InitPostgres(in_dbname, dboid, username, useroid, out_dbname);
#endif
}

static inline void
get_guc_variables_compat(struct config_generic ***vars, int *num_vars)
{
	Assert(vars != NULL);
	Assert(num_vars != NULL);

#if PG_VERSION_NUM >= 160000
	*vars = get_guc_variables(num_vars);
#else
	*vars = get_guc_variables();
	*num_vars = GetNumConfigOptions();
#endif
}

#endif
