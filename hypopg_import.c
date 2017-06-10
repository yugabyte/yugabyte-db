/*-------------------------------------------------------------------------
 *
 * hypopg_import.c: Import of some PostgreSQL private fuctions.
 *
 * This program is open source, licensed under the PostgreSQL license.
 * For license terms, see the LICENSE file.
 *
 * Copyright (c) 2008-2017, PostgreSQL Global Development Group
 *
 *-------------------------------------------------------------------------
 */


#include "postgres.h"

#if PG_VERSION_NUM >= 90300
#include "access/htup_details.h"
#endif
#include "catalog/heap.h"
#include "catalog/namespace.h"
#include "catalog/pg_opclass.h"
#include "commands/defrem.h"
#if PG_VERSION_NUM < 90500
#include "lib/stringinfo.h"
#endif
#include "nodes/makefuncs.h"
#include "optimizer/clauses.h"
#include "optimizer/planner.h"
#include "parser/parse_coerce.h"
#include "utils/builtins.h"
#include "utils/lsyscache.h"
#include "utils/rel.h"
#include "utils/syscache.h"

#include "hypopg_import.h"


/* Copied from src/backend/optimizer/util/plancat.c, not exported.
 *
 * Build a targetlist representing the columns of the specified index.
 * Each column is represented by a Var for the corresponding base-relation
 * column, or an expression in base-relation Vars, as appropriate.
 *
 * There are never any dropped columns in indexes, so unlike
 * build_physical_tlist, we need no failure case.
 */
List *
build_index_tlist(PlannerInfo *root, IndexOptInfo *index,
				  Relation heapRelation)
{
	List	   *tlist = NIL;
	Index		varno = index->rel->relid;
	ListCell   *indexpr_item;
	int			i;

	indexpr_item = list_head(index->indexprs);
	for (i = 0; i < index->ncolumns; i++)
	{
		int			indexkey = index->indexkeys[i];
		Expr	   *indexvar;

		if (indexkey != 0)
		{
			/* simple column */
			Form_pg_attribute att_tup;

			if (indexkey < 0)
				att_tup = SystemAttributeDefinition(indexkey,
										   heapRelation->rd_rel->relhasoids);
			else
				att_tup = heapRelation->rd_att->attrs[indexkey - 1];

			indexvar = (Expr *) makeVar(varno,
										indexkey,
										att_tup->atttypid,
										att_tup->atttypmod,
										att_tup->attcollation,
										0);
		}
		else
		{
			/* expression column */
			if (indexpr_item == NULL)
				elog(ERROR, "wrong number of index expressions");
			indexvar = (Expr *) lfirst(indexpr_item);
			indexpr_item = lnext(indexpr_item);
		}

		tlist = lappend(tlist,
						makeTargetEntry(indexvar,
										i + 1,
										NULL,
										false));
	}
	if (indexpr_item != NULL)
		elog(ERROR, "wrong number of index expressions");

	return tlist;
}

/*
 * Copied from src/backend/commands/indexcmds.c, not exported.
 * Resolve possibly-defaulted operator class specification
 */
Oid
GetIndexOpClass(List *opclass, Oid attrType,
				char *accessMethodName, Oid accessMethodId)
{
	char	   *schemaname;
	char	   *opcname;
	HeapTuple	tuple;
	Oid			opClassId,
				opInputType;

	/*
	 * Release 7.0 removed network_ops, timespan_ops, and datetime_ops, so we
	 * ignore those opclass names so the default *_ops is used.  This can be
	 * removed in some later release.  bjm 2000/02/07
	 *
	 * Release 7.1 removes lztext_ops, so suppress that too for a while.  tgl
	 * 2000/07/30
	 *
	 * Release 7.2 renames timestamp_ops to timestamptz_ops, so suppress that
	 * too for awhile.  I'm starting to think we need a better approach. tgl
	 * 2000/10/01
	 *
	 * Release 8.0 removes bigbox_ops (which was dead code for a long while
	 * anyway).  tgl 2003/11/11
	 */
	if (list_length(opclass) == 1)
	{
		char	   *claname = strVal(linitial(opclass));

		if (strcmp(claname, "network_ops") == 0 ||
			strcmp(claname, "timespan_ops") == 0 ||
			strcmp(claname, "datetime_ops") == 0 ||
			strcmp(claname, "lztext_ops") == 0 ||
			strcmp(claname, "timestamp_ops") == 0 ||
			strcmp(claname, "bigbox_ops") == 0)
			opclass = NIL;
	}

	if (opclass == NIL)
	{
		/* no operator class specified, so find the default */
		opClassId = GetDefaultOpClass(attrType, accessMethodId);
		if (!OidIsValid(opClassId))
			ereport(ERROR,
					(errcode(ERRCODE_UNDEFINED_OBJECT),
					 errmsg("data type %s has no default operator class for access method \"%s\"",
							format_type_be(attrType), accessMethodName),
					 errhint("You must specify an operator class for the index or define a default operator class for the data type.")));
		return opClassId;
	}

	/*
	 * Specific opclass name given, so look up the opclass.
	 */

	/* deconstruct the name list */
	DeconstructQualifiedName(opclass, &schemaname, &opcname);

	if (schemaname)
	{
		/* Look in specific schema only */
		Oid			namespaceId;

#if PG_VERSION_NUM >= 90300
		namespaceId = LookupExplicitNamespace(schemaname, false);
#else
		namespaceId = LookupExplicitNamespace(schemaname);
#endif
		tuple = SearchSysCache3(CLAAMNAMENSP,
								ObjectIdGetDatum(accessMethodId),
								PointerGetDatum(opcname),
								ObjectIdGetDatum(namespaceId));
	}
	else
	{
		/* Unqualified opclass name, so search the search path */
		opClassId = OpclassnameGetOpcid(accessMethodId, opcname);
		if (!OidIsValid(opClassId))
			ereport(ERROR,
					(errcode(ERRCODE_UNDEFINED_OBJECT),
					 errmsg("operator class \"%s\" does not exist for access method \"%s\"",
							opcname, accessMethodName)));
		tuple = SearchSysCache1(CLAOID, ObjectIdGetDatum(opClassId));
	}

	if (!HeapTupleIsValid(tuple))
	{
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				 errmsg("operator class \"%s\" does not exist for access method \"%s\"",
						NameListToString(opclass), accessMethodName)));
	}

	/*
	 * Verify that the index operator class accepts this datatype.  Note we
	 * will accept binary compatibility.
	 */
	opClassId = HeapTupleGetOid(tuple);
	opInputType = ((Form_pg_opclass) GETSTRUCT(tuple))->opcintype;

	if (!IsBinaryCoercible(attrType, opInputType))
		ereport(ERROR,
				(errcode(ERRCODE_DATATYPE_MISMATCH),
				 errmsg("operator class \"%s\" does not accept data type %s",
					  NameListToString(opclass), format_type_be(attrType))));

	ReleaseSysCache(tuple);

	return opClassId;
}

