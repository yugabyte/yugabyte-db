/*--------------------------------------------------------------------------------------------------
 *
 * yb_osc_validate.c
 *    Checks that yb_begin_mirror_table runs before it starts an online schema
 *    change job.
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file
 * except in compliance with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * IDENTIFICATION
 *    src/backend/yb_online_schema_change/yb_osc_validate.c
 *--------------------------------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/genam.h"
#include "access/htup_details.h"
#include "access/sysattr.h"
#include "access/table.h"
#include "access/tableam.h"
#include "access/transam.h"
#include "catalog/dependency.h"
#include "catalog/pg_attribute.h"
#include "catalog/pg_constraint.h"
#include "catalog/pg_inherits.h"
#include "catalog/pg_proc.h"
#include "catalog/pg_type.h"
#include "commands/proclang.h"
#include "executor/tuptable.h"
#include "miscadmin.h"
#include "nodes/nodeFuncs.h"
#include "optimizer/clauses.h"
#include "optimizer/optimizer.h"
#include "pg_yb_utils.h"
#include "utils/acl.h"
#include "utils/array.h"
#include "utils/builtins.h"
#include "utils/fmgroids.h"
#include "utils/guc.h"
#include "utils/lsyscache.h"
#include "utils/regproc.h"
#include "utils/relcache.h"
#include "utils/snapmgr.h"
#include "utils/syscache.h"
#include "yb/yql/pggate/ybc_pggate.h"
#include "yb_osc_internal.h"


/*
 * The cutover swaps the table names, and a rename cannot move a relation to
 * another schema.
 */
static void
EnsureSameSchema(Relation srcRel, Relation dstRel)
{
	if (RelationGetNamespace(srcRel) != RelationGetNamespace(dstRel))
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("source and target tables must be in the same schema")));
}

/*
 * pg_inherits names tables by OID, so the cutover rename would leave the
 * renamed table in the hierarchy while its replacement stands outside it.
 * DML addressed to an ancestor would also fire no statement trigger on rel,
 * and the copy would read an inheritance parent together with its children.
 */
static void
EnsureNotInHierarchy(Relation rel, bool isSource)
{
	const char *role = isSource ? "source" : "target";

	if (rel->rd_rel->relispartition)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("%s table \"%s\" is a partition", role,
						RelationGetRelationName(rel))));

	if (has_superclass(RelationGetRelid(rel)))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("%s table \"%s\" inherits from another table", role,
						RelationGetRelationName(rel)),
				 errdetail("Online schema change of tables that use table inheritance is not supported.")));

	if (find_inheritance_children(RelationGetRelid(rel), NoLock) != NIL)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("%s table \"%s\" has inheritance children", role,
						RelationGetRelationName(rel)),
				 errdetail("Online schema change of tables that use table inheritance is not supported.")));
}

/*
 * Require a key witness: a unique index on the source whose key columns are a
 * subset of the target's primary key, or one on the target that covers the
 * source's primary key.  Distinct source rows must reach distinct target keys:
 * the copy writes the target's rows blindly, so two source rows with the same
 * target key would collapse into one, and a DELETE of either would tombstone
 * the row that stands for the other.  See YbOscGetKeyWitnessAttnums.
 *
 * The two primary keys need not match, and the sharding schemes may differ.
 */
static void
ValidateKeyWitness(Relation srcRel, Relation dstRel)
{
	(void) YbOscGetKeyWitnessAttnums(srcRel, dstRel);
}

/* Whether the function's SET clauses include search_path. */
static bool
FunctionSetsSearchPath(HeapTuple proctup)
{
	bool		isnull;
	Datum		proconfig = SysCacheGetAttr(PROCOID, proctup,
											Anum_pg_proc_proconfig, &isnull);

	if (isnull)
		return false;

	Datum	   *items;
	int			nitems;

	deconstruct_array(DatumGetArrayTypeP(proconfig), TEXTOID, -1, false,
					  TYPALIGN_INT, &items, NULL, &nitems);
	for (int i = 0; i < nitems; i++)
	{
		if (strncmp(TextDatumGetCString(items[i]), "search_path=",
					strlen("search_path=")) == 0)
			return true;
	}
	return false;
}

