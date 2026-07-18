/*-------------------------------------------------------------------------
 *
 * copyfuncs.c
 *	  Copy functions for Postgres tree nodes.
 *
 *
 * Portions Copyright (c) 1996-2026, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * IDENTIFICATION
 *	  src/backend/nodes/copyfuncs.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "miscadmin.h"
#include "utils/datum.h"

/* YB includes */
#include "nodes/ybbitmatrix.h"

/*
 * YB_TODO_PG19MERGE: upstream PG commit 964d01ae90c314eb31132c2e7712d5d9fc237331
 * auto-generates node copy functions from struct definitions in header files.
 * All manually-written YB copy functions have been removed in favor of
 * auto-generation. Adding a todo for a more thorough audit of all the changes.
 * Kept YbUpdateAffectedEntities custom copy function due to
 * YbCopyBitMatrix. See also readfuncs.c, outfuncs.c, equalfuncs.c.
 */


/*
 * Macros to simplify copying of different kinds of fields.  Use these
 * wherever possible to reduce the chance for silly typos.  Note that these
 * hard-wire the convention that the local variables in a Copy routine are
 * named 'newnode' and 'from'.
 */

/* Copy a simple scalar field (int, float, bool, enum, etc) */
#define COPY_SCALAR_FIELD(fldname) \
	(newnode->fldname = from->fldname)

/* Copy a field that is a pointer to some kind of Node or Node tree */
#define COPY_NODE_FIELD(fldname) \
	(newnode->fldname = copyObjectImpl(from->fldname))

/* Copy a field that is a pointer to a Bitmapset */
#define COPY_BITMAPSET_FIELD(fldname) \
	(newnode->fldname = bms_copy(from->fldname))

/* Copy a field that is a pointer to a C string, or perhaps NULL */
#define COPY_STRING_FIELD(fldname) \
	(newnode->fldname = from->fldname ? pstrdup(from->fldname) : NULL)

/* Copy a field that is an inline array */
#define COPY_ARRAY_FIELD(fldname) \
	memcpy(newnode->fldname, from->fldname, sizeof(newnode->fldname))

/* Copy a field that is a pointer to a simple palloc'd object of size sz */
#define COPY_POINTER_FIELD(fldname, sz) \
	do { \
		Size	_size = (sz); \
		if (_size > 0) \
		{ \
			newnode->fldname = palloc(_size); \
			memcpy(newnode->fldname, from->fldname, _size); \
		} \
	} while (0)

/* Copy a parse location field (for Copy, this is same as scalar case) */
#define COPY_LOCATION_FIELD(fldname) \
	(newnode->fldname = from->fldname)


#include "copyfuncs.funcs.c"


/*
 * Support functions for nodes with custom_copy_equal attribute
 */

static Const *
_copyConst(const Const *from)
{
	Const	   *newnode = makeNode(Const);

	COPY_SCALAR_FIELD(consttype);
	COPY_SCALAR_FIELD(consttypmod);
	COPY_SCALAR_FIELD(constcollid);
	COPY_SCALAR_FIELD(constlen);

	if (from->constbyval || from->constisnull)
	{
		/*
		 * passed by value so just copy the datum. Also, don't try to copy
		 * struct when value is null!
		 */
		newnode->constvalue = from->constvalue;
	}
	else
	{
		/*
		 * passed by reference.  We need a palloc'd copy.
		 */
		newnode->constvalue = datumCopy(from->constvalue,
										from->constbyval,
										from->constlen);
	}

	COPY_SCALAR_FIELD(constisnull);
	COPY_SCALAR_FIELD(constbyval);
	COPY_LOCATION_FIELD(location);

	return newnode;
}

static A_Const *
_copyA_Const(const A_Const *from)
{
	A_Const    *newnode = makeNode(A_Const);

	COPY_SCALAR_FIELD(isnull);
	if (!from->isnull)
	{
		/* This part must duplicate other _copy*() functions. */
		COPY_SCALAR_FIELD(val.node.type);
		switch (nodeTag(&from->val))
		{
			case T_Integer:
				COPY_SCALAR_FIELD(val.ival.ival);
				break;
			case T_Float:
				COPY_STRING_FIELD(val.fval.fval);
				break;
			case T_Boolean:
				COPY_SCALAR_FIELD(val.boolval.boolval);
				break;
			case T_String:
				COPY_STRING_FIELD(val.sval.sval);
				break;
			case T_BitString:
				COPY_STRING_FIELD(val.bsval.bsval);
				break;
			default:
				elog(ERROR, "unrecognized node type: %d",
					 (int) nodeTag(&from->val));
				break;
		}
	}

	COPY_LOCATION_FIELD(location);

	return newnode;
}

static ExtensibleNode *
_copyExtensibleNode(const ExtensibleNode *from)
{
	ExtensibleNode *newnode;
	const ExtensibleNodeMethods *methods;

	methods = GetExtensibleNodeMethods(from->extnodename, false);
	newnode = (ExtensibleNode *) newNode(methods->node_size,
										 T_ExtensibleNode);
	COPY_STRING_FIELD(extnodename);

	/* copy the private fields */
	methods->nodeCopy(newnode, from);

	return newnode;
}