/*
 * Copied from src/backend/commands/indexcmds.c, not exported.
 * CheckPredicate
 *		Checks that the given partial-index predicate is valid.
 *
 * This used to also constrain the form of the predicate to forms that
 * indxpath.c could do something with.  However, that seems overly
 * restrictive.  One useful application of partial indexes is to apply
 * a UNIQUE constraint across a subset of a table, and in that scenario
 * any evaluatable predicate will work.  So accept any predicate here
 * (except ones requiring a plan), and let indxpath.c fend for itself.
 */
void
CheckPredicate(Expr *predicate)
{
	/*
	 * transformExpr() should have already rejected subqueries, aggregates,
	 * and window functions, based on the EXPR_KIND_ for a predicate.
	 */

	/*
	 * A predicate using mutable functions is probably wrong, for the same
	 * reasons that we don't allow an index expression to use one.
	 */
	if (CheckMutability(predicate))
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
		   errmsg("functions in index predicate must be marked IMMUTABLE")));
}

/*
 * Copied from src/backend/commands/indexcmds.c, not exported.
 * CheckMutability
 *		Test whether given expression is mutable
 */
bool
CheckMutability(Expr *expr)
{
	/*
	 * First run the expression through the planner.  This has a couple of
	 * important consequences.  First, function default arguments will get
	 * inserted, which may affect volatility (consider "default now()").
	 * Second, inline-able functions will get inlined, which may allow us to
	 * conclude that the function is really less volatile than it's marked. As
	 * an example, polymorphic functions must be marked with the most volatile
	 * behavior that they have for any input type, but once we inline the
	 * function we may be able to conclude that it's not so volatile for the
	 * particular input type we're dealing with.
	 *
	 * We assume here that expression_planner() won't scribble on its input.
	 */
	expr = expression_planner(expr);

	/* Now we can search for non-immutable functions */
	return contain_mutable_functions((Node *) expr);
}

#if PG_VERSION_NUM < 90500
/*
 * Copied from src/backend/commands/amcmds.c
 *
 * get_am_name - given an access method OID name and type, look up its name.
 */
char *
get_am_name(Oid amOid)
{
	HeapTuple	tup;
	char	   *result = NULL;

	tup = SearchSysCache1(AMOID, ObjectIdGetDatum(amOid));
	if (HeapTupleIsValid(tup))
	{
		Form_pg_am	amform = (Form_pg_am) GETSTRUCT(tup);

		result = pstrdup(NameStr(amform->amname));
		ReleaseSysCache(tup);
	}
	return result;
}
#endif


/*
 * Copied from src/backend/utils/adt/ruleutils.c, not exported.
 *
 * get_opclass_name			- fetch name of an index operator class
 *
 * The opclass name is appended (after a space) to buf.
 *
 * Output is suppressed if the opclass is the default for the given
 * actual_datatype.  (If you don't want this behavior, just pass
 * InvalidOid for actual_datatype.)
 */
void
get_opclass_name(Oid opclass, Oid actual_datatype,
				 StringInfo buf)
{
	HeapTuple	ht_opc;
	Form_pg_opclass opcrec;
	char	   *opcname;
	char	   *nspname;

	ht_opc = SearchSysCache1(CLAOID, ObjectIdGetDatum(opclass));
	if (!HeapTupleIsValid(ht_opc))
		elog(ERROR, "cache lookup failed for opclass %u", opclass);
	opcrec = (Form_pg_opclass) GETSTRUCT(ht_opc);

	if (!OidIsValid(actual_datatype) ||
		GetDefaultOpClass(actual_datatype, opcrec->opcmethod) != opclass)
	{
		/* Okay, we need the opclass name.  Do we need to qualify it? */
		opcname = NameStr(opcrec->opcname);
		if (OpclassIsVisible(opclass))
			appendStringInfo(buf, " %s", quote_identifier(opcname));
		else
		{
			nspname = get_namespace_name(opcrec->opcnamespace);
			appendStringInfo(buf, " %s.%s",
							 quote_identifier(nspname),
							 quote_identifier(opcname));
		}
	}
	ReleaseSysCache(ht_opc);
}
