/*------------------------------------------------------------------------------
 *
 * yb_alter_concurrently.c
 *	  ALTER TABLE ... CONCURRENTLY: online table rewrites driven through the
 *	  online schema change framework (see utils/yb_online_schema_change.h).
 *
 * A rewriting ALTER TABLE normally copies the table row by row under
 * AccessExclusiveLock.  With CONCURRENTLY the same statement is run as an
 * online schema change job.  The statement is run first on an empty shadow
 * table, a physical clone of the source, so that PostgreSQL itself works out
 * the new schema and how each column is recomputed; that per-row work becomes
 * the job's transform, and the framework copies the table while DML goes on.
 * The cutover then runs the statement on the source, as an ordinary rewrite
 * whose phase 3 adopts the shadow's storage (YbOscSwapWithShadow) instead of
 * copying rows.  The source keeps its OID, so every OID-keyed object survives,
 * as after an ordinary rewrite.
 *
 * Like CREATE INDEX CONCURRENTLY, the statement holds a session-level
 * ShareUpdateExclusiveLock on the source throughout and spans several
 * transactions:
 *
 *   1. Create the shadow <schema>.yb_osc_shadow_<oid>, run the statement on
 *      it, create the transform <schema>.yb_osc_xf_<oid> and check the job
 *      as begin would.  A statement that does not rewrite runs as an
 *      ordinary ALTER TABLE instead.  The job's copy runs in other backends,
 *      which must see the shadow and the transform, so this commits first.
 *   2. Begin the job (YbOscBegin), retrying transient lock conflicts.
 *   3. Wait for the copy in a plain transaction.
 *   4. Cut over (YbOscFinish), in the transaction the utility layer commits.
 *
 * Nothing before step 4 changes the source, and step 4 is one transaction.
 * An error in step 1 rolls back with its transaction.  An error in step 2 or
 * 3, a cancel or a failed copy included, goes through RollBackAndRethrow,
 * which drops what the statement created.  An error in step 4 leaves the job
 * active, with its copy complete: re-issuing the same statement retries the
 * cutover.  So does re-issuing a statement whose backend died; a shadow left
 * without a job is dropped by the next statement on the table.
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License.  You may obtain a copy
 * of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 * IDENTIFICATION
 *        src/backend/commands/yb_alter_concurrently.c
 *
 *------------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/attmap.h"
#include "access/genam.h"
#include "access/htup_details.h"
#include "access/multixact.h"
#include "access/reloptions.h"
#include "access/sysattr.h"
#include "access/table.h"
#include "access/xact.h"
#include "catalog/catalog.h"
#include "catalog/dependency.h"
#include "catalog/indexing.h"
#include "catalog/namespace.h"
#include "catalog/objectaddress.h"
#include "catalog/pg_attribute.h"
#include "catalog/pg_class.h"
#include "catalog/pg_constraint.h"
#include "catalog/pg_index.h"
#include "catalog/pg_inherits.h"
#include "catalog/pg_proc.h"
#include "catalog/pg_type.h"
#include "commands/cluster.h"
#include "commands/defrem.h"
#include "commands/event_trigger.h"
#include "commands/tablecmds.h"
#include "commands/tablespace.h"
#include "commands/yb_alter_concurrently.h"
#include "commands/yb_cmds.h"
#include "executor/spi.h"
#include "miscadmin.h"
#include "nodes/makefuncs.h"
#include "nodes/nodeFuncs.h"
#include "nodes/plannodes.h"
#include "nodes/value.h"
#include "optimizer/optimizer.h"
#include "parser/parse_coerce.h"
#include "parser/parse_func.h"
#include "parser/parse_utilcmd.h"
#include "parser/parser.h"
#include "pg_yb_utils.h"
#include "rewrite/rewriteHandler.h"
#include "storage/lmgr.h"
#include "utils/acl.h"
#include "utils/builtins.h"
#include "utils/fmgroids.h"
#include "utils/guc.h"
#include "utils/inval.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"
#include "utils/queryjumble.h"
#include "utils/rel.h"
#include "utils/relcache.h"
#include "utils/ruleutils.h"
#include "utils/snapmgr.h"
#include "utils/syscache.h"
#include "utils/yb_online_schema_change.h"
#include "yb/yql/pggate/ybc_pggate.h"


bool		yb_enable_alter_table_concurrently = true;
int			yb_alter_table_concurrently_poll_interval_ms = 100;
bool		yb_osc_keep_old_table = false;
bool		yb_test_fail_osc_cutover = false;

#define YB_OSC_SHADOW_NAME_PREFIX		"yb_osc_shadow_"
#define YB_OSC_TRANSFORM_NAME_PREFIX	"yb_osc_xf_"
#define YB_OSC_DROPPED_COLUMN_PREFIX	"yb_osc_dropped_"

/*
 * What ResumeDdlTransaction needs to continue the statement's DDL processing
 * in a later transaction.
 */
typedef struct YbOscTxnState
{
	YbDdlMode	ddl_mode;
	YbDdlOriginalStmtState stmt_state;
} YbOscTxnState;

typedef struct YbOscVarRemapContext
{
	Relation	src_rel;
	Relation	shadow_rel;
} YbOscVarRemapContext;

/*
 * Commit the current transaction, DDL work included, and start a plain one.
 *
 * Unlike YbCommitTransactionCommandIntermediate, this does not re-arm the DDL
 * state in the new transaction, because the wait must run without it; the
 * caller re-arms it with ResumeDdlTransaction when needed.
 */
static void
CommitAndStartPlainTransaction(YbOscTxnState *state)
{
	Assert(YBGetDdlUseRegularTransactionBlock());
	state->ddl_mode = YBGetCurrentDdlMode();
	YBGetDdlOriginalStmtState(&state->stmt_state);

	if (ActiveSnapshotSet())
		PopActiveSnapshot();
	CommitTransactionCommand();
	StartTransactionCommand();
}

static void
ResumeDdlTransaction(const YbOscTxnState *state)
{
	YbOscCheckTransactionalDdl();
	YBAddDdlTxnState(state->ddl_mode);
	YBSetDdlOriginalStmtState(&state->stmt_state);
}

static char *
ShadowName(Oid srcOid)
{
	return psprintf("%s%u", YB_OSC_SHADOW_NAME_PREFIX, srcOid);
}

static char *
TransformName(Oid srcOid)
{
	return psprintf("%s%u", YB_OSC_TRANSFORM_NAME_PREFIX, srcOid);
}

static void
ExecUtilitySql(const char *sql)
{
	if (SPI_connect() != SPI_OK_CONNECT)
		ereport(ERROR, (errmsg("SPI_connect failed")));

	if (SPI_execute(sql, false, 0) != SPI_OK_UTILITY)
		ereport(ERROR,
				(errmsg("ALTER TABLE ... CONCURRENTLY internal DDL failed: %s", sql)));

	SPI_finish();
}

static List *
RelationReloptions(Oid relid)
{
	HeapTuple	tuple = SearchSysCache1(RELOID, ObjectIdGetDatum(relid));
	Datum		datum;
	bool		isnull;
	List	   *options = NIL;

	if (!HeapTupleIsValid(tuple))
		elog(ERROR, "cache lookup failed for relation %u", relid);
	datum = SysCacheGetAttr(RELOID, tuple, Anum_pg_class_reloptions, &isnull);
	if (!isnull)
		options = untransformRelOptions(datum);
	ReleaseSysCache(tuple);
	return options;
}

/*
 * The shadow and its indexes are created with the source's reloptions plus a
 * SPLIT clause for the current tablet layout.  Without a yb_presplit entry the
 * SPLIT clause would be persisted as one, and the reloptions would no longer
 * match the source's when the cutover pairs indexes by definition.  The
 * empty-string sentinel tells YbSyncSplitOptionsAndPresplit to use the SPLIT
 * clause without recording it.
 */
static List *
WithPresplitSentinel(List *options)
{
	ListCell   *lc;

	foreach(lc, options)
	{
		if (strcmp(((DefElem *) lfirst(lc))->defname, "yb_presplit") == 0)
			return options;
	}
	return lappend(options, makeDefElem("yb_presplit", (Node *) makeString(""), -1));
}

/*
 * Run cmds as an ALTER TABLE on relid the way ProcessUtilitySlow would: with
 * a utility context, which the parse transformation of subcommands such as
 * ADD COLUMN and ALTER COLUMN TYPE requires (AlterTableInternal has none), and
 * with its own event-trigger command entry.  queryString is the text the
 * subcommands' parse locations refer to.  capture and shadowOid go into the
 * context; see AlterTableUtilityContext.
 */