/*
 * Check that the transform is an IMMUTABLE PL/pgSQL function from the source
 * row type to the target row type that sets its own search_path.
 *
 * IMMUTABLE, because the trigger derives the key of a target row, and of each
 * of its index entries, from transform(OLD) to tombstone them.  If that value
 * differs from the one written earlier, the tombstone misses: the row
 * survives a DELETE, or an index entry is orphaned.  STABLE is not enough,
 * because the copy and the trigger call the transform in different
 * statements, possibly hours apart.  The function is opaque, so this applies
 * to every column, including ones the online schema change never recomputes.
 *
 * search_path, because the copy runs with search_path pinned to pg_catalog,
 * pg_temp and the trigger with the session's, and PL/pgSQL resolves the names
 * in the function body when it runs.
 */
static YbOscTransform *
ValidateTransformFn(Oid transformFnOid, Relation srcRel, Relation dstRel)
{
	HeapTuple	proctup = SearchSysCache1(PROCOID, ObjectIdGetDatum(transformFnOid));

	if (!HeapTupleIsValid(proctup))
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_FUNCTION),
				 errmsg("transform function with OID %u does not exist", transformFnOid)));

	Form_pg_proc procform = (Form_pg_proc) GETSTRUCT(proctup);

	if (procform->prolang != get_language_oid("plpgsql", false /* missing_ok */ ))
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("transform function \"%s\" must be a PL/pgSQL function",
						NameStr(procform->proname)),
				 errhint("Declare it with LANGUAGE plpgsql.")));

	if (procform->pronargs != 1)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("transform function must take exactly one argument")));

	Oid			srcType = srcRel->rd_rel->reltype;
	Oid			dstType = dstRel->rd_rel->reltype;
	Oid			argType = procform->proargtypes.values[0];

	if (argType != srcType)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("transform function argument type must be \"%s\"",
						RelationGetRelationName(srcRel))));

	if (procform->prorettype != dstType)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("transform function return type must be \"%s\"",
						RelationGetRelationName(dstRel))));

	if (procform->provolatile != PROVOLATILE_IMMUTABLE)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("transform function \"%s\" must be IMMUTABLE",
						NameStr(procform->proname)),
				 errdetail("The change-capture trigger recomputes the target's primary key "
						   "and index entries from the old row to tombstone them, so the "
						   "transform must return the same row whenever it is called on the "
						   "same source row.")));

	if (!FunctionSetsSearchPath(proctup))
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("transform function \"%s\" must set search_path",
						NameStr(procform->proname)),
				 errdetail("The copy and the change-capture trigger run the transform with "
						   "different search_path settings, so a name in its body could "
						   "resolve to different objects."),
				 errhint("Add SET search_path FROM CURRENT, or SET search_path = <schemas>, "
						 "to the function.")));

	ReleaseSysCache(proctup);

	return YbOscCompileTransform(transformFnOid, srcRel, dstRel);
}

/*
 * The columns some index of dstRel covers.  *allIndexed is set if an index
 * covers the whole row.
 */
static Bitmapset *
IndexedTargetColumns(Relation dstRel, bool *allIndexed)
{
	Bitmapset  *indexed = RelationGetIndexAttrBitmap(dstRel, INDEX_ATTR_BITMAP_ALL);
	int			offset = YBGetFirstLowInvalidAttributeNumber(dstRel);
	Bitmapset  *result = NULL;
	int			bit = -1;

	*allIndexed = false;
	while ((bit = bms_next_member(indexed, bit)) >= 0)
	{
		AttrNumber	attnum = bit + offset;

		if (attnum == InvalidAttrNumber)
			*allIndexed = true;
		else if (attnum > 0)
			result = bms_add_member(result, attnum);
	}
	bms_free(indexed);
	return result;
}

/* Built-in functions are the ones initdb creates from pg_proc.dat. */
static bool
NotBuiltinChecker(Oid funcId, void *context)
{
	if (funcId < FirstGenbkiObjectId)
		return false;
	*(Oid *) context = funcId;
	return true;
}