static Bitmapset *
_copyBitmapset(const Bitmapset *from)
{
	return bms_copy(from);
}

/* YB: custom copy for YbUpdateAffectedEntities due to YbCopyBitMatrix */
static YbUpdateAffectedEntities *
_copyYbUpdateAffectedEntities(const YbUpdateAffectedEntities *from)
{
	YbUpdateAffectedEntities *newnode = makeNode(YbUpdateAffectedEntities);

	COPY_POINTER_FIELD(entity_list, from->matrix.ncols * sizeof(struct YbUpdateEntity));
	COPY_POINTER_FIELD(col_info_list, from->matrix.nrows * sizeof(struct YbUpdateColInfo));
	YbCopyBitMatrix(&newnode->matrix, &from->matrix);

	return newnode;
}

/* YB: custom copy for YbBatchedNestLoop due to pointer arrays needing deep copy */
static YbBatchedNestLoop *
_copyYbBatchedNestLoop(const YbBatchedNestLoop *from)
{
	YbBatchedNestLoop *newnode = makeNode(YbBatchedNestLoop);

	COPY_SCALAR_FIELD(nl.join.plan.disabled_nodes);
	COPY_SCALAR_FIELD(nl.join.plan.startup_cost);
	COPY_SCALAR_FIELD(nl.join.plan.total_cost);
	COPY_SCALAR_FIELD(nl.join.plan.plan_rows);
	COPY_SCALAR_FIELD(nl.join.plan.plan_width);
	COPY_SCALAR_FIELD(nl.join.plan.parallel_aware);
	COPY_SCALAR_FIELD(nl.join.plan.parallel_safe);
	COPY_SCALAR_FIELD(nl.join.plan.async_capable);
	COPY_SCALAR_FIELD(nl.join.plan.plan_node_id);
	COPY_NODE_FIELD(nl.join.plan.targetlist);
	COPY_NODE_FIELD(nl.join.plan.qual);
	COPY_NODE_FIELD(nl.join.plan.lefttree);
	COPY_NODE_FIELD(nl.join.plan.righttree);
	COPY_NODE_FIELD(nl.join.plan.initPlan);
	COPY_BITMAPSET_FIELD(nl.join.plan.extParam);
	COPY_BITMAPSET_FIELD(nl.join.plan.allParam);
	COPY_STRING_FIELD(nl.join.plan.ybHintAlias);
	COPY_SCALAR_FIELD(nl.join.plan.ybUniqueId);
	COPY_STRING_FIELD(nl.join.plan.ybInheritedHintAlias);
	COPY_SCALAR_FIELD(nl.join.plan.ybIsHinted);
	COPY_SCALAR_FIELD(nl.join.plan.ybHasHintedUid);
	COPY_SCALAR_FIELD(nl.join.jointype);
	COPY_SCALAR_FIELD(nl.join.inner_unique);
	COPY_NODE_FIELD(nl.join.joinqual);
	COPY_NODE_FIELD(nl.nestParams);
	COPY_SCALAR_FIELD(num_hashClauseInfos);
	if (from->num_hashClauseInfos > 0)
		COPY_POINTER_FIELD(hashClauseInfos,
						   from->num_hashClauseInfos * sizeof(YbBNLHashClauseInfo));
	for (int i = 0; i < from->num_hashClauseInfos; i++)
		newnode->hashClauseInfos[i].outerParamExpr =
			(Expr *) copyObject(from->hashClauseInfos[i].outerParamExpr);
	for (int i = 0; i < from->num_hashClauseInfos; i++)
		newnode->hashClauseInfos[i].orig_expr =
			(Expr *) copyObject(from->hashClauseInfos[i].orig_expr);
	COPY_SCALAR_FIELD(numSortCols);
	COPY_POINTER_FIELD(sortColIdx, from->numSortCols * sizeof(AttrNumber));
	COPY_POINTER_FIELD(sortOperators, from->numSortCols * sizeof(Oid));
	COPY_POINTER_FIELD(collations, from->numSortCols * sizeof(Oid));
	COPY_POINTER_FIELD(nullsFirst, from->numSortCols * sizeof(bool));

	return newnode;
}

/*
 * copyObjectImpl -- implementation of copyObject(); see nodes/nodes.h
 *
 * Create a copy of a Node tree or list.  This is a "deep" copy: all
 * substructure is copied too, recursively.
 */
void *
copyObjectImpl(const void *from)
{
	void	   *retval;

	if (from == NULL)
		return NULL;

	/* Guard against stack overflow due to overly complex expressions */
	check_stack_depth();

	switch (nodeTag(from))
	{
#include "copyfuncs.switch.c"

		case T_List:
			retval = list_copy_deep(from);
			break;

			/*
			 * Lists of integers, OIDs and XIDs don't need to be deep-copied,
			 * so we perform a shallow copy via list_copy()
			 */
		case T_IntList:
		case T_OidList:
		case T_XidList:
			retval = list_copy(from);
			break;

		default:
			elog(ERROR, "unrecognized node type: %d", (int) nodeTag(from));
			retval = NULL;		/* keep compiler quiet */
			break;
	}

	return retval;
}