static void
RunAlterTable(Oid relid, List *cmds, bool recurse, const char *queryString,
			  const AlterTableUtilityContext *outer,
			  YbAlterTableCapture *capture, Oid shadowOid)
{
	AlterTableStmt *atstmt = makeNode(AlterTableStmt);
	PlannedStmt *pstmt = makeNode(PlannedStmt);
	AlterTableUtilityContext atcontext;

	atstmt->relation = makeRangeVar(get_namespace_name(get_rel_namespace(relid)),
									get_rel_name(relid), -1);
	atstmt->relation->inh = recurse;
	atstmt->cmds = cmds;
	atstmt->objtype = OBJECT_TABLE;
	atstmt->missing_ok = false;

	pstmt->commandType = CMD_UTILITY;
	pstmt->canSetTag = false;
	pstmt->utilityStmt = (Node *) atstmt;
	pstmt->stmt_location = outer ? outer->pstmt->stmt_location : 0;
	pstmt->stmt_len = outer ? outer->pstmt->stmt_len : 0;

	memset(&atcontext, 0, sizeof(atcontext));
	atcontext.pstmt = pstmt;
	atcontext.queryString = queryString;
	atcontext.relid = relid;
	atcontext.params = outer ? outer->params : NULL;
	atcontext.queryEnv = outer ? outer->queryEnv : NULL;
	atcontext.yb_osc_capture = capture;
	atcontext.yb_osc_shadow_oid = shadowOid;

	LockRelationOid(relid, AccessExclusiveLock);
	EventTriggerAlterTableStart((Node *) atstmt);
	EventTriggerAlterTableRelid(relid);
	AlterTable(atstmt, AccessExclusiveLock, &atcontext);
	EventTriggerAlterTableEnd();
}

/*
 * The text of this ALTER TABLE statement alone, cut out of a query string
 * that may hold several, for the job to record and its cutover to re-run.
 * Allocated in PortalContext, so that it survives the commits that follow.
 */
static char *
AlterStatementText(const AlterTableUtilityContext *context)
{
	int			location = context->pstmt ? context->pstmt->stmt_location : -1;
	int			len = context->pstmt ? context->pstmt->stmt_len : 0;
	const char *text = CleanQuerytext(context->queryString, &location, &len);
	MemoryContext oldcxt = MemoryContextSwitchTo(PortalContext);
	char	   *result = pnstrdup(text, len);

	MemoryContextSwitchTo(oldcxt);
	return result;
}

static void
CheckSourceEligible(Relation rel, AlterTableStmt *stmt)
{
	const char *relname = RelationGetRelationName(rel);
	YbcTableProperties props;
	bool		isXrepl = false;

	if (!yb_enable_alter_table_concurrently)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("ALTER TABLE ... CONCURRENTLY is disabled"),
				 errhint("Set yb_enable_alter_table_concurrently to true.")));

	if (stmt->objtype != OBJECT_TABLE)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("CONCURRENTLY is only supported for ALTER TABLE")));

	switch (rel->rd_rel->relkind)
	{
		case RELKIND_RELATION:
			break;
		case RELKIND_PARTITIONED_TABLE:
			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					 errmsg("ALTER TABLE ... CONCURRENTLY is not supported on partitioned tables")));
			break;
		default:
			ereport(ERROR,
					(errcode(ERRCODE_WRONG_OBJECT_TYPE),
					 errmsg("\"%s\" is not a table", relname)));
			break;
	}

	/* The check ALTER TABLE makes (ATSimplePermissions). */
	if (!pg_class_ownercheck(RelationGetRelid(rel), GetUserId()) &&
		!IsYbDbAdminUser(GetUserId()))
		aclcheck_error(ACLCHECK_NOT_OWNER, OBJECT_TABLE, relname);

	if (IsSystemRelation(rel) || IsCatalogRelation(rel))
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("permission denied: \"%s\" is a system catalog", relname)));

	if (rel->rd_rel->relpersistence == RELPERSISTENCE_TEMP)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("ALTER TABLE ... CONCURRENTLY is not supported on temporary tables")));

	if (!IsYBRelation(rel))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("ALTER TABLE ... CONCURRENTLY is only supported on YugabyteDB-backed tables")));

	if (rel->rd_rel->relispartition)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("ALTER TABLE ... CONCURRENTLY is not supported on a partition")));

	/*
	 * DML addressed to an inheritance parent fires no statement trigger on
	 * the child, so the job's capture triggers would miss it; children the
	 * ALTER would recurse into are not cloned at all.
	 */
	if (rel->rd_rel->relhassubclass || has_superclass(RelationGetRelid(rel)))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("ALTER TABLE ... CONCURRENTLY is not supported on a table with inheritance parents or children")));

	if (OidIsValid(rel->rd_rel->reloftype))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("ALTER TABLE ... CONCURRENTLY is not supported on typed tables")));

	props = YbGetTableProperties(rel);
	if (props->is_colocated || OidIsValid(props->tablegroup_oid))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("ALTER TABLE ... CONCURRENTLY is not supported on colocated tables")));

	/*
	 * An ordinary rewrite goes through the master's xCluster/CDC checks for
	 * the rewritten table; the shadow is a table of its own and would bypass
	 * them, so every replicated table is refused here.
	 */
	HandleYBStatus(YBCIsObjectPartOfXRepl(MyDatabaseId, YbGetRelfileNodeId(rel),
										  &isXrepl));
	if (isXrepl)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("ALTER TABLE ... CONCURRENTLY is not supported on a table that is part of CDC or xCluster replication")));
}

/*
 * The job's ALTER TABLE statement, parsed.  The cutover replays it, and a
 * re-issued statement resumes the job only if it is the same.
 */
static AlterTableStmt *
ParseJobStatement(int64 jobId, const char *query)
{
	List	   *raw = query ? raw_parser(query, RAW_PARSE_DEFAULT) : NIL;
	RawStmt    *rawStmt = (list_length(raw) == 1) ? linitial_node(RawStmt, raw) : NULL;

	if (rawStmt == NULL || !IsA(rawStmt->stmt, AlterTableStmt))
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("online schema change job %lld does not record a single ALTER TABLE statement",
						(long long) jobId),
				 errdetail("Recorded statement: %s", query ? query : "(none)")));
	return (AlterTableStmt *) rawStmt->stmt;
}

/*
 * The active job of this statement on rel, to resume, or 0 if there is none.
 * YB's DDL retry can re-run the statement after its job began, and an
 * operator can re-issue it to finish a job whose backend died; both resume.
 * Any other active job on rel is an error.  Subcommands are compared with
 * equal(), which ignores parse locations, so the text need not match.
 */
static int64
FindJobToResume(Relation rel, AlterTableStmt *stmt)
{
	int64		jobId = YbOscFindActiveJob(list_make1_oid(RelationGetRelid(rel)));
	YbOscJob	job;

	if (jobId == 0)
		return 0;
	if (!YbOscFetchJob(jobId, &job))
		elog(ERROR, "online schema change job %lld disappeared", (long long) jobId);

	if (strcmp(job.origin, YB_OSC_ORIGIN_ALTER_TABLE) != 0 ||
		job.source_table_oid != RelationGetRelid(rel) ||
		!equal(ParseJobStatement(jobId, job.query)->cmds, stmt->cmds))
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_IN_USE),
				 errmsg("another online schema change job is in progress on table \"%s\"",
						RelationGetRelationName(rel)),
				 job.query ? errdetail("Job %lld runs: %s", (long long) jobId, job.query) : 0,
				 errhint("Finish it with yb_finish_mirror_table(%lld) or roll it back with yb_abort_mirror_table(%lld).",
						 (long long) jobId, (long long) jobId)));
	return jobId;
}

static void
DropShadow(Oid shadowOid)
{
	ObjectAddress object;

	ObjectAddressSet(object, RelationRelationId, shadowOid);
	performDeletion(&object, DROP_CASCADE, PERFORM_DELETION_INTERNAL);
}

/*
 * With no active job on the source, a shadow can only be left over from a
 * statement whose begin failed.  Drop it, and with it its transform, which
 * depends on its row type.
 */
static void
DropOrphanShadow(Relation rel, const char *shadowName)
{
	Oid			shadowOid = get_relname_relid(shadowName, RelationGetNamespace(rel));

	if (!OidIsValid(shadowOid))
		return;
	ereport(NOTICE,
			(errmsg("dropping shadow table \"%s\" left behind by an earlier ALTER TABLE ... CONCURRENTLY",
					shadowName)));
	DropShadow(shadowOid);
	CommandCounterIncrement();
}

/*
 * Add the source's CHECK constraints to the shadow's CREATE, cooked: the
 * shadow has the source's attnum layout, so the stored expressions apply
 * unchanged.  NOT VALID ones stay NOT VALID, so the copy verifies exactly
 * what an ordinary rewrite would.
 */
static void
AppendCheckConstraints(Relation srcRel, Relation pgConstraint,
					   CreateStmt *createStmt)
{
	ScanKeyData key;
	SysScanDesc scan;
	HeapTuple	tuple;

	ScanKeyInit(&key, Anum_pg_constraint_conrelid, BTEqualStrategyNumber,
				F_OIDEQ, ObjectIdGetDatum(RelationGetRelid(srcRel)));
	scan = systable_beginscan(pgConstraint, ConstraintRelidTypidNameIndexId,
							  true /* indexOK */ , NULL /* snapshot */ ,
							  1 /* nkeys */ , &key);

	while (HeapTupleIsValid(tuple = systable_getnext(scan)))
	{
		Form_pg_constraint con = (Form_pg_constraint) GETSTRUCT(tuple);
		Datum		conbin;
		bool		isnull;
		Constraint *check;

		if (con->contype != CONSTRAINT_CHECK)
			continue;

		conbin = heap_getattr(tuple, Anum_pg_constraint_conbin,
							  RelationGetDescr(pgConstraint), &isnull);
		if (isnull)
			elog(ERROR, "null conbin for constraint %u", con->oid);

		check = makeNode(Constraint);
		check->contype = CONSTR_CHECK;
		check->conname = pstrdup(NameStr(con->conname));
		check->cooked_expr = TextDatumGetCString(conbin);
		check->initially_valid = con->convalidated;
		check->skip_validation = !con->convalidated;
		check->is_no_inherit = con->connoinherit;
		check->location = -1;
		createStmt->constraints = lappend(createStmt->constraints, check);
	}
	systable_endscan(scan);
}