/* Sets *(Oid *) context to the first function that is not built in. */
static bool
ContainNotBuiltinFunctionsWalker(Node *node, void *context)
{
	if (node == NULL)
		return false;
	if (check_functions_in_node(node, NotBuiltinChecker, context))
		return true;
	return expression_tree_walker(node, ContainNotBuiltinFunctionsWalker,
								  context);
}

static bool
StableFunctionChecker(Oid funcId, void *context)
{
	return func_volatile(funcId) == PROVOLATILE_STABLE;
}

static bool
ContainStableFunctionsWalker(Node *node, void *context)
{
	if (node == NULL)
		return false;
	if (check_functions_in_node(node, StableFunctionChecker, context))
		return true;
	/* All variants of SQLValueFunction are stable. */
	if (IsA(node, SQLValueFunction))
		return true;
	return expression_tree_walker(node, ContainStableFunctionsWalker, context);
}

static bool
SequenceFunctionChecker(Oid funcId, void *context)
{
	return (funcId == F_NEXTVAL ||
			funcId == F_CURRVAL ||
			funcId == F_LASTVAL ||
			funcId == F_SETVAL_REGCLASS_INT8 ||
			funcId == F_SETVAL_REGCLASS_INT8_BOOL);
}

static bool
ContainSequenceFunctionsWalker(Node *node, void *context)
{
	if (node == NULL)
		return false;
	if (IsA(node, NextValueExpr) ||
		check_functions_in_node(node, SequenceFunctionChecker, context))
		return true;
	return expression_tree_walker(node, ContainSequenceFunctionsWalker, context);
}

/* yb_plpgsql_single_return_expr, in the plpgsql module */
typedef Node *(*YbOscSingleReturnExprFn) (Oid fnOid, const char **why);

typedef struct YbOscTransformCheck
{
	const char *fnName;
} YbOscTransformCheck;

static void
pg_attribute_noreturn()
TransformShapeNotSupported(const YbOscTransformCheck *chk, const char *why)
{
	ereport(ERROR,
			(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
			 errmsg("transform function %s is not supported",
					chk->fnName),
			 errdetail("%s", why),
			 errhint("Write the body as BEGIN RETURN ROW(<one expression per target "
					 "column>)::<target>; END.")));
}

/* Report a column the transform computes in a way it must not. */
static void
pg_attribute_noreturn()
TransformColumnNotSupported(const char *msg, const char *detail,
							const char *hint)
{
	ereport(ERROR,
			(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
			 errmsg("%s", msg),
			 errdetail("%s", detail),
			 hint ? errhint("%s", hint) : 0));
}

/*
 * Check the expression the transform computes target column attname with.
 *
 * The change-capture trigger tombstones a row's key and index entries as
 * computed from transform(OLD), so it must compute what the copy or an
 * earlier replay wrote.  Hence no subquery, whose result depends on when it
 * runs, no stable expression, which the copy and each DML session would
 * evaluate at their own time and under their own settings, and no function
 * that is not built in, since a user-defined function labeled IMMUTABLE may
 * not be.  A volatile expression, such as gen_random_uuid(), is allowed for a
 * column no index covers: such a column is only ever written as a value and
 * the newest write of the row wins, so a fresh value on each replay is not
 * observable.  Sequence functions are refused even there, since each replay
 * and each re-copied chunk would consume a value.
 */
