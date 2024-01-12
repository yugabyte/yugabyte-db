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

static void * pg_malloc(size_t num_bytes);
static void * pg_calloc(size_t n_members, size_t num_bytes);
static void * pg_realloc(void *mem, size_t num_bytes);
static void pg_free(void *mem);

static bool gHasSetVTable = false;
static bson_mem_vtable_t gMemVtable = {
	pg_malloc,
	pg_calloc,
	pg_realloc,
	pg_free,
	{ 0 }
};


/* --------------------------------------------------------- */
/* GUCs and default values */
/* --------------------------------------------------------- */

/* GUC controlling whether or not we use the pretty printed version json representation for bson */
#define DEFAULT_BSON_TEXT_USE_JSON_REPRESENTATION false
bool BsonTextUseJsonRepresentation = DEFAULT_BSON_TEXT_USE_JSON_REPRESENTATION;


/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */

/*
 * Registers callbacks for Bson to use postgres allocators.
 */
void
InstallBsonMemVTables(void)
{
	if (!gHasSetVTable)
	{
		bson_mem_set_vtable(&gMemVtable);
		gHasSetVTable = true;
	}
}


/*
 * Initializes core configurations pertaining to the bson type management.
 */
void
InitBsonConfigurations(void)
{
	DefineCustomBoolVariable(
		"helio_core.bsonUseEJson",
		gettext_noop(
			"Determines whether the bson text is printed as extended Json. Used mainly for test."),
		NULL, &BsonTextUseJsonRepresentation, DEFAULT_BSON_TEXT_USE_JSON_REPRESENTATION,
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


static void
pg_free(void *mem)
{
	if (mem != NULL)
	{
		pfree(mem);
	}
}