/*
 * The CREATE TABLE for the shadow: every source attribute in attnum order --
 * dropped ones as int4 placeholders that CreateShadow drops again, so the
 * shadow's attnums, and with them its DocDB column ids, equal the source's --
 * with the same type, typmod, collation and NOT NULL, but no default,
 * identity or generation expression: the transform supplies every column.
 * Primary key (with its HASH/ASC/DESC options), current split boundaries,
 * tablespace, reloptions and CHECK constraints follow the source.
 */
static CreateStmt *
BuildShadowCreateStmt(Relation srcRel, const char *nspName,
					  const char *shadowName, List **placeholders)
{
	TupleDesc	desc = RelationGetDescr(srcRel);
	CreateStmt *stmt = makeNode(CreateStmt);
	Relation	pgConstraint;
	Oid			srcTablespace = srcRel->rd_rel->reltablespace;

	stmt->relation = makeRangeVar(pstrdup(nspName), pstrdup(shadowName), -1);
	stmt->relation->relpersistence = srcRel->rd_rel->relpersistence;
	stmt->oncommit = ONCOMMIT_NOOP;

	/*
	 * Name the tablespace explicitly, even the database default: otherwise
	 * DefineRelation would use default_tablespace, and since the cutover swaps
	 * reltablespace along with the storage, the source would silently move.
	 */
	if (!OidIsValid(srcTablespace))
		srcTablespace = MyDatabaseTableSpace;
	stmt->tablespacename = get_tablespace_name(srcTablespace);

	for (int i = 0; i < desc->natts; i++)
	{
		Form_pg_attribute attr = TupleDescAttr(desc, i);
		ColumnDef  *col;

		if (attr->attisdropped)
		{
			char	   *name = psprintf("%s%d", YB_OSC_DROPPED_COLUMN_PREFIX,
										attr->attnum);

			col = makeColumnDef(name, INT4OID, -1, InvalidOid);
			*placeholders = lappend(*placeholders, name);
		}
		else
		{
			col = makeColumnDef(NameStr(attr->attname), attr->atttypid,
								attr->atttypmod, attr->attcollation);
			col->is_not_null = attr->attnotnull;
			col->storage = attr->attstorage;
			for (int dim = 0; dim < attr->attndims; dim++)
				col->typeName->arrayBounds =
					lappend(col->typeName->arrayBounds, makeInteger(-1));
		}
		stmt->tableElts = lappend(stmt->tableElts, col);
	}

	YbGetTableProperties(srcRel);
	stmt->split_options = YbGetSplitOptions(srcRel);
	stmt->options = WithPresplitSentinel(RelationReloptions(RelationGetRelid(srcRel)));

	pgConstraint = table_open(ConstraintRelationId, RowExclusiveLock);
	YbATCopyPrimaryKeyToCreateStmt(srcRel, pgConstraint, stmt);
	AppendCheckConstraints(srcRel, pgConstraint, stmt);
	table_close(pgConstraint, RowExclusiveLock);

	return stmt;
}

static Oid
CreateShadow(Relation srcRel, const char *nspName, const char *shadowName,
			 const AlterTableUtilityContext *context)
{
	List	   *placeholders = NIL;
	CreateStmt *stmt = BuildShadowCreateStmt(srcRel, nspName, shadowName,
											 &placeholders);
	ObjectAddress address;
	Oid			shadowOid;

	address = DefineRelation(stmt, RELKIND_RELATION, srcRel->rd_rel->relowner,
							 NULL /* typaddress */ , "" /* queryString */ );
	shadowOid = address.objectId;
	CommandCounterIncrement();

	if (placeholders != NIL)
	{
		List	   *cmds = NIL;
		ListCell   *lc;

		foreach(lc, placeholders)
		{
			AlterTableCmd *cmd = makeNode(AlterTableCmd);

			cmd->subtype = AT_DropColumn;
			cmd->name = (char *) lfirst(lc);
			cmd->behavior = DROP_RESTRICT;
			cmd->missing_ok = false;
			cmds = lappend(cmds, cmd);
		}
		RunAlterTable(shadowOid, cmds, false /* recurse */ ,
					  context->queryString, context, NULL /* capture */ ,
					  InvalidOid /* shadowOid */ );
		CommandCounterIncrement();
	}

	return shadowOid;
}

/*
 * Clone every index of the source, the primary key included, onto the
 * shadow.  Returns the (source index, shadow index) pairs.
 */
static List *
CloneIndexes(Relation srcRel, Oid shadowOid)
{
	int			natts = RelationGetDescr(srcRel)->natts;
	AttrMap    *attmap = make_attrmap(natts);
	List	   *pairs = NIL;
	ListCell   *lc;

	/* Identity map: the shadow has the source's attnum layout. */
	for (int i = 0; i < natts; i++)
		attmap->attnums[i] = i + 1;

	foreach(lc, RelationGetIndexList(srcRel))
	{
		Oid			srcIndex = lfirst_oid(lc);
		Relation	idxRel = index_open(srcIndex, AccessShareLock);
		IndexStmt  *stmt = generateClonedIndexStmt(NULL /* heapRel */ , idxRel,
												   attmap, NULL /* constraintOid */ );
		ObjectAddress address;

		/*
		 * The shadow is empty, and an online build would commit transactions
		 * of its own from under the driver.
		 */
		stmt->concurrent = YB_CONCURRENCY_DISABLED;

		/* generateClonedIndexStmt drops yb_presplit; see WithPresplitSentinel. */
		stmt->options = WithPresplitSentinel(RelationReloptions(srcIndex));

		/* A YB primary key index has no DocDB table of its own. */
		if (!idxRel->rd_index->indisprimary)
		{
			Oid			idxTablespace = idxRel->rd_rel->reltablespace;

			YbGetTableProperties(idxRel);
			stmt->split_options = YbGetSplitOptions(idxRel);

			/* As for the table; see BuildShadowCreateStmt. */
			if (!OidIsValid(idxTablespace))
				idxTablespace = MyDatabaseTableSpace;
			stmt->tableSpace = get_tablespace_name(idxTablespace);
		}

		address = DefineIndex(shadowOid, stmt,
							  InvalidOid /* indexRelationId */ ,
							  InvalidOid /* parentIndexId */ ,
							  InvalidOid /* parentConstraintId */ ,
							  false /* is_alter_table */ ,
							  false /* check_rights */ ,
							  false /* check_not_in_use */ ,
							  false /* skip_build */ ,
							  true /* quiet */ );
		index_close(idxRel, AccessShareLock);
		CommandCounterIncrement();

		pairs = lappend(pairs, list_make2_oid(srcIndex, address.objectId));
	}

	return pairs;
}

static Oid
PairedShadowIndex(List *pairs, Oid srcIndex)
{
	ListCell   *lc;

	foreach(lc, pairs)
	{
		List	   *pair = (List *) lfirst(lc);

		if (linitial_oid(pair) == srcIndex)
			return lsecond_oid(pair);
	}
	return InvalidOid;
}

/*
 * The subcommand list to apply to the shadow: the statement's own, except
 * that ADD PRIMARY KEY / UNIQUE ... USING INDEX <name> names the shadow's
 * clone of that index.
 */
static List *
CmdsForShadow(List *cmds, Oid srcNsp, List *pairs)
{
	List	   *shadowCmds = copyObject(cmds);
	ListCell   *lc;

	foreach(lc, shadowCmds)
	{
		AlterTableCmd *cmd = lfirst_node(AlterTableCmd, lc);
		Constraint *con;
		Oid			shadowIndex;

		if (cmd->subtype != AT_AddConstraint || cmd->def == NULL ||
			!IsA(cmd->def, Constraint))
			continue;
		con = (Constraint *) cmd->def;
		if (con->indexname == NULL)
			continue;

		shadowIndex = PairedShadowIndex(pairs,
										 get_relname_relid(con->indexname, srcNsp));
		if (OidIsValid(shadowIndex))
			con->indexname = get_rel_name(shadowIndex);
	}
	return shadowCmds;
}

/*
 * Existing (pre-ALTER) source columns whose value this statement recomputes:
 * a non-generated NewColumnValue whose attnum is inside the source's attnum
 * range is a type change on a column the source already has.  A column the
 * statement added has an attnum beyond the source's natts.
 */
static Bitmapset *
AlteredSourceColumns(List *newvals, int srcNatts)
{
	Bitmapset  *altered = NULL;
	ListCell   *lc;

	foreach(lc, newvals)
	{
		YbNewColumnValue *val = (YbNewColumnValue *) lfirst(lc);

		if (!val->is_generated && val->attnum <= srcNatts)
			altered = bms_add_member(altered, val->attnum);
	}
	return altered;
}

/*
 * Refuse a type change on a foreign-key column, in either direction.  At
 * cutover ATPostAlterTypeCleanup would re-add the foreign key and validate it
 * by a full scan under AccessExclusiveLock, turning the brief cutover into an
 * unbounded exclusive window.
 */
