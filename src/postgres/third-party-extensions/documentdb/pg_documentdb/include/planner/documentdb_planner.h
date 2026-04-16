/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/planner/documentdb_planner.h
 *
 * The pg_documentdb planner hook function.
 *
 *-------------------------------------------------------------------------
 */

#ifndef DOCUMENTDB_PLANNER_H
#define DOCUMENTDB_PLANNER_H


#include "postgres.h"
#include <access/xlog.h>

#include <optimizer/planner.h>
#include <optimizer/paths.h>
#include <commands/explain.h>
#include <optimizer/plancat.h>


extern planner_hook_type ExtensionPreviousPlannerHook;
extern set_rel_pathlist_hook_type ExtensionPreviousSetRelPathlistHook;
extern explain_get_index_name_hook_type ExtensionPreviousIndexNameHook;
extern get_relation_info_hook_type ExtensionPreviousGetRelationInfoHook;
extern bool SimulateRecoveryState;
extern bool DocumentDBPGReadOnlyForDiskFull;


PlannedStmt * DocumentDBApiPlanner(Query *parse, const char *queryString, int
								   cursorOptions,
								   ParamListInfo boundParams);
void ExtensionRelPathlistHook(PlannerInfo *root, RelOptInfo *rel, Index rti,
							  RangeTblEntry *rte);
void ExtensionGetRelationInfoHook(PlannerInfo *root, Oid relationObjectId,
								  bool inhparent, RelOptInfo *rel);
bool IsDocumentDbCollectionBasedRTE(RangeTblEntry *rte);
bool IsResolvableDocumentDbCollectionBasedRTE(RangeTblEntry *rte,
											  ParamListInfo boundParams);
const char * ExtensionExplainGetIndexName(Oid indexId);
Const * GetConstParamValue(Node *param, ParamListInfo boundParams);

const char * ExtensionIndexOidGetIndexName(Oid indexId, bool useLibPq);
const char * GetDocumentDBIndexNameFromPostgresIndex(const char *pgIndexName, bool
													 useLibPq);

/* Method that throws an error if we're trying to execute a write command and the
 * current database is in recovery mode (read-only mode). */
static inline void
ThrowIfWriteCommandNotAllowed(void)
{
	if (RecoveryInProgress() || SimulateRecoveryState)
	{
		ereport(ERROR, (errcode(ERRCODE_READ_ONLY_SQL_TRANSACTION), errmsg(
							"Can't execute write operation, the database is in recovery and waiting for the standby node to be promoted.")));
	}

	if (DocumentDBPGReadOnlyForDiskFull)
	{
		/*
		 *  We want to throw `ERRCODE_DISK_FULL` from backend when the disk is say `90% full` as opposed to waiting
		 *  for the disk to be `100% full`. Marlin runs a background task that monitors the disk and
		 *  sets a config `ApiGucPrefix.IsPgReadOnlyForDiskFull = true`, the postgres process then reads the config
		 *  and stores it in the `DocumentDBPGReadOnlyForDiskFull` variable. Marlin also set the postgres config
		 *  `default_transaction_read_only = on` which makes postgres throw `ERRCODE_READ_ONLY_SQL_TRANSACTION`
		 *  for any operation that can update data.
		 *
		 *  ToMongoError() utility in PostgresMongoResultExtensions.cs (aka gateway) then converts the Postgres
		 *  error to appropriate Mongo Client error code and error message.
		 */
		ereport(ERROR, (errcode(ERRCODE_DISK_FULL), errmsg(
							"Can't execute write operation, The database disk is full")));
	}
}


#endif