static void
CheckTransformColumn(const YbOscTransformCheck *chk, Node *expr,
					 const char *attname, bool indexed)
{
	Oid			notBuiltin = InvalidOid;

	if (contain_subplans(expr))
		TransformColumnNotSupported(psprintf("transform function %s computes column \"%s\" with a subquery",
											 chk->fnName, attname),
									"Its result depends on when it runs, so the change-capture "
									"trigger could compute a different row than the copy wrote.",
									NULL);

	if (ContainNotBuiltinFunctionsWalker(expr, &notBuiltin))
		TransformColumnNotSupported(psprintf("transform function %s computes column \"%s\" with a user-defined function",
											 chk->fnName, attname),
									psprintf("Function %s is not built in.  Only built-in "
											 "functions are allowed, since a user-defined "
											 "function declared IMMUTABLE might not be.",
											 format_procedure(notBuiltin)),
									NULL);

	if (ContainSequenceFunctionsWalker(expr, NULL))
		TransformColumnNotSupported(psprintf("transform function %s computes column \"%s\" with a sequence function",
											 chk->fnName, attname),
									"The copy and the change-capture triggers can evaluate the "
									"expression more than once for a row, and each evaluation "
									"consumes a sequence value.",
									NULL);

	if (ContainStableFunctionsWalker(expr, NULL))
		TransformColumnNotSupported(psprintf("transform function %s computes column \"%s\" with a stable expression",
											 chk->fnName, attname),
									"The copy and the change-capture triggers would evaluate it at "
									"different times and under different settings.",
									NULL);

	if (indexed && contain_volatile_functions(expr))
		TransformColumnNotSupported(psprintf("transform function %s computes indexed column \"%s\" with a volatile expression",
											 chk->fnName, attname),
									"The change-capture triggers find a row's index entries by "
									"recomputing its values, so an indexed column must get the same "
									"value each time.",
									"Create the index after the online schema change.");
}

/*
 * Check the transform's body, once, when the job begins; the transform cannot
 * change while the job runs (YbOscCheckTransformChange).
 *
 * The body must be exactly BEGIN RETURN ROW(<one expression per target
 * column>)::<target>; END.  A DECLARE section's initializers, loops, exception
 * handlers and lookups into other tables are refused, and so the copy and the
 * change-capture trigger need no per-row check of what the transform returns.
 * Each key-witness column (YbOscGetKeyWitnessAttnums) must be its source
 * column, unchanged: that is what makes the witness prove that distinct source
 * rows reach distinct target rows.  CheckTransformColumn checks the other
 * columns.
 *
 * PL/pgSQL resolves names in the body when the function first runs, so they
 * are resolved here under the function's own SET clauses, as a call would.
 * ValidateTransformFn has checked that these set search_path.
 */
static void
CheckTransformBody(Oid transformFnOid, Relation srcRel, Relation dstRel)
{
	YbOscSingleReturnExprFn singleReturnExpr = (YbOscSingleReturnExprFn)
		load_external_function("$libdir/plpgsql",
							   "yb_plpgsql_single_return_expr",
							   true /* signalNotFound */ , NULL);
	YbOscTransformCheck chk = {
		.fnName = format_procedure(transformFnOid),
	};
	TupleDesc	srcDesc = RelationGetDescr(srcRel);
	TupleDesc	dstDesc = RelationGetDescr(dstRel);
	HeapTuple	proctup;
	Datum		proconfig;
	bool		isnull;
	int			gucNestLevel;
	const char *why = NULL;
	Node	   *expr;
	RowExpr    *row;
	List	   *witness;
	Bitmapset  *indexed;
	bool		allIndexed;
	ListCell   *lc;
	int			i;

	proctup = SearchSysCache1(PROCOID, ObjectIdGetDatum(transformFnOid));
	if (!HeapTupleIsValid(proctup))
		elog(ERROR, "cache lookup failed for function %u", transformFnOid);
	proconfig = SysCacheGetAttr(PROCOID, proctup, Anum_pg_proc_proconfig,
								&isnull);

	gucNestLevel = NewGUCNestLevel();
	if (!isnull)
		ProcessGUCArray(DatumGetArrayTypeP(proconfig),
						superuser() ? PGC_SUSET : PGC_USERSET,
						PGC_S_SESSION, GUC_ACTION_SAVE);
	expr = singleReturnExpr(transformFnOid, &why);
	AtEOXact_GUC(true, gucNestLevel);
	ReleaseSysCache(proctup);

	if (expr == NULL)
		TransformShapeNotSupported(&chk, why);

	if (!IsA(expr, RowExpr) ||
		((RowExpr *) expr)->row_typeid != dstRel->rd_rel->reltype ||
		list_length(((RowExpr *) expr)->args) != dstDesc->natts)
		TransformShapeNotSupported(&chk,
								   psprintf("Its RETURN statement must return ROW(...)::%s.",
											format_type_be(dstRel->rd_rel->reltype)));
	row = (RowExpr *) expr;

	witness = YbOscGetKeyWitnessAttnums(srcRel, dstRel);
	indexed = IndexedTargetColumns(dstRel, &allIndexed);

	i = 0;
	foreach(lc, row->args)
	{
		Form_pg_attribute att = TupleDescAttr(dstDesc, i++);
		Node	   *colExpr = (Node *) lfirst(lc);
		const char *attname = NameStr(att->attname);
		AttrNumber	srcAttnum = InvalidAttrNumber;
		ListCell   *wlc;

		if (att->attisdropped)
			continue;

		foreach(wlc, witness)
		{
			if (strcmp(NameStr(TupleDescAttr(srcDesc, lfirst_int(wlc) - 1)->attname),
					   attname) == 0)
				srcAttnum = lfirst_int(wlc);
		}

		if (srcAttnum != InvalidAttrNumber &&
			!(IsA(colExpr, Var) && ((Var *) colExpr)->varattno == srcAttnum))
			TransformColumnNotSupported(psprintf("transform function %s must pass key column \"%s\" through unchanged",
												 chk.fnName, attname),
										"The online schema change relies on a unique index over that "
										"column to prove that distinct source rows reach distinct "
										"target rows, which only holds while the column is copied "
										"unchanged.",
										psprintf("Return the argument's field \"%s\" as is.",
												 attname));

		CheckTransformColumn(&chk, colExpr, attname,
							 allIndexed || bms_is_member(att->attnum, indexed));
	}
}