static void
RefuseTypeChangeOnForeignKey(Relation srcRel, Bitmapset *altered)
{
	Oid			srcOid = RelationGetRelid(srcRel);
	TupleDesc	srcDesc = RelationGetDescr(srcRel);
	Relation	pgConstraint;
	ScanKeyData key[2];
	SysScanDesc scan;
	HeapTuple	tuple;
	ListCell   *lc;

	if (bms_is_empty(altered))
		return;

	foreach(lc, RelationGetFKeyList(srcRel))
	{
		ForeignKeyCacheInfo *fk = (ForeignKeyCacheInfo *) lfirst(lc);

		for (int i = 0; i < fk->nkeys; i++)
			if (bms_is_member(fk->conkey[i], altered))
				ereport(ERROR,
						(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
						 errmsg("ALTER TABLE ... CONCURRENTLY cannot change the type of a foreign-key column"),
						 errdetail("Column \"%s\" is part of a foreign key on table \"%s\".",
								   NameStr(TupleDescAttr(srcDesc, fk->conkey[i] - 1)->attname),
								   RelationGetRelationName(srcRel)),
						 errhint("Drop the foreign key, run the ALTER, then re-add it.")));
	}

	pgConstraint = table_open(ConstraintRelationId, AccessShareLock);
	ScanKeyInit(&key[0], Anum_pg_constraint_confrelid, BTEqualStrategyNumber,
				F_OIDEQ, ObjectIdGetDatum(srcOid));
	ScanKeyInit(&key[1], Anum_pg_constraint_contype, BTEqualStrategyNumber,
				F_CHAREQ, CharGetDatum(CONSTRAINT_FOREIGN));
	/* No index on confrelid; this runs once per statement, so a seqscan is fine. */
	scan = systable_beginscan(pgConstraint, InvalidOid /* no index */ ,
							  false /* indexOK */ , NULL /* snapshot */ ,
							  2 /* nkeys */ , key);
	while (HeapTupleIsValid(tuple = systable_getnext(scan)))
	{
		AttrNumber	conkey[INDEX_MAX_KEYS];
		AttrNumber	confkey[INDEX_MAX_KEYS];
		int			numfks;

		DeconstructFkConstraintRow(tuple, &numfks, conkey, confkey,
								   NULL, NULL, NULL, NULL, NULL);

		for (int i = 0; i < numfks; i++)
			if (bms_is_member(confkey[i], altered))
			{
				Form_pg_constraint con = (Form_pg_constraint) GETSTRUCT(tuple);
				char	   *conname = pstrdup(NameStr(con->conname));
				char	   *referencing = get_rel_name(con->conrelid);
				char	   *colname = pstrdup(NameStr(TupleDescAttr(srcDesc,
																   confkey[i] - 1)->attname));

				systable_endscan(scan);
				table_close(pgConstraint, AccessShareLock);
				ereport(ERROR,
						(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
						 errmsg("ALTER TABLE ... CONCURRENTLY cannot change the type of a foreign-key column"),
						 errdetail("Column \"%s\" is referenced by foreign key \"%s\" on table \"%s\".",
								   colname, conname, referencing ? referencing : "?"),
						 errhint("Drop the foreign key, run the ALTER, then re-add it.")));
			}
	}
	systable_endscan(scan);
	table_close(pgConstraint, AccessShareLock);
}

/*
 * eval_const_expressions turns an explicit COLLATE into a collation-only
 * implicit RelabelType, which ruleutils does not print, so the deparsed
 * transform would silently evaluate the expression under another collation.
 */
static bool
YbOscLostCollationWalker(Node *node, void *context)
{
	if (node == NULL)
		return false;
	if (IsA(node, RelabelType))
	{
		RelabelType *r = (RelabelType *) node;

		if (r->relabelformat == COERCE_IMPLICIT_CAST &&
			OidIsValid(r->resultcollid) &&
			r->resulttype == exprType((Node *) r->arg) &&
			r->resultcollid != exprCollation((Node *) r->arg))
			return true;
	}
	return expression_tree_walker(node, YbOscLostCollationWalker, context);
}

static void
RefuseLostCollation(List *newvals)
{
	ListCell   *lc;

	foreach(lc, newvals)
	{
		YbNewColumnValue *val = (YbNewColumnValue *) lfirst(lc);

		if (YbOscLostCollationWalker((Node *) val->expr, NULL))
			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					 errmsg("ALTER TABLE ... CONCURRENTLY cannot preserve an explicit COLLATE in a rewrite expression"),
					 errhint("Run the ALTER TABLE without CONCURRENTLY.")));
	}
}

/*
 * Errors that only the source's catalog state produces: the shadow carries no
 * column defaults, identity or generation attributes, so the T1 ALTER cannot
 * raise them, and they would otherwise surface only at cutover after the
 * whole copy.  Best-effort, not a guarantee.
 */
static void
CheckSourceOnlyAlterErrors(Relation srcRel, Relation shadowRel, Bitmapset *altered)
{
	TupleDesc	srcDesc = RelationGetDescr(srcRel);
	TupleDesc	shadowDesc = RelationGetDescr(shadowRel);
	int			attnum = -1;

	if (bms_is_empty(altered))
		return;

	/* A source default that no longer coerces to the column's new type. */
	while ((attnum = bms_next_member(altered, attnum)) >= 0)
	{
		Form_pg_attribute srcAttr = TupleDescAttr(srcDesc, attnum - 1);
		Form_pg_attribute newAttr;
		Node	   *def;

		if (!srcAttr->atthasdef || srcAttr->attgenerated)
			continue;
		newAttr = TupleDescAttr(shadowDesc, attnum - 1);
		def = build_column_default(srcRel, attnum);
		if (def != NULL &&
			coerce_to_target_type(NULL /* pstate */ , def, exprType(def),
								  newAttr->atttypid, newAttr->atttypmod,
								  COERCION_ASSIGNMENT, COERCE_IMPLICIT_CAST,
								  -1) == NULL)
			ereport(ERROR,
					(errcode(ERRCODE_DATATYPE_MISMATCH),
					 errmsg("default for column \"%s\" cannot be cast automatically to type %s",
							NameStr(srcAttr->attname),
							format_type_be(newAttr->atttypid))));
	}

	/* A source stored generated column that depends on an altered column. */
	for (int i = 0; i < srcDesc->natts; i++)
	{
		Form_pg_attribute genAttr = TupleDescAttr(srcDesc, i);
		Node	   *genexpr;
		Bitmapset  *refs = NULL;
		int			bit = -1;

		if (genAttr->attisdropped ||
			genAttr->attgenerated != ATTRIBUTE_GENERATED_STORED)
			continue;

		genexpr = build_column_default(srcRel, genAttr->attnum);
		pull_varattnos(genexpr, 1, &refs);
		while ((bit = bms_next_member(refs, bit)) >= 0)
		{
			AttrNumber	ref = bit + FirstLowInvalidHeapAttributeNumber;

			if (ref > 0 && bms_is_member(ref, altered))
				ereport(ERROR,
						(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
						 errmsg("cannot alter type of a column used by a generated column"),
						 errdetail("Column \"%s\" is used by generated column \"%s\".",
								   NameStr(TupleDescAttr(srcDesc, ref - 1)->attname),
								   NameStr(genAttr->attname))));
		}
		bms_free(refs);
	}
}

/*
 * Decide from what phase 2 recorded whether the statement rewrites at all
 * (false: the caller falls back to the ordinary ALTER TABLE) and, if so,
 * whether the rewrite is one the job can carry out.
 */
static bool
ClassifyShadowAlter(const YbAlterTableCapture *capture, Oid srcOid,
					Oid shadowOid)
{
	Relation	shadowRel;
	Relation	srcRel;
	TupleDesc	desc;
	Bitmapset  *altered;

	if (capture->rewrite == 0)
		return false;

	if (capture->rewrite & (AT_REWRITE_ACCESS_METHOD | AT_REWRITE_ALTER_PERSISTENCE))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("ALTER TABLE ... CONCURRENTLY cannot change the persistence or access method of a table")));

	shadowRel = table_open(shadowOid, NoLock);
	desc = RelationGetDescr(shadowRel);

	if (!OidIsValid(RelationGetPrimaryKeyIndex(shadowRel)))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("ALTER TABLE ... CONCURRENTLY cannot drop the primary key of a table"),
				 errdetail("Without a primary key the rewritten table would be keyed by a generated row id, which the change-capture trigger cannot recompute for the rows it replays.")));

	/*
	 * The source's identity and generated columns were cloned as plain
	 * columns, so any on the shadow were added by this statement.  The
	 * framework supports neither on a target.
	 */
	for (int i = 0; i < desc->natts; i++)
	{
		Form_pg_attribute attr = TupleDescAttr(desc, i);

		if (attr->attisdropped)
			continue;
		if (attr->attidentity || attr->attgenerated)
			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					 errmsg("ALTER TABLE ... CONCURRENTLY cannot add an identity or generated column"),
					 errdetail("Column \"%s\" is one.", NameStr(attr->attname))));
	}

	/*
	 * A serial column's sequence would belong to the shadow and be dropped
	 * with it, and the cutover would give the source a fresh sequence that
	 * repeats the values the copy assigned.
	 */
	if (getOwnedSequences(shadowOid) != NIL)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("ALTER TABLE ... CONCURRENTLY cannot add a serial column"),
				 errhint("Create the sequence first and add the column with DEFAULT nextval(...).")));

	/*
	 * The remaining checks need the source's foreign keys, defaults and
	 * generated columns, which the shadow does not carry.
	 */
	srcRel = table_open(srcOid, NoLock);
	altered = AlteredSourceColumns(capture->newvals, RelationGetDescr(srcRel)->natts);

	RefuseTypeChangeOnForeignKey(srcRel, altered);
	RefuseLostCollation(capture->newvals);
	CheckSourceOnlyAlterErrors(srcRel, shadowRel, altered);

	bms_free(altered);
	table_close(srcRel, NoLock);
	table_close(shadowRel, NoLock);
	return true;
}

