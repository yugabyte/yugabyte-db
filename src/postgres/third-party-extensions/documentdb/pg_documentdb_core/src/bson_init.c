/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/bson_init.c
 *
 * Initialization of the shared library initialization for bson.
 *-------------------------------------------------------------------------
 */
#include <postgres.h>
#include <miscadmin.h>
#include <utils/guc.h>
#include <bson.h>

#include "bson_init.h"

/* YB includes */
#include "pg_yb_utils.h"

static void * pg_malloc(size_t num_bytes);
static void * pg_calloc(size_t n_members, size_t num_bytes);
static void * pg_realloc(void *mem, size_t num_bytes);
static void * pg_aligned_alloc(size_t alignment, size_t num_bytes);
static void pg_free(void *mem);

static bool gHasSetVTable = false;
static bson_mem_vtable_t gMemVtable = {
	pg_malloc,
	pg_calloc,
	pg_realloc,
	pg_free,
	pg_aligned_alloc,
	{ 0 }
};


/* --------------------------------------------------------- */
/* GUCs and default values */
/* --------------------------------------------------------- */

/* GUC controlling whether or not we use the pretty printed version json representation for bson */
#define DEFAULT_BSON_TEXT_USE_JSON_REPRESENTATION false
bool BsonTextUseJsonRepresentation = DEFAULT_BSON_TEXT_USE_JSON_REPRESENTATION;

/* GUC deciding whether collation is support */
#define DEFAULT_ENABLE_COLLATION false
bool EnableCollation = DEFAULT_ENABLE_COLLATION;

/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */

/*
 * Registers callbacks for Bson to use postgres allocators.
 */
void
InstallBsonMemVTables(void)
{
	/*
	 * YB: Upstream bug. pg_aligned_alloc does not work on Postgres versions
	 * less than 16. Temporarily fixing this by defaulting to default bson
	 * non-pg mem allocators. This is called from _PG_init, so check the env var
	 * instead of IsYugaByteEnabled.
	 */
	if (!YBIsEnabledInPostgresEnvVar() && !gHasSetVTable)
	{
		bson_mem_set_vtable(&gMemVtable);
		gHasSetVTable = true;
	}
}


/*
 * Initializes core configurations pertaining to documentdb core.
 */
void
InitDocumentDBCoreConfigurations(const char *prefix)
{
	DefineCustomBoolVariable(
		psprintf("%s.bsonUseEJson", prefix),
		gettext_noop(
			"Determines whether the bson text is printed as extended Json. Used mainly for test."),
		NULL, &BsonTextUseJsonRepresentation, DEFAULT_BSON_TEXT_USE_JSON_REPRESENTATION,
		PGC_USERSET, 0, NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.enableCollation", prefix),
		gettext_noop(
			"Determines whether collation is supported."),
		NULL, &EnableCollation,
		DEFAULT_ENABLE_COLLATION,
		PGC_USERSET, 0, NULL, NULL, NULL);
}


/* --------------------------------------------------------- */
/* Private methods */
/* --------------------------------------------------------- */
static void *
pg_malloc(size_t num_bytes)
{
	return palloc(num_bytes);
}


static void *
pg_calloc(size_t n_members, size_t num_bytes)
{
	/* TODO: Is this the best way to handle this? */
	return palloc0(n_members * num_bytes);
}


static void *
pg_realloc(void *mem, size_t num_bytes)
{
	if (mem == NULL)
	{
		return pg_malloc(num_bytes);
	}

	return repalloc(mem, num_bytes);
}


static void *
pg_aligned_alloc(size_t alignment, size_t num_bytes)
{
#if PG_VERSION_NUM >= 160000
	return palloc_aligned(num_bytes, alignment, 0);
#else
	return pg_malloc(num_bytes);
#endif
}


static void
pg_free(void *mem)
{
	if (mem != NULL)
	{
		pfree(mem);
	}
}