/*
 * The target must have no foreign keys while the copy runs.  An RI check on
 * the copy's INSERT ... SELECT would run a nested statement, which
 * ybOscRejectNestedTableAccess rejects, and the parent-side actions of a
 * foreign key would change the target outside the capture trigger.
 * yb_wait_mirror_table adds the source's once the copy is done.
 */
static void
EnsureDstNoForeignKeys(Relation dstRel)
{
	if (RelationGetFKeyList(dstRel) != NIL)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("target table \"%s\" of an online schema change must not have foreign keys",
						RelationGetRelationName(dstRel)),
				 errhint("The source table's foreign keys are copied to the target "
						 "by yb_wait_mirror_table; drop them from the target and let the online "
						 "schema change install them.")));
}

/*
 * Reject a stored generated column or an identity column on the target.  The
 * trigger writes rows through YbExecInsertRow, which does not compute
 * generated columns.  The copy's INSERT ... SELECT names every column, which
 * the rewriter rejects for generated and GENERATED ALWAYS identity columns
 * (rewriteTargetListIU), but only once the copy runs, possibly hours into the
 * job.  A GENERATED BY DEFAULT identity column is rejected too, for the reason
 * given at EnsureDstNoOwnedSequences.
 */
static void
EnsureDstNoGeneratedColumns(Relation dstRel)
{
	TupleDesc	dstDesc = RelationGetDescr(dstRel);

	for (int i = 0; i < dstDesc->natts; i++)
	{
		Form_pg_attribute att = TupleDescAttr(dstDesc, i);

		if (att->attisdropped)
			continue;

		if (att->attgenerated)
			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					 errmsg("target table \"%s\" of an online schema change must not have generated columns",
							RelationGetRelationName(dstRel)),
					 errdetail("Column \"%s\" is a generated column.",
							   NameStr(att->attname))));

		if (att->attidentity)
			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					 errmsg("target table \"%s\" of an online schema change must not have identity columns",
							RelationGetRelationName(dstRel)),
					 errdetail("Column \"%s\" is an identity column.",
							   NameStr(att->attname)),
					 errhint("Give the column a DEFAULT that calls nextval() on the "
							 "source table's sequence instead.")));
	}
}

/*
 * Reject a target that owns a sequence, such as a serial column's.  The copy
 * and the trigger write explicit values and never advance it, so after cutover
 * its next value would repeat a key that is already in the table.
 */