/*
 * Vars of a captured new column value address the shadow's attributes.  Map
 * them onto the source's by name: pre-existing columns have the same attnum
 * on both sides, and a column this statement added cannot be referenced by
 * the same statement's expressions, so this is a check as much as a mapping.
 */
static Node *
RemapShadowVarsMutator(Node *node, YbOscVarRemapContext *ctx)
{
	if (node == NULL)
		return NULL;

	if (IsA(node, Var))
	{
		Var		   *var = (Var *) node;
		Var		   *newvar;
		const char *attname;
		AttrNumber	srcAttno;

		if (var->varattno == 0)
			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					 errmsg("ALTER TABLE ... CONCURRENTLY does not support whole-row references in rewrite expressions")));
		if (var->varattno < 0)
			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					 errmsg("ALTER TABLE ... CONCURRENTLY does not support system column references in rewrite expressions")));

		attname = NameStr(TupleDescAttr(RelationGetDescr(ctx->shadow_rel),
										var->varattno - 1)->attname);
		srcAttno = get_attnum(RelationGetRelid(ctx->src_rel), attname);
		if (srcAttno == InvalidAttrNumber)
			elog(ERROR, "column \"%s\" referenced by a rewrite expression does not exist in table \"%s\"",
				 attname, RelationGetRelationName(ctx->src_rel));

		newvar = copyObject(var);
		newvar->varno = 1;
		newvar->varattno = srcAttno;
		newvar->varnosyn = 1;
		newvar->varattnosyn = srcAttno;
		return (Node *) newvar;
	}

	return expression_tree_mutator(node, RemapShadowVarsMutator, (void *) ctx);
}

/*
 * A whole-row Var of the source (from a generation expression) deparses as
 * "s.*", which a SELECT list would expand; spell it as ROW(...)::type over the
 * source's attributes instead, with NULL placeholders for dropped ones.
 */
static Node *
SourceWholeRowMutator(Node *node, Relation srcRel)
{
	if (node == NULL)
		return NULL;

	if (IsA(node, Var))
	{
		Var		   *var = (Var *) node;

		if (var->varattno < 0)
			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					 errmsg("ALTER TABLE ... CONCURRENTLY does not support system column references in rewrite expressions")));

		if (var->varattno == 0)
		{
			TupleDesc	desc = RelationGetDescr(srcRel);
			RowExpr    *row = makeNode(RowExpr);

			if (var->vartype != srcRel->rd_rel->reltype)
				ereport(ERROR,
						(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
						 errmsg("ALTER TABLE ... CONCURRENTLY does not support whole-row references in rewrite expressions")));

			for (int i = 0; i < desc->natts; i++)
			{
				Form_pg_attribute attr = TupleDescAttr(desc, i);
				Node	   *arg = attr->attisdropped ?
					(Node *) makeNullConst(INT4OID, -1, InvalidOid) :
					(Node *) makeVar(1, attr->attnum, attr->atttypid,
									 attr->atttypmod, attr->attcollation, 0);

				row->args = lappend(row->args, arg);
			}
			row->row_typeid = var->vartype;
			row->row_format = COERCE_EXPLICIT_CAST;
			row->colnames = NIL;
			row->location = -1;
			return (Node *) row;
		}
		return (Node *) copyObject(var);
	}

	return expression_tree_mutator(node, SourceWholeRowMutator, (void *) srcRel);
}

static YbNewColumnValue *
FindNewColumnValue(List *newvals, AttrNumber attnum)
{
	ListCell   *lc;

	foreach(lc, newvals)
	{
		YbNewColumnValue *val = (YbNewColumnValue *) lfirst(lc);

		if (val->attnum == attnum && !val->is_generated)
			return val;
	}
	return NULL;
}

/*
 * The transform's SELECT list: one expression over the source row (alias
 * "s") per live shadow column, in attnum order, computing what
 * ATRewriteTable would.  A column phase 2 recorded a new value for takes that
 * expression; any other column the source also has is passed through (a
 * stored generated one by its generation expression); a column the statement
 * added takes its default, or NULL.
 */
static char *
BuildTransformSelectList(Relation srcRel, Relation shadowRel, List *newvals)
{
	Oid			srcOid = RelationGetRelid(srcRel);
	TupleDesc	srcDesc = RelationGetDescr(srcRel);
	TupleDesc	shadowDesc = RelationGetDescr(shadowRel);
	List	   *context = deparse_context_for("s", srcOid);
	YbOscVarRemapContext remap = {.src_rel = srcRel,.shadow_rel = shadowRel};
	StringInfoData buf;
	int			saveNestlevel;

	initStringInfo(&buf);

	/*
	 * Deparse with an empty search_path so that ruleutils schema-qualifies
	 * every name outside pg_catalog.  PL/pgSQL parses the body when the
	 * capture trigger or the backfill first calls it, under the function's
	 * own search_path of pg_catalog, pg_temp.
	 */
	saveNestlevel = NewGUCNestLevel();
	(void) set_config_option("search_path", "",
							 PGC_USERSET, PGC_S_SESSION,
							 GUC_ACTION_SAVE, true, 0, false);

	for (int i = 0; i < shadowDesc->natts; i++)
	{
		Form_pg_attribute attr = TupleDescAttr(shadowDesc, i);
		YbNewColumnValue *val;
		Node	   *expr;

		if (attr->attisdropped)
			continue;

		val = FindNewColumnValue(newvals, attr->attnum);
		if (val != NULL)
			expr = RemapShadowVarsMutator((Node *) val->expr, &remap);
		else if (attr->attnum <= srcDesc->natts)
		{
			/*
			 * Matched by attnum, not name: a column this statement drops and
			 * re-adds under the same name gets an attnum beyond the source's
			 * natts and must not receive the dropped column's data.
			 */
			Form_pg_attribute srcAttr = TupleDescAttr(srcDesc, attr->attnum - 1);

			if (srcAttr->attgenerated)
				expr = build_column_default(srcRel, attr->attnum);
			else
				expr = (Node *) makeVar(1, attr->attnum, srcAttr->atttypid,
										srcAttr->atttypmod,
										srcAttr->attcollation, 0);
		}
		else
		{
			expr = build_column_default(shadowRel, attr->attnum);
			if (expr == NULL)
				expr = (Node *) makeNullConst(attr->atttypid, attr->atttypmod,
											  attr->attcollation);
		}
		expr = SourceWholeRowMutator(expr, srcRel);

		if (buf.len > 0)
			appendStringInfoString(&buf, ", ");
		/* Show implicit casts: the coercions are part of the rewrite. */
		appendStringInfoString(&buf,
							   deparse_expression(expr, context,
												  true /* forceprefix */ ,
												  true /* showimplicit */ ,
												  false /* yb_pretty */ ,
												  false /* yb_maskconstants */ ));
	}

	AtEOXact_GUC(true, saveNestlevel);

	return buf.data;
}

static const char *
DollarQuoteTag(const char *body)
{
	for (int i = 0;; i++)
	{
		const char *tag = (i == 0) ? "$yb_osc$" : psprintf("$yb_osc%d$", i);

		if (strstr(body, tag) == NULL)
			return tag;
	}
}

/*
 * Create the transform, a PL/pgSQL function from the source row type to the
 * shadow row type that returns ROW(<SELECT list>).  It is declared
 * IMMUTABLE, as the framework requires.  Begin checks each expression of the
 * SELECT list (CheckTransformBody); the only ones it lets through that are not
 * immutable are volatile ones for columns no index covers, which the trigger
 * never uses as a key.
 * The deparsed names outside pg_catalog are schema-qualified, so the function
 * pins search_path to pg_catalog, pg_temp.
 */
static Oid
YbOscCreateTransform(Relation srcRel, Relation shadowRel, const char *nspName,
				const char *fnName, List *newvals)
{
	char	   *selectList = BuildTransformSelectList(srcRel, shadowRel, newvals);
	const char *tag = DollarQuoteTag(selectList);
	Oid			argtypes[1] = {srcRel->rd_rel->reltype};

	ExecUtilitySql(psprintf("CREATE FUNCTION %s(s %s) RETURNS %s "
							"LANGUAGE plpgsql IMMUTABLE "
							"SET search_path = pg_catalog, pg_temp "
							"AS %s BEGIN RETURN ROW(%s)::%s; END %s",
							quote_qualified_identifier(nspName, fnName),
							format_type_be_qualified(srcRel->rd_rel->reltype),
							format_type_be_qualified(shadowRel->rd_rel->reltype),
							tag, selectList,
							format_type_be_qualified(shadowRel->rd_rel->reltype),
							tag));
	CommandCounterIncrement();

	return LookupFuncName(list_make2(makeString(pstrdup(nspName)),
									 makeString(pstrdup(fnName))),
						  1, argtypes, false /* missing_ok */ );
}

/*
 * Wait for the job's copy to end and return the state it ended in, and its
 * error message, if any, in PortalContext.  A master that cannot be asked is
 * asked again, so that a briefly unreachable master does not fail the
 * statement.
 */
static YbcPgTableBackfillState
WaitForCopy(Oid srcOid, int64 jobId, char **errorMessage)
{
	MemoryContext pollcxt = AllocSetContextCreate(CurrentMemoryContext,
												  "ALTER TABLE CONCURRENTLY poll",
												  ALLOCSET_SMALL_SIZES);
	YbcPgTableBackfillState state;
	char	   *error = NULL;

	for (;;)
	{
		MemoryContext oldcxt;
		bool		known;

		CHECK_FOR_INTERRUPTS();

		MemoryContextReset(pollcxt);
		oldcxt = MemoryContextSwitchTo(pollcxt);
		known = YbOscGetCopyState(srcOid, jobId, &state, &error);
		MemoryContextSwitchTo(oldcxt);

		if (known && state != YB_TABLE_BACKFILL_RUNNING)
			break;
		pg_usleep(yb_alter_table_concurrently_poll_interval_ms * 1000L);
	}

	*errorMessage = error ? MemoryContextStrdup(PortalContext, error) : NULL;
	MemoryContextDelete(pollcxt);
	return state;
}

/*
 * Re-take the statement's session lock after a transaction abort released it.
 * With object locking a session lock may only be taken over a transaction
 * lock on the same relation.
 */
static void
RelockSession(LockRelId *lockrelid, LOCKMODE lockmode)
{
	LockRelationId(lockrelid, lockmode);
	LockRelationIdForSession(lockrelid, lockmode);
}

/*
 * Undo the statement after an error in step 2 or 3 (see the file header):
 * abort the failed transaction, roll back job jobId (YbOscAbort, which also
 * drops the shadow and the transform), or, before the job began, drop the
 * shadow, in a DDL transaction of its own, and re-raise edata.  If the
 * rollback itself fails, it is reported as a WARNING and edata's hint names
 * the manual way out.
 */
static void
pg_attribute_noreturn()
RollBackAndRethrow(int64 jobId, Oid shadowOid, const YbOscTxnState *txn,
				   ErrorData *edata)
{
	AbortCurrentTransaction();
	StartTransactionCommand();

	PG_TRY();
	{
		YbOscTxnState scratch;

		ResumeDdlTransaction(txn);
		PushActiveSnapshot(GetTransactionSnapshot());
		if (jobId != 0)
			YbOscAbort(jobId);
		else
			DropShadow(shadowOid);
		CommitAndStartPlainTransaction(&scratch);
	}
	PG_CATCH();
	{
		ErrorData  *cleanupError;

		MemoryContextSwitchTo(PortalContext);
		cleanupError = CopyErrorData();
		FlushErrorState();
		AbortCurrentTransaction();
		StartTransactionCommand();

		ereport(WARNING,
				(errmsg("could not roll back ALTER TABLE ... CONCURRENTLY"),
				 errdetail("%s", cleanupError->message)));
		if (edata->hint == NULL)
			edata->hint = jobId != 0 ?
				psprintf("Roll the job back with yb_abort_mirror_table(%lld).",
						 (long long) jobId) :
				pstrdup("The next ALTER TABLE ... CONCURRENTLY on the table drops the shadow table left behind.");
	}
	PG_END_TRY();

	ReThrowError(edata);
}

/* Each retry re-runs only the begin, not the copy, so the cap is generous. */
#define YB_OSC_BEGIN_MAX_ATTEMPTS 20

static bool
IsRetryableBeginError(int sqlerrcode)
{
	return sqlerrcode == ERRCODE_YB_DEADLOCK ||
		sqlerrcode == ERRCODE_T_R_DEADLOCK_DETECTED ||
		sqlerrcode == ERRCODE_YB_TXN_CONFLICT ||
		sqlerrcode == ERRCODE_T_R_SERIALIZATION_FAILURE ||
		sqlerrcode == ERRCODE_YB_TXN_ABORTED;
}

/*
 * Begin the job in a DDL transaction of its own, which the caller commits.
 *
 * YbOscBegin takes ExclusiveLock on the source to fence DML, and under a
 * write workload the deadlock detector may pick it as the victim.  The begin
 * has made nothing durable by then, so it is retried in a fresh transaction.
 * Anything else, or running out of attempts, rolls the statement back.
 */
static int64
BeginJob(Oid srcOid, Oid shadowOid, Oid transformOid,
		 const YbOscBeginOptions *opts, const YbOscTxnState *txn,
		 LockRelId *lockrelid, LOCKMODE lockmode)
{
	for (int attempt = 1;; attempt++)
	{
		ErrorData  *edata = NULL;
		int64		jobId = 0;

		ResumeDdlTransaction(txn);
		PushActiveSnapshot(GetTransactionSnapshot());
		PG_TRY();
		{
			jobId = YbOscBegin(srcOid, shadowOid, transformOid, opts);
		}
		PG_CATCH();
		{
			MemoryContextSwitchTo(PortalContext);
			edata = CopyErrorData();
			FlushErrorState();
		}
		PG_END_TRY();

		if (edata == NULL)
		{
			PopActiveSnapshot();
			return jobId;
		}

		if (!IsRetryableBeginError(edata->sqlerrcode) ||
			attempt >= YB_OSC_BEGIN_MAX_ATTEMPTS)
			RollBackAndRethrow(0 /* jobId */ , shadowOid, txn, edata);

		/* The abort also drops the snapshot and the session lock. */
		AbortCurrentTransaction();
		StartTransactionCommand();

		ereport(LOG,
				(errmsg("retrying the begin of ALTER TABLE ... CONCURRENTLY on table \"%s\" after a transient conflict (attempt %d): %s",
						get_rel_name(srcOid), attempt, edata->message)));
		FreeErrorData(edata);
		pg_usleep(Min(attempt, 10) * 100000L);	/* linear backoff, capped at 1s */
		RelockSession(lockrelid, lockmode);
	}
}

/*
 * Step 1 (see the file header): create the shadow and the transform and
 * check the job.  Returns the shadow, or InvalidOid if the statement turned
 * out not to rewrite, in which case it has been run as an ordinary ALTER
 * TABLE.  Closes rel.
 */
static Oid
PrepareShadow(Relation rel, AlterTableStmt *stmt,
			  AlterTableUtilityContext *context,
			  const YbOscBeginOptions *opts, Oid *transformOid)
{
	Oid			relid = RelationGetRelid(rel);
	char	   *nspName = get_namespace_name(RelationGetNamespace(rel));
	char	   *shadowName = ShadowName(relid);
	YbAlterTableCapture capture = {0};
	Oid			shadowOid;
	List	   *indexPairs;
	List	   *shadowCmds;
	Relation	shadowRel;

	DropOrphanShadow(rel, shadowName);

	shadowOid = CreateShadow(rel, nspName, shadowName, context);
	indexPairs = CloneIndexes(rel, shadowOid);
	shadowCmds = CmdsForShadow(stmt->cmds, RelationGetNamespace(rel), indexPairs);
	table_close(rel, NoLock);

	RunAlterTable(shadowOid, shadowCmds, stmt->relation->inh,
				  context->queryString, context, &capture,
				  InvalidOid /* shadowOid */ );
	CommandCounterIncrement();

	if (!ClassifyShadowAlter(&capture, relid, shadowOid))
	{
		ereport(NOTICE,
				(errmsg("no table rewrite required; executing ALTER TABLE non-concurrently")));
		DropShadow(shadowOid);
		LockRelationOid(relid, AccessExclusiveLock);
		AlterTable(stmt, AccessExclusiveLock, context);
		return InvalidOid;
	}

	rel = table_open(relid, NoLock);
	shadowRel = table_open(shadowOid, NoLock);
	*transformOid = YbOscCreateTransform(rel, shadowRel, nspName,
										 TransformName(relid), capture.newvals);
	table_close(shadowRel, NoLock);
	table_close(rel, NoLock);

	/*
	 * Refuse what begin would refuse now, so that the error rolls the shadow
	 * and the transform back with this transaction.
	 */
	YbOscValidateJob(relid, shadowOid, *transformOid, opts);
	return shadowOid;
}

/*
 * Error context of a failed cutover, which leaves the job active with its
 * copy complete.
 */
static void
CutoverErrorCallback(void *arg)
{
	int64		jobId = *(int64 *) arg;

	errcontext("cutover of online schema change job %lld, which stays active: re-issue the same ALTER TABLE ... CONCURRENTLY statement to retry it",
			   (long long) jobId);
}