static void
EnsureDstNoOwnedSequences(Relation dstRel)
{
	List	   *seqs = getOwnedSequences(RelationGetRelid(dstRel));

	if (seqs != NIL)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("target table \"%s\" of an online schema change must not own a sequence",
						RelationGetRelationName(dstRel)),
				 errdetail("Sequence \"%s\" is owned by the target table.",
						   get_rel_name(linitial_oid(seqs))),
				 errhint("Give the column a DEFAULT that calls nextval() on the "
						 "source table's sequence instead.")));
}

/*
 * Reject a source that is referenced by a foreign key, including a
 * self-reference.  The constraint refers to the source by OID, so after
 * cutover it would still enforce against the old source.
 */
static void
EnsureSrcNoIncomingForeignKeys(Relation srcRel)
{
	ScanKeyData key[2];

	ScanKeyInit(&key[0], Anum_pg_constraint_confrelid, BTEqualStrategyNumber,
				F_OIDEQ, ObjectIdGetDatum(RelationGetRelid(srcRel)));
	ScanKeyInit(&key[1], Anum_pg_constraint_contype, BTEqualStrategyNumber,
				F_CHAREQ, CharGetDatum(CONSTRAINT_FOREIGN));

	Relation	pgConstraint = table_open(ConstraintRelationId, AccessShareLock);

	/* No index on confrelid; this runs once per job, so a seqscan is fine. */
	SysScanDesc scan = systable_beginscan(pgConstraint, InvalidOid /* no index */ ,
										  false /* indexOK */ , NULL /* snapshot */ ,
										  2 /* nkeys */ , key);
	HeapTuple	tuple = systable_getnext(scan);

	if (HeapTupleIsValid(tuple))
	{
		Form_pg_constraint con = (Form_pg_constraint) GETSTRUCT(tuple);
		char	   *conname = pstrdup(NameStr(con->conname));
		char	   *referencing = get_rel_name(con->conrelid);

		systable_endscan(scan);
		table_close(pgConstraint, AccessShareLock);

		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("source table \"%s\" of an online schema change must not be referenced by foreign keys",
						RelationGetRelationName(srcRel)),
				 errdetail("Constraint \"%s\" on table \"%s\" references it.",
						   conname, referencing ? referencing : "?")));
	}

	systable_endscan(scan);
	table_close(pgConstraint, AccessShareLock);
}

/*
 * yb_wait_mirror_table recreates each source foreign key on the target under
 * its own name, so every source column in a foreign key must exist on the
 * target with the same type, and the name must be free on the target.
 * Otherwise it would fail only after the copy.  The target has no foreign keys
 * (EnsureDstNoForeignKeys), so any constraint of that name clashes.
 */
static void
EnsureForeignKeyColumnsOnTarget(Relation srcRel, Relation dstRel)
{
	Oid			dstOid = RelationGetRelid(dstRel);
	ListCell   *lc;

	foreach(lc, RelationGetFKeyList(srcRel))
	{
		ForeignKeyCacheInfo *fk = (ForeignKeyCacheInfo *) lfirst(lc);
		char	   *conname = get_constraint_name(fk->conoid);

		if (conname &&
			ConstraintNameIsUsed(CONSTRAINT_RELATION, dstOid, conname))
			ereport(ERROR,
					(errcode(ERRCODE_DUPLICATE_OBJECT),
					 errmsg("target table \"%s\" already has a constraint named \"%s\"",
							RelationGetRelationName(dstRel), conname),
					 errdetail("The source's foreign key \"%s\" is recreated on the target "
							   "under the same name.", conname),
					 errhint("Rename or drop the target's constraint first.")));

		for (int i = 0; i < fk->nkeys; i++)
		{
			Form_pg_attribute srcAttr =
				TupleDescAttr(RelationGetDescr(srcRel), fk->conkey[i] - 1);
			const char *attname = NameStr(srcAttr->attname);
			AttrNumber	dstAttnum = get_attnum(dstOid, attname);

			if (dstAttnum == InvalidAttrNumber)
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
						 errmsg("target table \"%s\" is missing foreign-key column \"%s\"",
								RelationGetRelationName(dstRel), attname),
						 errdetail("Column \"%s\" of the source is constrained by a foreign key, "
								   "which the online schema change must recreate on the target.",
								   attname)));

			if (get_atttype(dstOid, dstAttnum) != srcAttr->atttypid)
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
						 errmsg("foreign-key column \"%s\" must have the same type on the "
								"source and target tables", attname)));
		}
	}
}