void
YbAlterTableConcurrently(AlterTableStmt *stmt, Oid relid, LOCKMODE lockmode,
						 AlterTableUtilityContext *context)
{
	Relation	rel;
	LockRelId	lockrelid;
	YbOscBeginOptions opts;
	YbOscTxnState txn;
	int64		jobId;
	ErrorData  *edata = NULL;
	ErrorContextCallback errcallback;

	YbOscCheckTransactionalDdl();

	/* The caller holds lockmode on relid. */
	rel = table_open(relid, NoLock);
	CheckSourceEligible(rel, stmt);

	/*
	 * Hold the lock across the commits below, as CREATE INDEX CONCURRENTLY
	 * does.  A transaction abort releases session locks too, so the error
	 * paths need no explicit unlock.
	 */
	lockrelid = rel->rd_lockInfo.lockRelId;
	LockRelationIdForSession(&lockrelid, lockmode);

	jobId = FindJobToResume(rel, stmt);
	if (jobId == 0)
	{
		Oid			shadowOid;
		Oid			transformOid;

		opts.cutover_mode = YB_OSC_CUTOVER_SWAP;
		opts.origin = YB_OSC_ORIGIN_ALTER_TABLE;
		opts.query = AlterStatementText(context);

		shadowOid = PrepareShadow(rel, stmt, context, &opts, &transformOid);
		if (!OidIsValid(shadowOid))
		{
			UnlockRelationIdForSession(&lockrelid, lockmode);
			return;
		}

		CommitAndStartPlainTransaction(&txn);
		jobId = BeginJob(relid, shadowOid, transformOid, &opts, &txn,
						 &lockrelid, lockmode);
	}
	else
		table_close(rel, NoLock);

	/*
	 * Wait in a plain transaction: it conflicts with no other DDL.  A cancel
	 * or a failed copy rolls the job back.
	 */
	CommitAndStartPlainTransaction(&txn);
	PG_TRY();
	{
		char	   *errorMessage;

		if (WaitForCopy(relid, jobId, &errorMessage) != YB_TABLE_BACKFILL_SUCCEEDED)
			ereport(ERROR,
					(errcode(ERRCODE_INTERNAL_ERROR),
					 errmsg("ALTER TABLE ... CONCURRENTLY failed while copying table \"%s\"",
							get_rel_name(relid)),
					 errdetail("%s", errorMessage ? errorMessage : "The copy was aborted.")));
	}
	PG_CATCH();
	{
		MemoryContextSwitchTo(PortalContext);
		edata = CopyErrorData();
		FlushErrorState();
	}
	PG_END_TRY();
	if (edata != NULL)
		RollBackAndRethrow(jobId, InvalidOid, &txn, edata);

	/*
	 * Cut over in the transaction the utility layer commits.  A failure rolls
	 * the cutover back as a whole and leaves the job active.  The snapshot
	 * stands in for the portal's, popped at the first commit.
	 */
	errcallback.callback = CutoverErrorCallback;
	errcallback.arg = &jobId;
	errcallback.previous = error_context_stack;
	error_context_stack = &errcallback;

	ResumeDdlTransaction(&txn);
	PushActiveSnapshot(GetTransactionSnapshot());
	YbOscFinish(jobId);
	PopActiveSnapshot();

	error_context_stack = errcallback.previous;
	UnlockRelationIdForSession(&lockrelid, lockmode);
}

void
YbOscCutover(Oid srcOid, Oid dstOid, int64 jobId, const char *query)
{
	AlterTableStmt *atstmt = ParseJobStatement(jobId, query);
	Oid			namedOid;
	YbOscJob	job;

	/*
	 * The source is known by OID; the statement's table name is only checked
	 * against it, since the text was written under the driver's search_path.
	 */
	namedOid = RangeVarGetRelid(atstmt->relation, NoLock, true /* missing_ok */ );
	if (OidIsValid(namedOid) && namedOid != srcOid)
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("the ALTER TABLE statement of online schema change job %lld names table \"%s\", not the job's source table \"%s\"",
						(long long) jobId, atstmt->relation->relname,
						get_rel_name(srcOid)),
				 errhint("Set search_path so that the name resolves to the source table.")));

	/*
	 * The transform depends on the shadow's row type and would block the
	 * shadow's drop; the triggers that evaluated it are already gone.
	 */
	if (!YbOscFetchJob(jobId, &job))
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("online schema change job %lld does not exist", (long long) jobId)));
	if (OidIsValid(job.transform_fn_oid) &&
		SearchSysCacheExists1(PROCOID, ObjectIdGetDatum(job.transform_fn_oid)))
	{
		ObjectAddress object;

		ObjectAddressSet(object, ProcedureRelationId, job.transform_fn_oid);
		performDeletion(&object, DROP_RESTRICT, PERFORM_DELETION_INTERNAL);
		CommandCounterIncrement();
	}

	RunAlterTable(srcOid, atstmt->cmds, atstmt->relation->inh, query,
				  NULL /* outer */ , NULL /* capture */ , dstOid);

	if (yb_test_fail_osc_cutover)
		ereport(ERROR,
				(errcode(ERRCODE_INTERNAL_ERROR),
				 errmsg("ALTER TABLE ... CONCURRENTLY cutover failed because yb_test_fail_osc_cutover is set")));
}

/*
 * Refuse a swap when the source's post-ALTER columns differ from the
 * shadow's, for instance because a recovery cutover replayed the statement
 * under a different search_path.  Swapping anyway would put a DocDB table of
 * the wrong layout under the source.
 */
static void
CheckAttrsMatch(Relation srcRel, Relation shadowRel)
{
	TupleDesc	src = RelationGetDescr(srcRel);
	TupleDesc	shd = RelationGetDescr(shadowRel);

	for (int i = 0; i < Max(src->natts, shd->natts); i++)
	{
		Form_pg_attribute a = i < src->natts ? TupleDescAttr(src, i) : NULL;
		Form_pg_attribute b = i < shd->natts ? TupleDescAttr(shd, i) : NULL;

		if (a && b && a->attisdropped && b->attisdropped)
			continue;
		if (a && b && !a->attisdropped && !b->attisdropped &&
			strcmp(NameStr(a->attname), NameStr(b->attname)) == 0 &&
			a->atttypid == b->atttypid &&
			a->atttypmod == b->atttypmod &&
			a->attcollation == b->attcollation &&
			a->attnotnull == b->attnotnull)
			continue;

		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("table \"%s\" no longer matches its shadow at cutover",
						RelationGetRelationName(srcRel)),
				 errdetail("Column %d differs.", i + 1),
				 errhint("Abort the online schema change with yb_abort_mirror_table and retry it.")));
	}
}

/* Whether two relations have the same reloptions, ignoring those YB does not persist. */
static bool
ReloptionsMatch(Oid rel1, Oid rel2)
{
	List	   *opts[2] = {NIL, NIL};
	Oid			relids[2] = {rel1, rel2};

	for (int i = 0; i < 2; i++)
	{
		HeapTuple	tuple = SearchSysCache1(RELOID, ObjectIdGetDatum(relids[i]));
		Datum		datum;
		bool		isnull;

		if (!HeapTupleIsValid(tuple))
			elog(ERROR, "cache lookup failed for relation %u", relids[i]);
		datum = SysCacheGetAttr(RELOID, tuple, Anum_pg_class_reloptions, &isnull);
		if (!isnull)
			opts[i] = untransformRelOptions(ybExcludeNonPersistentReloptions(datum));
		ReleaseSysCache(tuple);
	}

	/* DefElems of String values: equal() compares names and values. */
	return equal(opts[0], opts[1]);
}

/* pg_index.indclass of indexOid, copied. */
static oidvector *
IndexClasses(Oid indexOid)
{
	HeapTuple	tuple = SearchSysCache1(INDEXRELID, ObjectIdGetDatum(indexOid));
	Datum		datum;
	bool		isnull;
	oidvector  *result;

	if (!HeapTupleIsValid(tuple))
		elog(ERROR, "cache lookup failed for index %u", indexOid);
	datum = SysCacheGetAttr(INDEXRELID, tuple, Anum_pg_index_indclass, &isnull);
	Assert(!isnull);
	result = (oidvector *) PG_DETOAST_DATUM_COPY(datum);
	ReleaseSysCache(tuple);
	return result;
}

/*
 * Whether an index of the source and an index of the shadow have the same
 * definition: what pg_get_indexdef shows, but the names.  Key columns are
 * compared by name, expressions and predicates by their deparsed text over
 * the same alias.
 */
static bool
IndexDefsMatch(Oid srcIndex, Oid srcHeap, Oid shadowIndex, Oid shadowHeap)
{
	Relation	a = index_open(srcIndex, AccessExclusiveLock);
	Relation	b = index_open(shadowIndex, AccessExclusiveLock);
	Form_pg_index fa = a->rd_index;
	Form_pg_index fb = b->rd_index;
	bool		match;

	match = fa->indisunique == fb->indisunique &&
		fa->indisprimary == fb->indisprimary &&
		fa->indisexclusion == fb->indisexclusion &&
		fa->indnullsnotdistinct == fb->indnullsnotdistinct &&
		fa->indnatts == fb->indnatts &&
		fa->indnkeyatts == fb->indnkeyatts &&
		a->rd_rel->relam == b->rd_rel->relam &&
		a->rd_rel->reltablespace == b->rd_rel->reltablespace;

	for (int i = 0; match && i < fa->indnatts; i++)
	{
		AttrNumber	ka = fa->indkey.values[i];
		AttrNumber	kb = fb->indkey.values[i];

		if ((ka == 0) != (kb == 0) ||
			(ka != 0 && strcmp(get_attname(srcHeap, ka, false),
							   get_attname(shadowHeap, kb, false)) != 0))
			match = false;
	}

	/* Opclasses, collations and options exist for key columns only. */
	if (match)
	{
		oidvector  *ca = IndexClasses(srcIndex);
		oidvector  *cb = IndexClasses(shadowIndex);

		for (int i = 0; match && i < fa->indnkeyatts; i++)
		{
			if (ca->values[i] != cb->values[i] ||
				a->rd_indcollation[i] != b->rd_indcollation[i] ||
				a->rd_indoption[i] != b->rd_indoption[i])
				match = false;
		}
	}

	if (match)
	{
		List	   *ctxA = deparse_context_for("t", srcHeap);
		List	   *ctxB = deparse_context_for("t", shadowHeap);
		Node	   *exprsA = (Node *) RelationGetIndexExpressions(a);
		Node	   *exprsB = (Node *) RelationGetIndexExpressions(b);
		Node	   *predA = (Node *) make_ands_explicit(RelationGetIndexPredicate(a));
		Node	   *predB = (Node *) make_ands_explicit(RelationGetIndexPredicate(b));

		match = strcmp(deparse_expression(exprsA, ctxA, false, false, false, false),
					   deparse_expression(exprsB, ctxB, false, false, false, false)) == 0 &&
			strcmp(deparse_expression(predA, ctxA, false, false, false, false),
				   deparse_expression(predB, ctxB, false, false, false, false)) == 0;
	}

	match = match && ReloptionsMatch(srcIndex, shadowIndex);

	index_close(a, NoLock);
	index_close(b, NoLock);
	return match;
}

/*
 * Give srcIndex the shadow index's DocDB index table and validity flags,
 * leaving the old table on shadowIndex for the shadow's drop.  A YB primary
 * key index has no DocDB table of its own, so only its flags are copied.
 * Returns whether a DocDB index table changed hands.
 */
static bool
SwapIndexFiles(Oid srcIndex, Oid shadowIndex)
{
	Relation	pgIndex = table_open(IndexRelationId, RowExclusiveLock);
	HeapTuple	srcTuple = SearchSysCacheCopy1(INDEXRELID, ObjectIdGetDatum(srcIndex));
	HeapTuple	shadowTuple = SearchSysCache1(INDEXRELID, ObjectIdGetDatum(shadowIndex));
	Form_pg_index srcForm;
	Form_pg_index shadowForm;
	bool		isPrimary;

	if (!HeapTupleIsValid(srcTuple))
		elog(ERROR, "cache lookup failed for index %u", srcIndex);
	if (!HeapTupleIsValid(shadowTuple))
		elog(ERROR, "cache lookup failed for index %u", shadowIndex);
	srcForm = (Form_pg_index) GETSTRUCT(srcTuple);
	shadowForm = (Form_pg_index) GETSTRUCT(shadowTuple);
	isPrimary = srcForm->indisprimary;

	if (srcForm->indisvalid != shadowForm->indisvalid ||
		srcForm->indisready != shadowForm->indisready ||
		srcForm->indislive != shadowForm->indislive)
	{
		srcForm->indisvalid = shadowForm->indisvalid;
		srcForm->indisready = shadowForm->indisready;
		srcForm->indislive = shadowForm->indislive;
		CatalogTupleUpdate(pgIndex, &srcTuple->t_self, srcTuple);
	}

	ReleaseSysCache(shadowTuple);
	heap_freetuple(srcTuple);
	table_close(pgIndex, RowExclusiveLock);

	if (isPrimary)
	{
		CacheInvalidateRelcacheByRelid(srcIndex);
		return false;
	}

	YbSwapRelationFiles(srcIndex, shadowIndex, true /* is_internal */ ,
						InvalidTransactionId, InvalidMultiXactId);
	return true;
}

/*
 * relid, the source table or one of its indexes, now owns a DocDB table that
 * was created for the shadow and still carries the shadow's name and OID as
 * its pg_table_id.  Rename it so that master-side tools show the source's
 * names (the master allows the transient duplicate with the old table), and
 * point its pg_table_id at relid, which the DDL-atomicity verifier, CDC and
 * backup use to map it to pg_class.
 */
static void
AdoptDocDBTable(Oid relid, ObjectType objtype)
{
	YbcPgStatement handle = NULL;
	Oid			databaseId = YBCGetDatabaseOidByRelid(relid);

	YBCRename(relid, objtype, get_rel_name(relid), NULL /* colname */ );
	HandleYBStatus(YBCPgNewAlterTable(databaseId,
									  YbGetRelfileNodeIdFromRelId(relid), &handle));
	HandleYBStatus(YBCPgAlterTableSetPgTableId(handle, databaseId, relid));
	YBCExecAlterTable(handle, relid);
}

/*
 * The shadow is the rewritten table: the job filled it, and it enforced the
 * new constraints on every row written to it.  So where an ordinary rewrite
 * builds a transient heap, copies into it and calls finish_heap_swap, which
 * reindexes, this swaps relfilenodes with the shadow, the table and each
 * index with its counterpart, and drops the shadow, which then holds the old
 * storage.  Phase 2 ran the same subcommands on both sides, so their indexes
 * correspond one to one, and are paired by definition.
 */
void
YbOscSwapWithShadow(Oid sourceOid, Oid shadowOid)
{
	Relation	sourceRel = table_open(sourceOid, NoLock);
	Relation	shadowRel = table_open(shadowOid, AccessExclusiveLock);
	List	   *shadowIndexes = RelationGetIndexList(shadowRel);
	List	   *pairedSource = NIL;
	List	   *pairedShadow = NIL;
	List	   *adoptedIndexes = NIL;
	ListCell   *lc;
	ListCell   *lc2;

	if (sourceRel->rd_rel->relkind != RELKIND_RELATION ||
		shadowRel->rd_rel->relkind != RELKIND_RELATION ||
		!IsYBRelation(sourceRel) || !IsYBRelation(shadowRel) ||
		sourceRel->rd_rel->relpersistence != shadowRel->rd_rel->relpersistence)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("ALTER TABLE ... CONCURRENTLY cannot swap table \"%s\" with \"%s\"",
						RelationGetRelationName(sourceRel),
						RelationGetRelationName(shadowRel))));
	CheckAttrsMatch(sourceRel, shadowRel);

	foreach(lc, RelationGetIndexList(sourceRel))
	{
		Oid			srcIndex = lfirst_oid(lc);
		Oid			match = InvalidOid;

		foreach(lc2, shadowIndexes)
		{
			if (IndexDefsMatch(srcIndex, sourceOid, lfirst_oid(lc2), shadowOid))
			{
				match = lfirst_oid(lc2);
				break;
			}
		}
		if (!OidIsValid(match))
			ereport(ERROR,
					(errcode(ERRCODE_INTERNAL_ERROR),
					 errmsg("could not pair index \"%s\" with a shadow index",
							get_rel_name(srcIndex))));
		pairedSource = lappend_oid(pairedSource, srcIndex);
		pairedShadow = lappend_oid(pairedShadow, match);
		shadowIndexes = list_delete_oid(shadowIndexes, match);
	}
	if (shadowIndexes != NIL)
		ereport(ERROR,
				(errcode(ERRCODE_INTERNAL_ERROR),
				 errmsg("could not pair shadow index \"%s\" with an index of \"%s\"",
						get_rel_name(linitial_oid(shadowIndexes)),
						RelationGetRelationName(sourceRel))));

	/* Like finish_heap_swap, swap with the relations closed; locks stay. */
	table_close(sourceRel, NoLock);
	table_close(shadowRel, NoLock);

	/*
	 * Same frozenXid and cutoffMulti as finish_heap_swap after a rewrite:
	 * every row of the adopted table was written by the job.
	 */
	YbSwapRelationFiles(sourceOid, shadowOid, true /* is_internal */ ,
						RecentXmin, ReadNextMultiXactId());
	forboth(lc, pairedSource, lc2, pairedShadow)
	{
		if (SwapIndexFiles(lfirst_oid(lc), lfirst_oid(lc2)))
			adoptedIndexes = lappend_oid(adoptedIndexes, lfirst_oid(lc));
	}
	CommandCounterIncrement();

	AdoptDocDBTable(sourceOid, OBJECT_TABLE);
	foreach(lc, adoptedIndexes)
		AdoptDocDBTable(lfirst_oid(lc), OBJECT_INDEX);

	if (yb_osc_keep_old_table)
	{
		char		oldName[NAMEDATALEN];

		snprintf(oldName, sizeof(oldName), "yb_osc_old_%u", sourceOid);
		RenameRelationInternal(shadowOid, oldName, true /* is_internal */ ,
							   false /* is_index */ );
		YBCRename(shadowOid, OBJECT_TABLE, oldName, NULL /* colname */ );
	}
	else
	{
		/*
		 * As finish_heap_swap drops the transient heap.  The transform and
		 * the capture triggers are already gone, so DROP_RESTRICT suffices.
		 */
		ObjectAddress object;

		ObjectAddressSet(object, RelationRelationId, shadowOid);
		performDeletion(&object, DROP_RESTRICT, PERFORM_DELETION_INTERNAL);
	}

	CacheInvalidateRelcacheByRelid(sourceOid);
	foreach(lc, pairedSource)
		CacheInvalidateRelcacheByRelid(lfirst_oid(lc));
	/* Have the adopted table's pggate cache entry dropped after the DDL. */
	YbTrackAlteredTableId(sourceOid);
	CommandCounterIncrement();
}