/*
 * The copy reads the source and writes the target as the owner of the
 * transform (see yb_backfill_online_schema_change_chunk).  The caller must
 * have that owner's privileges, so that the job cannot run code with
 * privileges the caller lacks, and the owner must be allowed to read and
 * write the tables.  Checked here so that the job fails before it starts
 * rather than on its first chunk.
 */
static void
EnsureTransformOwnerCanCopy(Oid transformFnOid, Relation srcRel, Relation dstRel)
{
	Oid			owner = YbOscTransformOwner(transformFnOid);
	char	   *fnName = get_func_name(transformFnOid);

	if (!has_privs_of_role(GetUserId(), owner))
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("must be owner of transform function %s", fnName),
				 errdetail("The copy runs the transform with the privileges of its owner.")));

	if (pg_class_aclcheck(RelationGetRelid(srcRel), owner, ACL_SELECT) != ACLCHECK_OK)
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("owner of transform function %s cannot read table \"%s\"",
						fnName, RelationGetRelationName(srcRel)),
				 errdetail("The copy reads the source as the owner of the transform."),
				 errhint("Grant SELECT on table \"%s\" to role \"%s\".",
						 RelationGetRelationName(srcRel),
						 GetUserNameFromId(owner, false))));

	if (pg_class_aclcheck(RelationGetRelid(dstRel), owner,
						  ACL_INSERT) != ACLCHECK_OK)
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("owner of transform function %s cannot insert into target table \"%s\"",
						fnName, RelationGetRelationName(dstRel)),
				 errdetail("The copy writes the target as the owner of the transform."),
				 errhint("Grant INSERT on table \"%s\" to role \"%s\".",
						 RelationGetRelationName(dstRel),
						 GetUserNameFromId(owner, false))));
}

/*
 * The copy writes blindly and nothing deletes from the target, so a row that
 * is already there and has no counterpart in the source would survive the
 * cutover as live data.  yb_begin_mirror_table holds ExclusiveLock on the
 * target, so no row can arrive between this probe and the start of the job.
 */
static void
EnsureTargetEmpty(Relation dstRel)
{
	TableScanDesc scan = table_beginscan(dstRel, GetActiveSnapshot(), 0, NULL);
	TupleTableSlot *slot = table_slot_create(dstRel, NULL);
	bool		hasRow = table_scan_getnextslot(scan, ForwardScanDirection, slot);

	ExecDropSingleTupleTableSlot(slot);
	table_endscan(scan);

	/*
	 * The scan cached the target's DocDB schema, whose version the backfill's
	 * delete-marker pin is about to bump.  A trigger write through the stale
	 * entry would fail with a schema version mismatch.
	 */
	YBCPgAlterTableInvalidateTableByOid(YBCGetDatabaseOid(dstRel),
										YbGetRelfileNodeId(dstRel));

	if (hasRow)
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("target table \"%s\" is not empty",
						RelationGetRelationName(dstRel)),
				 errdetail("Rows already in the target would survive the cutover."),
				 errhint("Truncate the target table first.")));
}

void
YbOscValidate(Relation srcRel, Relation dstRel, Oid transformFnOid)
{
	EnsureSameSchema(srcRel, dstRel);
	EnsureNotInHierarchy(srcRel, true /* isSource */ );
	EnsureNotInHierarchy(dstRel, false /* isSource */ );
	ValidateKeyWitness(srcRel, dstRel);

	(void) ValidateTransformFn(transformFnOid, srcRel, dstRel);
	CheckTransformBody(transformFnOid, srcRel, dstRel);
	EnsureDstNoForeignKeys(dstRel);
	EnsureDstNoGeneratedColumns(dstRel);
	EnsureDstNoOwnedSequences(dstRel);
	EnsureSrcNoIncomingForeignKeys(srcRel);
	EnsureForeignKeyColumnsOnTarget(srcRel, dstRel);
	EnsureTransformOwnerCanCopy(transformFnOid, srcRel, dstRel);
	EnsureTargetEmpty(dstRel);
}
