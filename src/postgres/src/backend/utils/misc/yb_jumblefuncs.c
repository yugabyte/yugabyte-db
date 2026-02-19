/*-------------------------------------------------------------------------
 *
 * yb_jumblefuncs.c
 *	  Helper functions for calculating a plan ID through a "jumble".
 *
 * Portions Copyright (c) 1996-2025, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * IDENTIFICATION
 *	  src/backend/utils/misc/yb_jumblefuncs.c
 *
 *-------------------------------------------------------------------------
 */

 /*
  * YB: Source of this file is the file jumblefuncs.c in the pg_stat_plans v2.0.0 extension
  *     (commit 1a86703)
  */
#include "postgres.h"

#include "access/transam.h"
#include "catalog/pg_type.h"
#include "common/hashfn.h"
#include "miscadmin.h"
#include "nodes/extensible.h"
#include "nodes/nodeFuncs.h"
#include "nodes/plannodes.h"
#include "parser/parsetree.h"
#include "parser/scansup.h"
#include "utils/lsyscache.h"
#include "utils/syscache.h"
#include "utils/yb_jumblefuncs.h"

#define JUMBLE_SIZE				1024	/* query serialization buffer size */

static YbJumbleState *InitJumbleInternal(bool record_clocations);
static void AppendJumbleInternal(YbJumbleState *jstate,
								 const unsigned char *value, Size size);
static void FlushPendingNulls(YbJumbleState *jstate);
static void RecordConstLocation(YbJumbleState *jstate,
								int location, bool squashed);
static void _jumbleElements(YbJumbleState *jstate, List *elements);
static void _jumbleA_Const(YbJumbleState *jstate, Node *node);
static void _jumbleList(YbJumbleState *jstate, Node *node);
static void _jumbleVariableSetStmt(YbJumbleState *jstate, Node *node);
static void _jumbleRangeTblEntry_eref(YbJumbleState *jstate,
									  RangeTblEntry *rte,
									  Alias *expr);
static void _jumbleRtIndex(YbJumbleState *jstate, int rtIndex);
static void _jumbleSortColIdx(YbJumbleState *jstate, int numCols, AttrNumber *sortColIdx);
static int	cmpUint64(const void *p1, const void *p2);
static char *get_typname(Oid pg_type_oid);

/*
 * InitJumbleInternal
 *		Allocate a YbJumbleState object and make it ready to jumble.
 */
static YbJumbleState *
InitJumbleInternal(bool record_clocations)
{
	YbJumbleState *jstate;

	jstate = (YbJumbleState *) palloc(sizeof(YbJumbleState));

	/* Set up workspace for query jumbling */
	jstate->jumble = (unsigned char *) palloc(JUMBLE_SIZE);
	jstate->jumble_len = 0;

	if (record_clocations)
	{
		jstate->clocations_buf_size = 32;
		jstate->clocations = (YbLocationLen *)
			palloc(jstate->clocations_buf_size * sizeof(YbLocationLen));
	}
	else
	{
		jstate->clocations_buf_size = 0;
		jstate->clocations = NULL;
	}

	jstate->clocations_count = 0;
	jstate->highest_extern_param_id = 0;
	jstate->pending_nulls = 0;
#ifdef USE_ASSERT_CHECKING
	jstate->total_jumble_len = 0;
#endif

	jstate->ybRtIndexMap = NULL;
	jstate->ybTargetList = NIL;
	jstate->ybUseNames = false;

	return jstate;
}

/*
 * Exported initializer for jumble state that allows plugins to hash values and
 * nodes, but does not record constant locations, for now.
 */
YbJumbleState *
YbInitJumble()
{
	return InitJumbleInternal(false);
}

/*
 * Produce a 64-bit hash from a jumble state.
 */
uint64
YbHashJumbleState(YbJumbleState *jstate)
{
	/* Flush any pending NULLs before doing the final hash */
	if (jstate->pending_nulls > 0)
		FlushPendingNulls(jstate);

	/* Process the jumble buffer and produce the hash value */
	return DatumGetUInt64(hash_any_extended(jstate->jumble,
											jstate->jumble_len,
											0));
}

/*
 * AppendJumbleInternal: Internal function for appending to the jumble buffer
 *
 * Note: Callers must ensure that size > 0.
 */
static pg_attribute_always_inline void
AppendJumbleInternal(YbJumbleState *jstate, const unsigned char *item,
					 Size size)
{
	unsigned char *jumble = jstate->jumble;
	Size		jumble_len = jstate->jumble_len;

	/* Ensure the caller didn't mess up */
	Assert(size > 0);

	/*
	 * Fast path for when there's enough space left in the buffer.  This is
	 * worthwhile as means the memcpy can be inlined into very efficient code
	 * when 'size' is a compile-time constant.
	 */
	if (likely(size <= JUMBLE_SIZE - jumble_len))
	{
		memcpy(jumble + jumble_len, item, size);
		jstate->jumble_len += size;

#ifdef USE_ASSERT_CHECKING
		jstate->total_jumble_len += size;
#endif

		return;
	}

	/*
	 * Whenever the jumble buffer is full, we hash the current contents and
	 * reset the buffer to contain just that hash value, thus relying on the
	 * hash to summarize everything so far.
	 */
	do
	{
		Size		part_size;

		if (unlikely(jumble_len >= JUMBLE_SIZE))
		{
			uint64		start_hash;

			start_hash = DatumGetUInt64(hash_any_extended(jumble,
														  JUMBLE_SIZE, 0));
			memcpy(jumble, &start_hash, sizeof(start_hash));
			jumble_len = sizeof(start_hash);
		}
		part_size = Min(size, JUMBLE_SIZE - jumble_len);
		memcpy(jumble + jumble_len, item, part_size);
		jumble_len += part_size;
		item += part_size;
		size -= part_size;

#ifdef USE_ASSERT_CHECKING
		jstate->total_jumble_len += part_size;
#endif
	} while (size > 0);

	jstate->jumble_len = jumble_len;
}

/*
 * AppendJumble
 *		Add 'size' bytes of the given jumble 'value' to the jumble state
 */
static pg_noinline void
AppendJumble(YbJumbleState *jstate, const unsigned char *value, Size size)
{
	if (jstate->pending_nulls > 0)
		FlushPendingNulls(jstate);

	AppendJumbleInternal(jstate, value, size);
}

/*
 * AppendJumbleNull
 *		For jumbling NULL pointers
 */
static pg_attribute_always_inline void
AppendJumbleNull(YbJumbleState *jstate)
{
	jstate->pending_nulls++;
}

/*
 * AppendJumble8
 *		Add the first byte from the given 'value' pointer to the jumble state
 */
static pg_noinline void
AppendJumble8(YbJumbleState *jstate, const unsigned char *value)
{
	if (jstate->pending_nulls > 0)
		FlushPendingNulls(jstate);

	AppendJumbleInternal(jstate, value, 1);
}

/*
 * AppendJumble16
 *		Add the first 2 bytes from the given 'value' pointer to the jumble
 *		state.
 */
static pg_noinline void
AppendJumble16(YbJumbleState *jstate, const unsigned char *value)
{
	if (jstate->pending_nulls > 0)
		FlushPendingNulls(jstate);

	AppendJumbleInternal(jstate, value, 2);
}

/*
 * AppendJumble32
 *		Add the first 4 bytes from the given 'value' pointer to the jumble
 *		state.
 */
static pg_noinline void
AppendJumble32(YbJumbleState *jstate, const unsigned char *value)
{
	if (jstate->pending_nulls > 0)
		FlushPendingNulls(jstate);

	AppendJumbleInternal(jstate, value, 4);
}

/*
 * AppendJumble64
 *		Add the first 8 bytes from the given 'value' pointer to the jumble
 *		state.
 */
static pg_noinline void
AppendJumble64(YbJumbleState *jstate, const unsigned char *value)
{
	if (jstate->pending_nulls > 0)
		FlushPendingNulls(jstate);

	AppendJumbleInternal(jstate, value, 8);
}

/*
 * FlushPendingNulls
 *		Incorporate the pending_nulls value into the jumble buffer.
 *
 * Note: Callers must ensure that there's at least 1 pending NULL.
 */
static pg_attribute_always_inline void
FlushPendingNulls(YbJumbleState *jstate)
{
	Assert(jstate->pending_nulls > 0);

	AppendJumbleInternal(jstate,
						 (const unsigned char *) &jstate->pending_nulls, 4);
	jstate->pending_nulls = 0;
}


/*
 * Record location of constant within query string of query tree that is
 * currently being walked.
 *
 * 'squashed' signals that the constant represents the first or the last
 * element in a series of merged constants, and everything but the first/last
 * element contributes nothing to the jumble hash.
 */
static void
RecordConstLocation(YbJumbleState *jstate, int location, bool squashed)
{
	/* Skip if the caller is a plugin not interested in constant locations */
	if (jstate->clocations == NULL)
		return;

	/* -1 indicates unknown or undefined location */
	if (location >= 0)
	{
		/* enlarge array if needed */
		if (jstate->clocations_count >= jstate->clocations_buf_size)
		{
			jstate->clocations_buf_size *= 2;
			jstate->clocations = (YbLocationLen *)
				repalloc(jstate->clocations,
						 jstate->clocations_buf_size *
						 sizeof(YbLocationLen));
		}
		jstate->clocations[jstate->clocations_count].location = location;
		/* initialize lengths to -1 to simplify third-party module usage */
		jstate->clocations[jstate->clocations_count].squashed = squashed;
		jstate->clocations[jstate->clocations_count].length = -1;
		jstate->clocations_count++;
	}
}

/*
 * Subroutine for _jumbleElements: Verify a few simple cases where we can
 * deduce that the expression is a constant:
 *
 * - Ignore a possible wrapping RelabelType and CoerceViaIO.
 * - If it's a FuncExpr, check that the function is an implicit
 *   cast and its arguments are Const.
 * - Otherwise test if the expression is a simple Const.
 */
static bool
IsSquashableConst(Node *element)
{
	if (IsA(element, RelabelType))
		element = (Node *) ((RelabelType *) element)->arg;

	if (IsA(element, CoerceViaIO))
		element = (Node *) ((CoerceViaIO *) element)->arg;

	if (IsA(element, FuncExpr))
	{
		FuncExpr   *func = (FuncExpr *) element;
		ListCell   *temp;

		if (func->funcformat != COERCE_IMPLICIT_CAST && func->funcformat != COERCE_EXPLICIT_CAST)
			return false;

		if (func->funcid > FirstGenbkiObjectId)
			return false;

		foreach(temp, func->args)
		{
			Node	   *arg = lfirst(temp);

			if (!IsA(arg, Const))	/* XXX we could recurse here instead */
				return false;
		}

		return true;
	}

	if (!IsA(element, Const))
		return false;

	return true;
}

/*
 * Subroutine for _jumbleElements: Verify whether the provided list
 * can be squashed, meaning it contains only constant expressions.
 *
 * Return value indicates if squashing is possible.
 *
 * Note that this function searches only for explicit Const nodes with
 * possibly very simple decorations on top, and does not try to simplify
 * expressions.
 */
static bool
IsSquashableConstList(List *elements, Node **firstExpr, Node **lastExpr)
{
	ListCell   *temp;

	/*
	 * If squashing is disabled, or the list is too short, we don't try to
	 * squash it.
	 */
	if (list_length(elements) < 2)
		return false;

	foreach(temp, elements)
	{
		if (!IsSquashableConst(lfirst(temp)))
			return false;
	}

	*firstExpr = linitial(elements);
	*lastExpr = llast(elements);

	return true;
}

#define JUMBLE_NODE(item) \
	YbJumbleNode(jstate, (Node *) expr->item)
#define JUMBLE_ELEMENTS(list) \
	_jumbleElements(jstate, (List *) expr->list)
#define JUMBLE_LOCATION(location) \
	RecordConstLocation(jstate, expr->location, false)
#define JUMBLE_FIELD(item) \
do { \
	if (sizeof(expr->item) == 8) \
		AppendJumble64(jstate, (const unsigned char *) &(expr->item)); \
	else if (sizeof(expr->item) == 4) \
		AppendJumble32(jstate, (const unsigned char *) &(expr->item)); \
	else if (sizeof(expr->item) == 2) \
		AppendJumble16(jstate, (const unsigned char *) &(expr->item)); \
	else if (sizeof(expr->item) == 1) \
		AppendJumble8(jstate, (const unsigned char *) &(expr->item)); \
	else \
		AppendJumble(jstate, (const unsigned char *) &(expr->item), sizeof(expr->item)); \
} while (0)
#define JUMBLE_STRING(str) \
do { \
	if (expr->str) \
		AppendJumble(jstate, (const unsigned char *) (expr->str), strlen(expr->str) + 1); \
	else \
		AppendJumbleNull(jstate); \
} while(0)
/* Function name used for the node field attribute custom_query_jumble. */
#define JUMBLE_CUSTOM(nodetype, item) \
	_jumble##nodetype##_##item(jstate, expr, expr->item)

#define JUMBLE_BITMAPSET(item) \
do { \
	if (expr->item && expr->item->nwords > 0) \
		AppendJumble(jstate, (const unsigned char *) expr->item->words, sizeof(bitmapword) * expr->item->nwords); \
} while(0)
#define JUMBLE_ARRAY(item, len) \
do { \
	if (len > 0) \
		AppendJumble(jstate, (const unsigned char *) expr->item, sizeof(*(expr->item)) * len); \
} while(0)

static void
_jumbleAlias(YbJumbleState *jstate, Node *node)
{
	Alias	   *expr = (Alias *) node;

	JUMBLE_STRING(aliasname);
	JUMBLE_NODE(colnames);
}

static void
_jumbleRangeVar(YbJumbleState *jstate, Node *node)
{
	RangeVar   *expr = (RangeVar *) node;

	JUMBLE_STRING(catalogname);
	JUMBLE_STRING(schemaname);
	JUMBLE_STRING(relname);
	JUMBLE_FIELD(inh);
	JUMBLE_FIELD(relpersistence);
	JUMBLE_NODE(alias);
}

static void
_jumbleTableFunc(YbJumbleState *jstate, Node *node)
{
	TableFunc  *expr = (TableFunc *) node;

	/* JUMBLE_FIELD(functype); */
	JUMBLE_NODE(docexpr);
	JUMBLE_NODE(rowexpr);
	JUMBLE_NODE(colexprs);
}

static void
_jumbleIntoClause(YbJumbleState *jstate, Node *node)
{
	IntoClause *expr = (IntoClause *) node;

	JUMBLE_NODE(rel);
	JUMBLE_NODE(colNames);
	JUMBLE_STRING(accessMethod);
	JUMBLE_NODE(options);
	JUMBLE_FIELD(onCommit);
	JUMBLE_STRING(tableSpaceName);
	JUMBLE_FIELD(skipData);
}

static void
_jumbleRtIndex(YbJumbleState *jstate, int rtIndex)
{
	int			jumbleVarno;

	if (rtIndex > 0)
		jumbleVarno = jstate->ybRtIndexMap[rtIndex - 1];
	else
		jumbleVarno = 0;

	AppendJumble(jstate, (const unsigned char *) &jumbleVarno, sizeof(int));

	return;
}

static char *
get_typname(Oid pg_type_oid)
{
	HeapTuple	type_tuple = SearchSysCache1(TYPEOID,
											 ObjectIdGetDatum(pg_type_oid));

	if (!HeapTupleIsValid(type_tuple))
	{
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				 errmsg("type OID %u not found", pg_type_oid)));
	}

	Form_pg_type type_form = (Form_pg_type) GETSTRUCT(type_tuple);
	char	   *type_name = pstrdup(NameStr(type_form->typname));

	ReleaseSysCache(type_tuple);
	return type_name;
}

static void
_jumbleVar(YbJumbleState *jstate, Node *node)
{
	Var		   *expr = (Var *) node;

	/* JUMBLE_FIELD(varno); */
	_jumbleRtIndex(jstate, expr->varno);
	/* JUMBLE_FIELD(varattno); */
	_jumbleRtIndex(jstate, expr->varnosyn);
	JUMBLE_FIELD(varattnosyn);
	JUMBLE_FIELD(varlevelsup);
	/* JUMBLE_FIELD(varreturningtype); */

	if (jstate->ybUseNames)
	{
		char	   *typename = get_typname(expr->vartype);

		if (typename != NULL)
			AppendJumble(jstate, (const unsigned char *) typename, strlen(typename));
		else
			AppendJumbleNull(jstate);
	}
	else
		JUMBLE_FIELD(vartype);
}

static void
_jumbleConst(YbJumbleState *jstate, Node *node)
{
	Const	   *expr = (Const *) node;

	JUMBLE_FIELD(consttype);
	JUMBLE_LOCATION(location);

	/*
	 * AppendJumble(jstate, (const unsigned char *) &(expr->constvalue),
	 * expr->constlen);
	 */
}

static void
_jumbleParam(YbJumbleState *jstate, Node *node)
{
	Param	   *expr = (Param *) node;

	JUMBLE_FIELD(paramkind);
	JUMBLE_FIELD(paramid);
	JUMBLE_FIELD(paramtype);
}

static void
_jumbleAggref(YbJumbleState *jstate, Node *node)
{
	Aggref	   *expr = (Aggref *) node;

	JUMBLE_FIELD(aggfnoid);
	JUMBLE_NODE(aggdirectargs);
	JUMBLE_NODE(args);
	JUMBLE_NODE(aggorder);
	JUMBLE_NODE(aggdistinct);
	JUMBLE_NODE(aggfilter);
}

static void
_jumbleGroupingFunc(YbJumbleState *jstate, Node *node)
{
	GroupingFunc *expr = (GroupingFunc *) node;

	JUMBLE_NODE(refs);
	JUMBLE_FIELD(agglevelsup);
}

static void
_jumbleWindowFunc(YbJumbleState *jstate, Node *node)
{
	WindowFunc *expr = (WindowFunc *) node;

	JUMBLE_FIELD(winfnoid);
	JUMBLE_NODE(args);
	JUMBLE_NODE(aggfilter);
	JUMBLE_FIELD(winref);
}

#if 0
static void
_jumbleWindowFuncRunCondition(YbJumbleState *jstate, Node *node)
{
	WindowFuncRunCondition *expr = (WindowFuncRunCondition *) node;

	JUMBLE_FIELD(opno);
	JUMBLE_FIELD(wfunc_left);
	JUMBLE_NODE(arg);
}

static void
_jumbleMergeSupportFunc(YbJumbleState *jstate, Node *node)
{
	MergeSupportFunc *expr = (MergeSupportFunc *) node;

	JUMBLE_FIELD(msftype);
	JUMBLE_FIELD(msfcollid);
}
#endif

static void
_jumbleSubscriptingRef(YbJumbleState *jstate, Node *node)
{
	SubscriptingRef *expr = (SubscriptingRef *) node;

	JUMBLE_NODE(refupperindexpr);
	JUMBLE_NODE(reflowerindexpr);
	JUMBLE_NODE(refexpr);
	JUMBLE_NODE(refassgnexpr);
}

static void
_jumbleFuncExpr(YbJumbleState *jstate, Node *node)
{
	FuncExpr   *expr = (FuncExpr *) node;

	if (jstate->ybUseNames)
	{
		char	   *funcName = get_func_name(expr->funcid);

		if (funcName != NULL)
			AppendJumble(jstate, (const unsigned char *) funcName, strlen(funcName));
		else
			AppendJumbleNull(jstate);
	}
	else
		JUMBLE_FIELD(funcid);

	JUMBLE_NODE(args);
}

static void
_jumbleNamedArgExpr(YbJumbleState *jstate, Node *node)
{
	NamedArgExpr *expr = (NamedArgExpr *) node;

	JUMBLE_NODE(arg);
	JUMBLE_FIELD(argnumber);
}

static void
_jumbleOpExpr(YbJumbleState *jstate, Node *node)
{
	OpExpr	   *expr = (OpExpr *) node;

	JUMBLE_FIELD(opno);

	bool		jumbledArgs = false;

	if (list_length(expr->args) == 2)
	{
		Expr	   *leftarg = linitial(expr->args);

		if (nodeTag(leftarg) == T_Var)
		{
			Oid			left_type = exprType((Node *) leftarg);

			if (op_hashjoinable(expr->opno, left_type))
			{
				Expr	   *rightarg = lsecond(expr->args);

				if (nodeTag(rightarg) == T_Var)
				{
					bool		leftFirst = true;
					Var		   *leftVar = (Var *) leftarg;
					Var		   *rightVar = (Var *) rightarg;

					int			mappedLeftVarno,
								mappedRightVarno;

					if (leftVar->varnosyn > 0)
						mappedLeftVarno = jstate->ybRtIndexMap[leftVar->varnosyn - 1];
					else
						mappedLeftVarno = INT_MAX;

					if (rightVar->varnosyn > 0)
						mappedRightVarno = jstate->ybRtIndexMap[rightVar->varnosyn - 1];
					else
						mappedRightVarno = INT_MAX;

					if (mappedLeftVarno < mappedRightVarno)
						leftFirst = true;
					else if (mappedRightVarno < mappedLeftVarno)
						leftFirst = false;
					else if (leftVar->varattnosyn < rightVar->varattnosyn)
						leftFirst = true;
					else if (rightVar->varattnosyn < leftVar->varattnosyn)
						leftFirst = false;

					if (leftFirst)
					{
						_jumbleVar(jstate, (Node *) leftarg);
						_jumbleVar(jstate, (Node *) rightarg);
					}
					else
					{
						_jumbleVar(jstate, (Node *) rightarg);
						_jumbleVar(jstate, (Node *) leftarg);
					}

					jumbledArgs = true;
				}
			}
		}
	}

	if (!jumbledArgs)
	{
		JUMBLE_NODE(args);
	}
}

static void
_jumbleDistinctExpr(YbJumbleState *jstate, Node *node)
{
	DistinctExpr *expr = (DistinctExpr *) node;

	JUMBLE_FIELD(opno);
	JUMBLE_NODE(args);
}

static void
_jumbleNullIfExpr(YbJumbleState *jstate, Node *node)
{
	NullIfExpr *expr = (NullIfExpr *) node;

	JUMBLE_FIELD(opno);
	JUMBLE_NODE(args);
}

static void
_jumbleScalarArrayOpExpr(YbJumbleState *jstate, Node *node)
{
	ScalarArrayOpExpr *expr = (ScalarArrayOpExpr *) node;

	JUMBLE_FIELD(opno);
	JUMBLE_FIELD(useOr);
	JUMBLE_NODE(args);
}

static void
_jumbleBoolExpr(YbJumbleState *jstate, Node *node)
{
	BoolExpr   *expr = (BoolExpr *) node;

	JUMBLE_FIELD(boolop);
	JUMBLE_NODE(args);
}

static void
_jumbleSubLink(YbJumbleState *jstate, Node *node)
{
	SubLink    *expr = (SubLink *) node;

	JUMBLE_FIELD(subLinkType);
	JUMBLE_FIELD(subLinkId);
	JUMBLE_NODE(testexpr);
	JUMBLE_NODE(subselect);
}

static void
_jumbleSubPlan(YbJumbleState *jstate, Node *node)
{
	SubPlan    *expr = (SubPlan *) node;

	JUMBLE_FIELD(subLinkType);
	JUMBLE_NODE(testexpr);
	JUMBLE_NODE(paramIds);
	JUMBLE_FIELD(plan_id);
	JUMBLE_STRING(plan_name);
	JUMBLE_FIELD(firstColType);
	JUMBLE_FIELD(firstColTypmod);
	JUMBLE_FIELD(firstColCollation);
	JUMBLE_FIELD(useHashTable);
	JUMBLE_FIELD(unknownEqFalse);
	JUMBLE_FIELD(parallel_safe);
	JUMBLE_NODE(setParam);
	JUMBLE_NODE(parParam);
	JUMBLE_NODE(args);
}

static void
_jumbleFieldSelect(YbJumbleState *jstate, Node *node)
{
	FieldSelect *expr = (FieldSelect *) node;

	JUMBLE_NODE(arg);
	JUMBLE_FIELD(fieldnum);
}

static void
_jumbleFieldStore(YbJumbleState *jstate, Node *node)
{
	FieldStore *expr = (FieldStore *) node;

	JUMBLE_NODE(arg);
	JUMBLE_NODE(newvals);
}

static void
_jumbleRelabelType(YbJumbleState *jstate, Node *node)
{
	RelabelType *expr = (RelabelType *) node;

	JUMBLE_NODE(arg);
	JUMBLE_FIELD(resulttype);
}

static void
_jumbleCoerceViaIO(YbJumbleState *jstate, Node *node)
{
	CoerceViaIO *expr = (CoerceViaIO *) node;

	JUMBLE_NODE(arg);
	JUMBLE_FIELD(resulttype);
}

static void
_jumbleArrayCoerceExpr(YbJumbleState *jstate, Node *node)
{
	ArrayCoerceExpr *expr = (ArrayCoerceExpr *) node;

	JUMBLE_NODE(arg);
	JUMBLE_NODE(elemexpr);
	JUMBLE_FIELD(resulttype);
}

static void
_jumbleConvertRowtypeExpr(YbJumbleState *jstate, Node *node)
{
	ConvertRowtypeExpr *expr = (ConvertRowtypeExpr *) node;

	JUMBLE_NODE(arg);
	JUMBLE_FIELD(resulttype);
}

static void
_jumbleCollateExpr(YbJumbleState *jstate, Node *node)
{
	CollateExpr *expr = (CollateExpr *) node;

	JUMBLE_NODE(arg);
	JUMBLE_FIELD(collOid);
}

static void
_jumbleCaseExpr(YbJumbleState *jstate, Node *node)
{
	CaseExpr   *expr = (CaseExpr *) node;

	JUMBLE_NODE(arg);
	JUMBLE_NODE(args);
	JUMBLE_NODE(defresult);
}

static void
_jumbleCaseWhen(YbJumbleState *jstate, Node *node)
{
	CaseWhen   *expr = (CaseWhen *) node;

	JUMBLE_NODE(expr);
	JUMBLE_NODE(result);
}

static void
_jumbleCaseTestExpr(YbJumbleState *jstate, Node *node)
{
	CaseTestExpr *expr = (CaseTestExpr *) node;

	JUMBLE_FIELD(typeId);
}

static void
_jumbleArrayExpr(YbJumbleState *jstate, Node *node)
{
	ArrayExpr  *expr = (ArrayExpr *) node;

	JUMBLE_ELEMENTS(elements);
}

static void
_jumbleRowExpr(YbJumbleState *jstate, Node *node)
{
	RowExpr    *expr = (RowExpr *) node;

	JUMBLE_NODE(args);
}

static void
_jumbleRowCompareExpr(YbJumbleState *jstate, Node *node)
{
	RowCompareExpr *expr = (RowCompareExpr *) node;

	/* JUMBLE_FIELD(cmptype); */
	JUMBLE_FIELD(rctype);
	JUMBLE_NODE(largs);
	JUMBLE_NODE(rargs);
}

static void
_jumbleCoalesceExpr(YbJumbleState *jstate, Node *node)
{
	CoalesceExpr *expr = (CoalesceExpr *) node;

	JUMBLE_NODE(args);
}

static void
_jumbleMinMaxExpr(YbJumbleState *jstate, Node *node)
{
	MinMaxExpr *expr = (MinMaxExpr *) node;

	JUMBLE_FIELD(op);
	JUMBLE_NODE(args);
}

static void
_jumbleSQLValueFunction(YbJumbleState *jstate, Node *node)
{
	SQLValueFunction *expr = (SQLValueFunction *) node;

	JUMBLE_FIELD(op);
	JUMBLE_FIELD(typmod);
}

static void
_jumbleXmlExpr(YbJumbleState *jstate, Node *node)
{
	XmlExpr    *expr = (XmlExpr *) node;

	JUMBLE_FIELD(op);
	JUMBLE_NODE(named_args);
	JUMBLE_NODE(args);
	/* JUMBLE_FIELD(indent); */
}

/*
 * Node types not yet supported in YB (based on PG18).
 */
#if 0
static void
_jumbleJsonFormat(YbJumbleState *jstate, Node *node)
{
	JsonFormat *expr = (JsonFormat *) node;

	JUMBLE_FIELD(format_type);
	JUMBLE_FIELD(encoding);
}

static void
_jumbleJsonReturning(YbJumbleState *jstate, Node *node)
{
	JsonReturning *expr = (JsonReturning *) node;

	JUMBLE_NODE(format);
	JUMBLE_FIELD(typid);
	JUMBLE_FIELD(typmod);
}

static void
_jumbleJsonValueExpr(YbJumbleState *jstate, Node *node)
{
	JsonValueExpr *expr = (JsonValueExpr *) node;

	JUMBLE_NODE(raw_expr);
	JUMBLE_NODE(formatted_expr);
	JUMBLE_NODE(format);
}

static void
_jumbleJsonConstructorExpr(YbJumbleState *jstate, Node *node)
{
	JsonConstructorExpr *expr = (JsonConstructorExpr *) node;

	JUMBLE_FIELD(type);
	JUMBLE_NODE(args);
	JUMBLE_NODE(func);
	JUMBLE_NODE(coercion);
	JUMBLE_NODE(returning);
	JUMBLE_FIELD(absent_on_null);
	JUMBLE_FIELD(unique);
}

static void
_jumbleJsonIsPredicate(YbJumbleState *jstate, Node *node)
{
	JsonIsPredicate *expr = (JsonIsPredicate *) node;

	JUMBLE_NODE(expr);
	JUMBLE_NODE(format);
	JUMBLE_FIELD(item_type);
	JUMBLE_FIELD(unique_keys);
}

static void
_jumbleJsonBehavior(YbJumbleState *jstate, Node *node)
{
	JsonBehavior *expr = (JsonBehavior *) node;

	JUMBLE_FIELD(btype);
	JUMBLE_NODE(expr);
	JUMBLE_FIELD(coerce);
}

static void
_jumbleJsonExpr(YbJumbleState *jstate, Node *node)
{
	JsonExpr   *expr = (JsonExpr *) node;

	JUMBLE_FIELD(op);
	JUMBLE_STRING(column_name);
	JUMBLE_NODE(formatted_expr);
	JUMBLE_NODE(format);
	JUMBLE_NODE(path_spec);
	JUMBLE_NODE(returning);
	JUMBLE_NODE(passing_names);
	JUMBLE_NODE(passing_values);
	JUMBLE_NODE(on_empty);
	JUMBLE_NODE(on_error);
	JUMBLE_FIELD(use_io_coercion);
	JUMBLE_FIELD(use_json_coercion);
	JUMBLE_FIELD(wrapper);
	JUMBLE_FIELD(omit_quotes);
	JUMBLE_FIELD(collation);
}

static void
_jumbleJsonTablePath(YbJumbleState *jstate, Node *node)
{
	JsonTablePath *expr = (JsonTablePath *) node;

	JUMBLE_NODE(value);
	JUMBLE_STRING(name);
}

static void
_jumbleJsonTablePathScan(YbJumbleState *jstate, Node *node)
{
	JsonTablePathScan *expr = (JsonTablePathScan *) node;

	JUMBLE_NODE(path);
	JUMBLE_FIELD(errorOnError);
	JUMBLE_NODE(child);
	JUMBLE_FIELD(colMin);
	JUMBLE_FIELD(colMax);
}

static void
_jumbleJsonTableSiblingJoin(YbJumbleState *jstate, Node *node)
{
	JsonTableSiblingJoin *expr = (JsonTableSiblingJoin *) node;

	JUMBLE_NODE(lplan);
	JUMBLE_NODE(rplan);
}
#endif

static void
_jumbleNullTest(YbJumbleState *jstate, Node *node)
{
	NullTest   *expr = (NullTest *) node;

	JUMBLE_NODE(arg);
	JUMBLE_FIELD(nulltesttype);
}

static void
_jumbleBooleanTest(YbJumbleState *jstate, Node *node)
{
	BooleanTest *expr = (BooleanTest *) node;

	JUMBLE_NODE(arg);
	JUMBLE_FIELD(booltesttype);
}

static void
_jumbleMergeAction(YbJumbleState *jstate, Node *node)
{
	MergeAction *expr = (MergeAction *) node;

	/* JUMBLE_FIELD(matchKind); */
	JUMBLE_FIELD(commandType);
	JUMBLE_NODE(qual);
	JUMBLE_NODE(targetList);
}

static void
_jumbleCoerceToDomain(YbJumbleState *jstate, Node *node)
{
	CoerceToDomain *expr = (CoerceToDomain *) node;

	JUMBLE_NODE(arg);
	JUMBLE_FIELD(resulttype);
}

static void
_jumbleCoerceToDomainValue(YbJumbleState *jstate, Node *node)
{
	CoerceToDomainValue *expr = (CoerceToDomainValue *) node;

	JUMBLE_FIELD(typeId);
}

static void
_jumbleSetToDefault(YbJumbleState *jstate, Node *node)
{
	SetToDefault *expr = (SetToDefault *) node;

	JUMBLE_FIELD(typeId);
}

static void
_jumbleCurrentOfExpr(YbJumbleState *jstate, Node *node)
{
	CurrentOfExpr *expr = (CurrentOfExpr *) node;

	JUMBLE_FIELD(cvarno);
	JUMBLE_STRING(cursor_name);
	JUMBLE_FIELD(cursor_param);
}

static void
_jumbleNextValueExpr(YbJumbleState *jstate, Node *node)
{
	NextValueExpr *expr = (NextValueExpr *) node;

	JUMBLE_FIELD(seqid);
	JUMBLE_FIELD(typeId);
}

static void
_jumbleInferenceElem(YbJumbleState *jstate, Node *node)
{
	InferenceElem *expr = (InferenceElem *) node;

	JUMBLE_NODE(expr);
	JUMBLE_FIELD(infercollid);
	JUMBLE_FIELD(inferopclass);
}

#if 0
static void
_jumbleReturningExpr(YbJumbleState *jstate, Node *node)
{
	ReturningExpr *expr = (ReturningExpr *) node;

	JUMBLE_FIELD(retlevelsup);
	JUMBLE_FIELD(retold);
	JUMBLE_NODE(retexpr);
}
#endif

static int
cmpUint64(const void *p1, const void *p2)
{
	uint64		v1 = *(uint64 *) p1;
	uint64		v2 = *(uint64 *) p2;

	int			cmp;

	if (v1 < v2)
		cmp = -1;
	else if (v1 > v2)
		cmp = 1;
	else
		cmp = 0;

	return cmp;
}

void
YbJumbleList(YbJumbleState *jstate, List *list, bool symmetric)
{
	if (list != NULL)
	{
		if (!symmetric || list_length(list) == 1)
			_jumbleList(jstate, (Node *) list);
		else
		{
			ListCell   *lc;

			uint64	   *hashValueArr = (uint64 *) palloc(list_length(list) * sizeof(uint64));
			YbJumbleState *workingJstate = YbInitJumble();

			workingJstate->ybRtIndexMap = jstate->ybRtIndexMap;
			workingJstate->ybTargetList = jstate->ybTargetList;

			int			cnt = 0;

			foreach(lc, list)
			{
				Node	   *node = (Node *) lfirst(lc);

				YbJumbleNode(workingJstate, node);
				uint64		targetEntryJumble = YbHashJumbleState(workingJstate);

				hashValueArr[cnt] = targetEntryJumble;

				memset(workingJstate->jumble, 0, JUMBLE_SIZE);
				unsigned char *saveJumble = workingJstate->jumble;

				memset(workingJstate, 0, sizeof(YbJumbleState));
				workingJstate->jumble = saveJumble;
				workingJstate->ybRtIndexMap = jstate->ybRtIndexMap;
				workingJstate->ybTargetList = jstate->ybTargetList;

				++cnt;
			}

			qsort(hashValueArr, cnt, sizeof(uint64), cmpUint64);

			int			i;

			for (i = 0; i < cnt; ++i)
				AppendJumble64(jstate, (const unsigned char *) &(hashValueArr[i]));

			pfree(hashValueArr);
			pfree(workingJstate->jumble);
			pfree(workingJstate);
		}
	}
	else
	{
		AppendJumbleNull(jstate);
	}
}

static void
_jumbleTargetEntry(YbJumbleState *jstate, Node *node)
{
	TargetEntry *expr = (TargetEntry *) node;

	JUMBLE_NODE(expr);
	/* JUMBLE_FIELD(resno); */
	JUMBLE_FIELD(ressortgroupref);
}

static void
_jumbleRangeTblRef(YbJumbleState *jstate, Node *node)
{
	RangeTblRef *expr = (RangeTblRef *) node;

	JUMBLE_FIELD(rtindex);
}

static void
_jumbleJoinExpr(YbJumbleState *jstate, Node *node)
{
	JoinExpr   *expr = (JoinExpr *) node;

	JUMBLE_FIELD(jointype);
	JUMBLE_FIELD(isNatural);
	JUMBLE_NODE(larg);
	JUMBLE_NODE(rarg);

	if (nodeTag(expr->quals) == T_List)
		YbJumbleList(jstate, (List *) (expr->quals), true);
	else
		JUMBLE_NODE(quals);

	/* JUMBLE_FIELD(rtindex); */
}

static void
_jumbleFromExpr(YbJumbleState *jstate, Node *node)
{
	FromExpr   *expr = (FromExpr *) node;

	JUMBLE_NODE(fromlist);

	if (nodeTag(expr->quals) == T_List)
		YbJumbleList(jstate, (List *) (expr->quals), true);
	else
		JUMBLE_NODE(quals);
}

static void
_jumbleOnConflictExpr(YbJumbleState *jstate, Node *node)
{
	OnConflictExpr *expr = (OnConflictExpr *) node;

	JUMBLE_FIELD(action);
	JUMBLE_NODE(arbiterElems);
	JUMBLE_NODE(arbiterWhere);
	JUMBLE_FIELD(constraint);
	JUMBLE_NODE(onConflictSet);
	JUMBLE_NODE(onConflictWhere);
	JUMBLE_FIELD(exclRelIndex);
	JUMBLE_NODE(exclRelTlist);
}

static void
_jumbleQuery(YbJumbleState *jstate, Node *node)
{
	Query	   *expr = (Query *) node;

	JUMBLE_FIELD(commandType);
	JUMBLE_NODE(utilityStmt);
	JUMBLE_NODE(cteList);
	JUMBLE_NODE(rtable);
	JUMBLE_NODE(jointree);
	JUMBLE_NODE(mergeActionList);
	/* JUMBLE_NODE(mergeJoinCondition); */
	JUMBLE_NODE(targetList);
	JUMBLE_NODE(onConflict);
	JUMBLE_NODE(returningList);
	JUMBLE_NODE(groupClause);
	JUMBLE_FIELD(groupDistinct);
	JUMBLE_NODE(groupingSets);
	JUMBLE_NODE(havingQual);
	JUMBLE_NODE(windowClause);
	JUMBLE_NODE(distinctClause);
	JUMBLE_NODE(sortClause);
	JUMBLE_NODE(limitOffset);
	JUMBLE_NODE(limitCount);
	JUMBLE_FIELD(limitOption);
	JUMBLE_NODE(rowMarks);
	JUMBLE_NODE(setOperations);
}

static void
_jumbleTypeName(YbJumbleState *jstate, Node *node)
{
	TypeName   *expr = (TypeName *) node;

	JUMBLE_NODE(names);
	JUMBLE_FIELD(typeOid);
	JUMBLE_FIELD(setof);
	JUMBLE_FIELD(pct_type);
	JUMBLE_NODE(typmods);
	JUMBLE_FIELD(typemod);
	JUMBLE_NODE(arrayBounds);
}

static void
_jumbleColumnRef(YbJumbleState *jstate, Node *node)
{
	ColumnRef  *expr = (ColumnRef *) node;

	JUMBLE_NODE(fields);
}

static void
_jumbleParamRef(YbJumbleState *jstate, Node *node)
{
	ParamRef   *expr = (ParamRef *) node;

	JUMBLE_FIELD(number);
}

static void
_jumbleA_Expr(YbJumbleState *jstate, Node *node)
{
	A_Expr	   *expr = (A_Expr *) node;

	JUMBLE_FIELD(kind);
	JUMBLE_NODE(name);
	JUMBLE_NODE(lexpr);
	JUMBLE_NODE(rexpr);
}

static void
_jumbleTypeCast(YbJumbleState *jstate, Node *node)
{
	TypeCast   *expr = (TypeCast *) node;

	JUMBLE_NODE(arg);
	JUMBLE_NODE(typeName);
}

static void
_jumbleCollateClause(YbJumbleState *jstate, Node *node)
{
	CollateClause *expr = (CollateClause *) node;

	JUMBLE_NODE(arg);
	JUMBLE_NODE(collname);
}

static void
_jumbleRoleSpec(YbJumbleState *jstate, Node *node)
{
	RoleSpec   *expr = (RoleSpec *) node;

	JUMBLE_FIELD(roletype);
	JUMBLE_STRING(rolename);
}

static void
_jumbleFuncCall(YbJumbleState *jstate, Node *node)
{
	FuncCall   *expr = (FuncCall *) node;

	JUMBLE_NODE(funcname);
	JUMBLE_NODE(args);
	JUMBLE_NODE(agg_order);
	JUMBLE_NODE(agg_filter);
	JUMBLE_NODE(over);
	JUMBLE_FIELD(agg_within_group);
	JUMBLE_FIELD(agg_star);
	JUMBLE_FIELD(agg_distinct);
	JUMBLE_FIELD(func_variadic);
	JUMBLE_FIELD(funcformat);
}

static void
_jumbleA_Star(YbJumbleState *jstate, Node *node)
{
	A_Star	   *expr = (A_Star *) node;

	(void) expr;
}

static void
_jumbleA_Indices(YbJumbleState *jstate, Node *node)
{
	A_Indices  *expr = (A_Indices *) node;

	JUMBLE_FIELD(is_slice);
	JUMBLE_NODE(lidx);
	JUMBLE_NODE(uidx);
}

static void
_jumbleA_Indirection(YbJumbleState *jstate, Node *node)
{
	A_Indirection *expr = (A_Indirection *) node;

	JUMBLE_NODE(arg);
	JUMBLE_NODE(indirection);
}

static void
_jumbleA_ArrayExpr(YbJumbleState *jstate, Node *node)
{
	A_ArrayExpr *expr = (A_ArrayExpr *) node;

	JUMBLE_NODE(elements);
}

static void
_jumbleResTarget(YbJumbleState *jstate, Node *node)
{
	ResTarget  *expr = (ResTarget *) node;

	JUMBLE_STRING(name);
	JUMBLE_NODE(indirection);
	JUMBLE_NODE(val);
}

static void
_jumbleMultiAssignRef(YbJumbleState *jstate, Node *node)
{
	MultiAssignRef *expr = (MultiAssignRef *) node;

	JUMBLE_NODE(source);
	JUMBLE_FIELD(colno);
	JUMBLE_FIELD(ncolumns);
}

static void
_jumbleSortBy(YbJumbleState *jstate, Node *node)
{
	SortBy	   *expr = (SortBy *) node;

	JUMBLE_NODE(node);
	JUMBLE_FIELD(sortby_dir);
	JUMBLE_FIELD(sortby_nulls);
	JUMBLE_NODE(useOp);
}

static void
_jumbleWindowDef(YbJumbleState *jstate, Node *node)
{
	WindowDef  *expr = (WindowDef *) node;

	JUMBLE_STRING(name);
	JUMBLE_STRING(refname);
	JUMBLE_NODE(partitionClause);
	JUMBLE_NODE(orderClause);
	JUMBLE_FIELD(frameOptions);
	JUMBLE_NODE(startOffset);
	JUMBLE_NODE(endOffset);
}

static void
_jumbleRangeSubselect(YbJumbleState *jstate, Node *node)
{
	RangeSubselect *expr = (RangeSubselect *) node;

	JUMBLE_FIELD(lateral);
	JUMBLE_NODE(subquery);
	JUMBLE_NODE(alias);
}

static void
_jumbleRangeFunction(YbJumbleState *jstate, Node *node)
{
	RangeFunction *expr = (RangeFunction *) node;

	JUMBLE_FIELD(lateral);
	JUMBLE_FIELD(ordinality);
	JUMBLE_FIELD(is_rowsfrom);
	JUMBLE_NODE(functions);
	JUMBLE_NODE(alias);
	JUMBLE_NODE(coldeflist);
}

static void
_jumbleRangeTableFunc(YbJumbleState *jstate, Node *node)
{
	RangeTableFunc *expr = (RangeTableFunc *) node;

	JUMBLE_FIELD(lateral);
	JUMBLE_NODE(docexpr);
	JUMBLE_NODE(rowexpr);
	JUMBLE_NODE(namespaces);
	JUMBLE_NODE(columns);
	JUMBLE_NODE(alias);
}

static void
_jumbleRangeTableFuncCol(YbJumbleState *jstate, Node *node)
{
	RangeTableFuncCol *expr = (RangeTableFuncCol *) node;

	JUMBLE_STRING(colname);
	JUMBLE_NODE(typeName);
	JUMBLE_FIELD(for_ordinality);
	JUMBLE_FIELD(is_not_null);
	JUMBLE_NODE(colexpr);
	JUMBLE_NODE(coldefexpr);
}

static void
_jumbleRangeTableSample(YbJumbleState *jstate, Node *node)
{
	RangeTableSample *expr = (RangeTableSample *) node;

	JUMBLE_NODE(relation);
	JUMBLE_NODE(method);
	JUMBLE_NODE(args);
	JUMBLE_NODE(repeatable);
}

static void
_jumbleColumnDef(YbJumbleState *jstate, Node *node)
{
	ColumnDef  *expr = (ColumnDef *) node;

	JUMBLE_STRING(colname);
	JUMBLE_NODE(typeName);
	JUMBLE_STRING(compression);
	JUMBLE_FIELD(inhcount);
	JUMBLE_FIELD(is_local);
	JUMBLE_FIELD(is_not_null);
	JUMBLE_FIELD(is_from_type);
	JUMBLE_FIELD(storage);
	/* JUMBLE_STRING(storage_name); */
	JUMBLE_NODE(raw_default);
	JUMBLE_NODE(cooked_default);
	JUMBLE_FIELD(identity);
	JUMBLE_NODE(identitySequence);
	JUMBLE_FIELD(generated);
	JUMBLE_NODE(collClause);
	JUMBLE_FIELD(collOid);
	JUMBLE_NODE(constraints);
	JUMBLE_NODE(fdwoptions);
}

static void
_jumbleTableLikeClause(YbJumbleState *jstate, Node *node)
{
	TableLikeClause *expr = (TableLikeClause *) node;

	JUMBLE_NODE(relation);
	JUMBLE_FIELD(options);
	JUMBLE_FIELD(relationOid);
}

static void
_jumbleIndexElem(YbJumbleState *jstate, Node *node)
{
	IndexElem  *expr = (IndexElem *) node;

	JUMBLE_STRING(name);
	JUMBLE_NODE(expr);
	JUMBLE_STRING(indexcolname);
	JUMBLE_NODE(collation);
	JUMBLE_NODE(opclass);
	JUMBLE_NODE(opclassopts);
	JUMBLE_FIELD(ordering);
	JUMBLE_FIELD(nulls_ordering);
}

static void
_jumbleDefElem(YbJumbleState *jstate, Node *node)
{
	DefElem    *expr = (DefElem *) node;

	JUMBLE_STRING(defnamespace);
	JUMBLE_STRING(defname);
	JUMBLE_NODE(arg);
	JUMBLE_FIELD(defaction);
}

static void
_jumbleLockingClause(YbJumbleState *jstate, Node *node)
{
	LockingClause *expr = (LockingClause *) node;

	JUMBLE_NODE(lockedRels);
	JUMBLE_FIELD(strength);
	JUMBLE_FIELD(waitPolicy);
}

static void
_jumbleXmlSerialize(YbJumbleState *jstate, Node *node)
{
	XmlSerialize *expr = (XmlSerialize *) node;

	JUMBLE_FIELD(xmloption);
	JUMBLE_NODE(expr);
	JUMBLE_NODE(typeName);
	/* JUMBLE_FIELD(indent); */
}

static void
_jumblePartitionElem(YbJumbleState *jstate, Node *node)
{
	PartitionElem *expr = (PartitionElem *) node;

	JUMBLE_STRING(name);
	JUMBLE_NODE(expr);
	JUMBLE_NODE(collation);
	JUMBLE_NODE(opclass);
}

static void
_jumblePartitionSpec(YbJumbleState *jstate, Node *node)
{
	PartitionSpec *expr = (PartitionSpec *) node;

	JUMBLE_FIELD(strategy);
	JUMBLE_NODE(partParams);
}

static void
_jumblePartitionBoundSpec(YbJumbleState *jstate, Node *node)
{
	PartitionBoundSpec *expr = (PartitionBoundSpec *) node;

	JUMBLE_FIELD(strategy);
	JUMBLE_FIELD(is_default);
	JUMBLE_FIELD(modulus);
	JUMBLE_FIELD(remainder);
	JUMBLE_NODE(listdatums);
	JUMBLE_NODE(lowerdatums);
	JUMBLE_NODE(upperdatums);
}

static void
_jumblePartitionRangeDatum(YbJumbleState *jstate, Node *node)
{
	PartitionRangeDatum *expr = (PartitionRangeDatum *) node;

	JUMBLE_FIELD(kind);
	JUMBLE_NODE(value);
}

static void
_jumblePartitionCmd(YbJumbleState *jstate, Node *node)
{
	PartitionCmd *expr = (PartitionCmd *) node;

	JUMBLE_NODE(name);
	JUMBLE_NODE(bound);
	JUMBLE_FIELD(concurrent);
}

static void
_jumbleRangeTblEntry(YbJumbleState *jstate, Node *node)
{
	RangeTblEntry *expr = (RangeTblEntry *) node;

	JUMBLE_CUSTOM(RangeTblEntry, eref);
	JUMBLE_NODE(eref);
	JUMBLE_FIELD(rtekind);
	JUMBLE_FIELD(inh);
	JUMBLE_NODE(tablesample);
	JUMBLE_NODE(subquery);
	JUMBLE_FIELD(jointype);
	JUMBLE_NODE(functions);
	JUMBLE_FIELD(funcordinality);
	JUMBLE_NODE(tablefunc);
	JUMBLE_NODE(values_lists);
	JUMBLE_STRING(ctename);
	JUMBLE_FIELD(ctelevelsup);
	JUMBLE_STRING(enrname);
}

#if 0
static void
_jumbleRTEPermissionInfo(YbJumbleState *jstate, Node *node)
{
	RTEPermissionInfo *expr = (RTEPermissionInfo *) node;

	JUMBLE_FIELD(relid);
	JUMBLE_FIELD(inh);
	JUMBLE_FIELD(requiredPerms);
	JUMBLE_FIELD(checkAsUser);
	JUMBLE_BITMAPSET(selectedCols);
	JUMBLE_BITMAPSET(insertedCols);
	JUMBLE_BITMAPSET(updatedCols);
}
#endif

static void
_jumbleRangeTblFunction(YbJumbleState *jstate, Node *node)
{
	RangeTblFunction *expr = (RangeTblFunction *) node;

	JUMBLE_NODE(funcexpr);
}

static void
_jumbleTableSampleClause(YbJumbleState *jstate, Node *node)
{
	TableSampleClause *expr = (TableSampleClause *) node;

	JUMBLE_FIELD(tsmhandler);
	JUMBLE_NODE(args);
	JUMBLE_NODE(repeatable);
}

static void
_jumbleWithCheckOption(YbJumbleState *jstate, Node *node)
{
	WithCheckOption *expr = (WithCheckOption *) node;

	JUMBLE_FIELD(kind);
	JUMBLE_STRING(relname);
	JUMBLE_STRING(polname);
	JUMBLE_NODE(qual);
	JUMBLE_FIELD(cascaded);
}

static void
_jumbleSortGroupClause(YbJumbleState *jstate, Node *node)
{
	SortGroupClause *expr = (SortGroupClause *) node;

	JUMBLE_FIELD(tleSortGroupRef);
	JUMBLE_FIELD(eqop);
	JUMBLE_FIELD(sortop);
	/* JUMBLE_FIELD(reverse_sort); */
	JUMBLE_FIELD(nulls_first);
}

static void
_jumbleGroupingSet(YbJumbleState *jstate, Node *node)
{
	GroupingSet *expr = (GroupingSet *) node;

	JUMBLE_NODE(content);
}

static void
_jumbleWindowClause(YbJumbleState *jstate, Node *node)
{
	WindowClause *expr = (WindowClause *) node;

	JUMBLE_NODE(partitionClause);
	JUMBLE_NODE(orderClause);
	JUMBLE_FIELD(frameOptions);
	JUMBLE_NODE(startOffset);
	JUMBLE_NODE(endOffset);
	JUMBLE_FIELD(winref);
}

static void
_jumbleRowMarkClause(YbJumbleState *jstate, Node *node)
{
	RowMarkClause *expr = (RowMarkClause *) node;

	JUMBLE_FIELD(rti);
	JUMBLE_FIELD(strength);
	JUMBLE_FIELD(waitPolicy);
	JUMBLE_FIELD(pushedDown);
}

static void
_jumbleWithClause(YbJumbleState *jstate, Node *node)
{
	WithClause *expr = (WithClause *) node;

	JUMBLE_NODE(ctes);
	JUMBLE_FIELD(recursive);
}

static void
_jumbleInferClause(YbJumbleState *jstate, Node *node)
{
	InferClause *expr = (InferClause *) node;

	JUMBLE_NODE(indexElems);
	JUMBLE_NODE(whereClause);
	JUMBLE_STRING(conname);
}

static void
_jumbleOnConflictClause(YbJumbleState *jstate, Node *node)
{
	OnConflictClause *expr = (OnConflictClause *) node;

	JUMBLE_FIELD(action);
	JUMBLE_NODE(infer);
	JUMBLE_NODE(targetList);
	JUMBLE_NODE(whereClause);
}

static void
_jumbleCTESearchClause(YbJumbleState *jstate, Node *node)
{
	CTESearchClause *expr = (CTESearchClause *) node;

	JUMBLE_NODE(search_col_list);
	JUMBLE_FIELD(search_breadth_first);
	JUMBLE_STRING(search_seq_column);
}

static void
_jumbleCTECycleClause(YbJumbleState *jstate, Node *node)
{
	CTECycleClause *expr = (CTECycleClause *) node;

	JUMBLE_NODE(cycle_col_list);
	JUMBLE_STRING(cycle_mark_column);
	JUMBLE_NODE(cycle_mark_value);
	JUMBLE_NODE(cycle_mark_default);
	JUMBLE_STRING(cycle_path_column);
	JUMBLE_FIELD(cycle_mark_type);
	JUMBLE_FIELD(cycle_mark_typmod);
	JUMBLE_FIELD(cycle_mark_collation);
	JUMBLE_FIELD(cycle_mark_neop);
}

static void
_jumbleCommonTableExpr(YbJumbleState *jstate, Node *node)
{
	CommonTableExpr *expr = (CommonTableExpr *) node;

	JUMBLE_STRING(ctename);
	JUMBLE_FIELD(ctematerialized);
	JUMBLE_NODE(ctequery);
}

static void
_jumbleMergeWhenClause(YbJumbleState *jstate, Node *node)
{
	MergeWhenClause *expr = (MergeWhenClause *) node;

	/* JUMBLE_FIELD(matchKind); */
	JUMBLE_FIELD(commandType);
	JUMBLE_FIELD(override);
	JUMBLE_NODE(condition);
	JUMBLE_NODE(targetList);
	JUMBLE_NODE(values);
}

#if 0
static void
_jumbleReturningOption(YbJumbleState *jstate, Node *node)
{
	ReturningOption *expr = (ReturningOption *) node;

	JUMBLE_FIELD(option);
	JUMBLE_STRING(value);
}

static void
_jumbleReturningClause(YbJumbleState *jstate, Node *node)
{
	ReturningClause *expr = (ReturningClause *) node;

	JUMBLE_NODE(options);
	JUMBLE_NODE(exprs);
}
#endif

static void
_jumbleTriggerTransition(YbJumbleState *jstate, Node *node)
{
	TriggerTransition *expr = (TriggerTransition *) node;

	JUMBLE_STRING(name);
	JUMBLE_FIELD(isNew);
	JUMBLE_FIELD(isTable);
}

#if 0
static void
_jumbleJsonOutput(YbJumbleState *jstate, Node *node)
{
	JsonOutput *expr = (JsonOutput *) node;

	JUMBLE_NODE(typeName);
	JUMBLE_NODE(returning);
}

static void
_jumbleJsonArgument(YbJumbleState *jstate, Node *node)
{
	JsonArgument *expr = (JsonArgument *) node;

	JUMBLE_NODE(val);
	JUMBLE_STRING(name);
}

static void
_jumbleJsonFuncExpr(YbJumbleState *jstate, Node *node)
{
	JsonFuncExpr *expr = (JsonFuncExpr *) node;

	JUMBLE_FIELD(op);
	JUMBLE_STRING(column_name);
	JUMBLE_NODE(context_item);
	JUMBLE_NODE(pathspec);
	JUMBLE_NODE(passing);
	JUMBLE_NODE(output);
	JUMBLE_NODE(on_empty);
	JUMBLE_NODE(on_error);
	JUMBLE_FIELD(wrapper);
	JUMBLE_FIELD(quotes);
}

static void
_jumbleJsonTablePathSpec(YbJumbleState *jstate, Node *node)
{
	JsonTablePathSpec *expr = (JsonTablePathSpec *) node;

	JUMBLE_NODE(string);
	JUMBLE_STRING(name);
}

static void
_jumbleJsonTable(YbJumbleState *jstate, Node *node)
{
	JsonTable  *expr = (JsonTable *) node;

	JUMBLE_NODE(context_item);
	JUMBLE_NODE(pathspec);
	JUMBLE_NODE(passing);
	JUMBLE_NODE(columns);
	JUMBLE_NODE(on_error);
	JUMBLE_NODE(alias);
	JUMBLE_FIELD(lateral);
}

static void
_jumbleJsonTableColumn(YbJumbleState *jstate, Node *node)
{
	JsonTableColumn *expr = (JsonTableColumn *) node;

	JUMBLE_FIELD(coltype);
	JUMBLE_STRING(name);
	JUMBLE_NODE(typeName);
	JUMBLE_NODE(pathspec);
	JUMBLE_NODE(format);
	JUMBLE_FIELD(wrapper);
	JUMBLE_FIELD(quotes);
	JUMBLE_NODE(columns);
	JUMBLE_NODE(on_empty);
	JUMBLE_NODE(on_error);
}

static void
_jumbleJsonKeyValue(YbJumbleState *jstate, Node *node)
{
	JsonKeyValue *expr = (JsonKeyValue *) node;

	JUMBLE_NODE(key);
	JUMBLE_NODE(value);
}

static void
_jumbleJsonParseExpr(YbJumbleState *jstate, Node *node)
{
	JsonParseExpr *expr = (JsonParseExpr *) node;

	JUMBLE_NODE(expr);
	JUMBLE_NODE(output);
	JUMBLE_FIELD(unique_keys);
}

static void
_jumbleJsonScalarExpr(YbJumbleState *jstate, Node *node)
{
	JsonScalarExpr *expr = (JsonScalarExpr *) node;

	JUMBLE_NODE(expr);
	JUMBLE_NODE(output);
}

static void
_jumbleJsonSerializeExpr(YbJumbleState *jstate, Node *node)
{
	JsonSerializeExpr *expr = (JsonSerializeExpr *) node;

	JUMBLE_NODE(expr);
	JUMBLE_NODE(output);
}

static void
_jumbleJsonObjectConstructor(YbJumbleState *jstate, Node *node)
{
	JsonObjectConstructor *expr = (JsonObjectConstructor *) node;

	JUMBLE_NODE(exprs);
	JUMBLE_NODE(output);
	JUMBLE_FIELD(absent_on_null);
	JUMBLE_FIELD(unique);
}

static void
_jumbleJsonArrayConstructor(YbJumbleState *jstate, Node *node)
{
	JsonArrayConstructor *expr = (JsonArrayConstructor *) node;

	JUMBLE_NODE(exprs);
	JUMBLE_NODE(output);
	JUMBLE_FIELD(absent_on_null);
}

static void
_jumbleJsonArrayQueryConstructor(YbJumbleState *jstate, Node *node)
{
	JsonArrayQueryConstructor *expr = (JsonArrayQueryConstructor *) node;

	JUMBLE_NODE(query);
	JUMBLE_NODE(output);
	JUMBLE_NODE(format);
	JUMBLE_FIELD(absent_on_null);
}

static void
_jumbleJsonAggConstructor(YbJumbleState *jstate, Node *node)
{
	JsonAggConstructor *expr = (JsonAggConstructor *) node;

	JUMBLE_NODE(output);
	JUMBLE_NODE(agg_filter);
	JUMBLE_NODE(agg_order);
	JUMBLE_NODE(over);
}

static void
_jumbleJsonObjectAgg(YbJumbleState *jstate, Node *node)
{
	JsonObjectAgg *expr = (JsonObjectAgg *) node;

	JUMBLE_NODE(constructor);
	JUMBLE_NODE(arg);
	JUMBLE_FIELD(absent_on_null);
	JUMBLE_FIELD(unique);
}

static void
_jumbleJsonArrayAgg(YbJumbleState *jstate, Node *node)
{
	JsonArrayAgg *expr = (JsonArrayAgg *) node;

	JUMBLE_NODE(constructor);
	JUMBLE_NODE(arg);
	JUMBLE_FIELD(absent_on_null);
}
#endif

static void
_jumbleInsertStmt(YbJumbleState *jstate, Node *node)
{
	InsertStmt *expr = (InsertStmt *) node;

	JUMBLE_NODE(relation);
	JUMBLE_NODE(cols);
	JUMBLE_NODE(selectStmt);
	JUMBLE_NODE(onConflictClause);
	/* JUMBLE_NODE(returningClause); */
	JUMBLE_NODE(returningList);
	JUMBLE_NODE(withClause);
	JUMBLE_FIELD(override);
}

static void
_jumbleDeleteStmt(YbJumbleState *jstate, Node *node)
{
	DeleteStmt *expr = (DeleteStmt *) node;

	JUMBLE_NODE(relation);
	JUMBLE_NODE(usingClause);
	JUMBLE_NODE(whereClause);
	/* JUMBLE_NODE(returningClause); */
	JUMBLE_NODE(returningList);
	JUMBLE_NODE(withClause);
}

static void
_jumbleUpdateStmt(YbJumbleState *jstate, Node *node)
{
	UpdateStmt *expr = (UpdateStmt *) node;

	JUMBLE_NODE(relation);
	JUMBLE_NODE(targetList);
	JUMBLE_NODE(whereClause);
	JUMBLE_NODE(fromClause);
	/* JUMBLE_NODE(returningClause); */
	JUMBLE_NODE(returningList);
	JUMBLE_NODE(withClause);
}

static void
_jumbleMergeStmt(YbJumbleState *jstate, Node *node)
{
	MergeStmt  *expr = (MergeStmt *) node;

	JUMBLE_NODE(relation);
	JUMBLE_NODE(sourceRelation);
	JUMBLE_NODE(joinCondition);
	JUMBLE_NODE(mergeWhenClauses);
	/* JUMBLE_NODE(returningClause); */
	JUMBLE_NODE(withClause);
}

static void
_jumbleSelectStmt(YbJumbleState *jstate, Node *node)
{
	SelectStmt *expr = (SelectStmt *) node;

	JUMBLE_NODE(distinctClause);
	JUMBLE_NODE(intoClause);
	JUMBLE_NODE(targetList);
	JUMBLE_NODE(fromClause);
	JUMBLE_NODE(whereClause);
	JUMBLE_NODE(groupClause);
	JUMBLE_FIELD(groupDistinct);
	JUMBLE_NODE(havingClause);
	JUMBLE_NODE(windowClause);
	JUMBLE_NODE(valuesLists);
	JUMBLE_NODE(sortClause);
	JUMBLE_NODE(limitOffset);
	JUMBLE_NODE(limitCount);
	JUMBLE_FIELD(limitOption);
	JUMBLE_NODE(lockingClause);
	JUMBLE_NODE(withClause);
	JUMBLE_FIELD(op);
	JUMBLE_FIELD(all);
	JUMBLE_NODE(larg);
	JUMBLE_NODE(rarg);
}

static void
_jumbleSetOperationStmt(YbJumbleState *jstate, Node *node)
{
	SetOperationStmt *expr = (SetOperationStmt *) node;

	JUMBLE_FIELD(op);
	JUMBLE_FIELD(all);
	JUMBLE_NODE(larg);
	JUMBLE_NODE(rarg);
}

static void
_jumbleReturnStmt(YbJumbleState *jstate, Node *node)
{
	ReturnStmt *expr = (ReturnStmt *) node;

	JUMBLE_NODE(returnval);
}

static void
_jumblePLAssignStmt(YbJumbleState *jstate, Node *node)
{
	PLAssignStmt *expr = (PLAssignStmt *) node;

	JUMBLE_STRING(name);
	JUMBLE_NODE(indirection);
	JUMBLE_FIELD(nnames);
	JUMBLE_NODE(val);
}

static void
_jumbleCreateSchemaStmt(YbJumbleState *jstate, Node *node)
{
	CreateSchemaStmt *expr = (CreateSchemaStmt *) node;

	JUMBLE_STRING(schemaname);
	JUMBLE_NODE(authrole);
	JUMBLE_NODE(schemaElts);
	JUMBLE_FIELD(if_not_exists);
}

static void
_jumbleAlterTableStmt(YbJumbleState *jstate, Node *node)
{
	AlterTableStmt *expr = (AlterTableStmt *) node;

	JUMBLE_NODE(relation);
	JUMBLE_NODE(cmds);
	JUMBLE_FIELD(objtype);
	JUMBLE_FIELD(missing_ok);
}

static void
_jumbleAlterTableCmd(YbJumbleState *jstate, Node *node)
{
	AlterTableCmd *expr = (AlterTableCmd *) node;

	JUMBLE_FIELD(subtype);
	JUMBLE_STRING(name);
	JUMBLE_FIELD(num);
	JUMBLE_NODE(newowner);
	JUMBLE_NODE(def);
	JUMBLE_FIELD(behavior);
	JUMBLE_FIELD(missing_ok);
	JUMBLE_FIELD(recurse);
}

#if 0
static void
_jumbleATAlterConstraint(YbJumbleState *jstate, Node *node)
{
	ATAlterConstraint *expr = (ATAlterConstraint *) node;

	JUMBLE_STRING(conname);
	JUMBLE_FIELD(alterEnforceability);
	JUMBLE_FIELD(is_enforced);
	JUMBLE_FIELD(alterDeferrability);
	JUMBLE_FIELD(deferrable);
	JUMBLE_FIELD(initdeferred);
	JUMBLE_FIELD(alterInheritability);
	JUMBLE_FIELD(noinherit);
}
#endif

static void
_jumbleReplicaIdentityStmt(YbJumbleState *jstate, Node *node)
{
	ReplicaIdentityStmt *expr = (ReplicaIdentityStmt *) node;

	JUMBLE_FIELD(identity_type);
	JUMBLE_STRING(name);
}

static void
_jumbleAlterCollationStmt(YbJumbleState *jstate, Node *node)
{
	AlterCollationStmt *expr = (AlterCollationStmt *) node;

	JUMBLE_NODE(collname);
}

static void
_jumbleAlterDomainStmt(YbJumbleState *jstate, Node *node)
{
	AlterDomainStmt *expr = (AlterDomainStmt *) node;

	JUMBLE_FIELD(subtype);
	JUMBLE_NODE(typeName);
	JUMBLE_STRING(name);
	JUMBLE_NODE(def);
	JUMBLE_FIELD(behavior);
	JUMBLE_FIELD(missing_ok);
}

static void
_jumbleGrantStmt(YbJumbleState *jstate, Node *node)
{
	GrantStmt  *expr = (GrantStmt *) node;

	JUMBLE_FIELD(is_grant);
	JUMBLE_FIELD(targtype);
	JUMBLE_FIELD(objtype);
	JUMBLE_NODE(objects);
	JUMBLE_NODE(privileges);
	JUMBLE_NODE(grantees);
	JUMBLE_FIELD(grant_option);
	JUMBLE_NODE(grantor);
	JUMBLE_FIELD(behavior);
}

static void
_jumbleObjectWithArgs(YbJumbleState *jstate, Node *node)
{
	ObjectWithArgs *expr = (ObjectWithArgs *) node;

	JUMBLE_NODE(objname);
	JUMBLE_NODE(objargs);
	JUMBLE_NODE(objfuncargs);
	JUMBLE_FIELD(args_unspecified);
}

static void
_jumbleAccessPriv(YbJumbleState *jstate, Node *node)
{
	AccessPriv *expr = (AccessPriv *) node;

	JUMBLE_STRING(priv_name);
	JUMBLE_NODE(cols);
}

static void
_jumbleGrantRoleStmt(YbJumbleState *jstate, Node *node)
{
	GrantRoleStmt *expr = (GrantRoleStmt *) node;

	JUMBLE_NODE(granted_roles);
	JUMBLE_NODE(grantee_roles);
	JUMBLE_FIELD(is_grant);
	/* JUMBLE_NODE(opt); */
	JUMBLE_NODE(grantor);
	JUMBLE_FIELD(behavior);
}

static void
_jumbleAlterDefaultPrivilegesStmt(YbJumbleState *jstate, Node *node)
{
	AlterDefaultPrivilegesStmt *expr = (AlterDefaultPrivilegesStmt *) node;

	JUMBLE_NODE(options);
	JUMBLE_NODE(action);
}

static void
_jumbleCopyStmt(YbJumbleState *jstate, Node *node)
{
	CopyStmt   *expr = (CopyStmt *) node;

	JUMBLE_NODE(relation);
	JUMBLE_NODE(query);
	JUMBLE_NODE(attlist);
	JUMBLE_FIELD(is_from);
	JUMBLE_FIELD(is_program);
	JUMBLE_STRING(filename);
	JUMBLE_NODE(options);
	JUMBLE_NODE(whereClause);
}

static void
_jumbleVariableShowStmt(YbJumbleState *jstate, Node *node)
{
	VariableShowStmt *expr = (VariableShowStmt *) node;

	JUMBLE_STRING(name);
}

static void
_jumbleCreateStmt(YbJumbleState *jstate, Node *node)
{
	CreateStmt *expr = (CreateStmt *) node;

	JUMBLE_NODE(relation);
	JUMBLE_NODE(tableElts);
	JUMBLE_NODE(inhRelations);
	JUMBLE_NODE(partbound);
	JUMBLE_NODE(partspec);
	JUMBLE_NODE(ofTypename);
	JUMBLE_NODE(constraints);
	/* JUMBLE_NODE(nnconstraints); */
	JUMBLE_NODE(options);
	JUMBLE_FIELD(oncommit);
	JUMBLE_STRING(tablespacename);
	JUMBLE_STRING(accessMethod);
	JUMBLE_FIELD(if_not_exists);
}

static void
_jumbleConstraint(YbJumbleState *jstate, Node *node)
{
	Constraint *expr = (Constraint *) node;

	JUMBLE_FIELD(contype);
	JUMBLE_STRING(conname);
	JUMBLE_FIELD(deferrable);
	JUMBLE_FIELD(initdeferred);
	/* JUMBLE_FIELD(is_enforced); */
	JUMBLE_FIELD(skip_validation);
	JUMBLE_FIELD(initially_valid);
	JUMBLE_FIELD(is_no_inherit);
	JUMBLE_NODE(raw_expr);
	JUMBLE_STRING(cooked_expr);
	JUMBLE_FIELD(generated_when);
	/* JUMBLE_FIELD(generated_kind); */
	JUMBLE_FIELD(nulls_not_distinct);
	JUMBLE_NODE(keys);
	/* JUMBLE_FIELD(without_overlaps); */
	JUMBLE_NODE(including);
	JUMBLE_NODE(exclusions);
	JUMBLE_NODE(options);
	JUMBLE_STRING(indexname);
	JUMBLE_STRING(indexspace);
	JUMBLE_FIELD(reset_default_tblspc);
	JUMBLE_STRING(access_method);
	JUMBLE_NODE(where_clause);
	JUMBLE_NODE(pktable);
	JUMBLE_NODE(fk_attrs);
	JUMBLE_NODE(pk_attrs);
	/* JUMBLE_FIELD(fk_with_period); */
	/* JUMBLE_FIELD(pk_with_period); */
	JUMBLE_FIELD(fk_matchtype);
	JUMBLE_FIELD(fk_upd_action);
	JUMBLE_FIELD(fk_del_action);
	JUMBLE_NODE(fk_del_set_cols);
	JUMBLE_NODE(old_conpfeqop);
	JUMBLE_FIELD(old_pktable_oid);
}

static void
_jumbleCreateTableSpaceStmt(YbJumbleState *jstate, Node *node)
{
	CreateTableSpaceStmt *expr = (CreateTableSpaceStmt *) node;

	JUMBLE_STRING(tablespacename);
	JUMBLE_NODE(owner);
	JUMBLE_STRING(location);
	JUMBLE_NODE(options);
}

static void
_jumbleDropTableSpaceStmt(YbJumbleState *jstate, Node *node)
{
	DropTableSpaceStmt *expr = (DropTableSpaceStmt *) node;

	JUMBLE_STRING(tablespacename);
	JUMBLE_FIELD(missing_ok);
}

static void
_jumbleAlterTableSpaceOptionsStmt(YbJumbleState *jstate, Node *node)
{
	AlterTableSpaceOptionsStmt *expr = (AlterTableSpaceOptionsStmt *) node;

	JUMBLE_STRING(tablespacename);
	JUMBLE_NODE(options);
	JUMBLE_FIELD(isReset);
}

static void
_jumbleAlterTableMoveAllStmt(YbJumbleState *jstate, Node *node)
{
	AlterTableMoveAllStmt *expr = (AlterTableMoveAllStmt *) node;

	JUMBLE_STRING(orig_tablespacename);
	JUMBLE_FIELD(objtype);
	JUMBLE_NODE(roles);
	JUMBLE_STRING(new_tablespacename);
	JUMBLE_FIELD(nowait);
}

static void
_jumbleCreateExtensionStmt(YbJumbleState *jstate, Node *node)
{
	CreateExtensionStmt *expr = (CreateExtensionStmt *) node;

	JUMBLE_STRING(extname);
	JUMBLE_FIELD(if_not_exists);
	JUMBLE_NODE(options);
}

static void
_jumbleAlterExtensionStmt(YbJumbleState *jstate, Node *node)
{
	AlterExtensionStmt *expr = (AlterExtensionStmt *) node;

	JUMBLE_STRING(extname);
	JUMBLE_NODE(options);
}

static void
_jumbleAlterExtensionContentsStmt(YbJumbleState *jstate, Node *node)
{
	AlterExtensionContentsStmt *expr = (AlterExtensionContentsStmt *) node;

	JUMBLE_STRING(extname);
	JUMBLE_FIELD(action);
	JUMBLE_FIELD(objtype);
	JUMBLE_NODE(object);
}

static void
_jumbleCreateFdwStmt(YbJumbleState *jstate, Node *node)
{
	CreateFdwStmt *expr = (CreateFdwStmt *) node;

	JUMBLE_STRING(fdwname);
	JUMBLE_NODE(func_options);
	JUMBLE_NODE(options);
}

static void
_jumbleAlterFdwStmt(YbJumbleState *jstate, Node *node)
{
	AlterFdwStmt *expr = (AlterFdwStmt *) node;

	JUMBLE_STRING(fdwname);
	JUMBLE_NODE(func_options);
	JUMBLE_NODE(options);
}

static void
_jumbleCreateForeignServerStmt(YbJumbleState *jstate, Node *node)
{
	CreateForeignServerStmt *expr = (CreateForeignServerStmt *) node;

	JUMBLE_STRING(servername);
	JUMBLE_STRING(servertype);
	JUMBLE_STRING(version);
	JUMBLE_STRING(fdwname);
	JUMBLE_FIELD(if_not_exists);
	JUMBLE_NODE(options);
}

static void
_jumbleAlterForeignServerStmt(YbJumbleState *jstate, Node *node)
{
	AlterForeignServerStmt *expr = (AlterForeignServerStmt *) node;

	JUMBLE_STRING(servername);
	JUMBLE_STRING(version);
	JUMBLE_NODE(options);
	JUMBLE_FIELD(has_version);
}

static void
_jumbleCreateForeignTableStmt(YbJumbleState *jstate, Node *node)
{
	CreateForeignTableStmt *expr = (CreateForeignTableStmt *) node;

	JUMBLE_NODE(base.relation);
	JUMBLE_NODE(base.tableElts);
	JUMBLE_NODE(base.inhRelations);
	JUMBLE_NODE(base.partbound);
	JUMBLE_NODE(base.partspec);
	JUMBLE_NODE(base.ofTypename);
	JUMBLE_NODE(base.constraints);
	/* JUMBLE_NODE(base.nnconstraints); */
	JUMBLE_NODE(base.options);
	JUMBLE_FIELD(base.oncommit);
	JUMBLE_STRING(base.tablespacename);
	JUMBLE_STRING(base.accessMethod);
	JUMBLE_FIELD(base.if_not_exists);
	JUMBLE_STRING(servername);
	JUMBLE_NODE(options);
}

static void
_jumbleCreateUserMappingStmt(YbJumbleState *jstate, Node *node)
{
	CreateUserMappingStmt *expr = (CreateUserMappingStmt *) node;

	JUMBLE_NODE(user);
	JUMBLE_STRING(servername);
	JUMBLE_FIELD(if_not_exists);
	JUMBLE_NODE(options);
}

static void
_jumbleAlterUserMappingStmt(YbJumbleState *jstate, Node *node)
{
	AlterUserMappingStmt *expr = (AlterUserMappingStmt *) node;

	JUMBLE_NODE(user);
	JUMBLE_STRING(servername);
	JUMBLE_NODE(options);
}

static void
_jumbleDropUserMappingStmt(YbJumbleState *jstate, Node *node)
{
	DropUserMappingStmt *expr = (DropUserMappingStmt *) node;

	JUMBLE_NODE(user);
	JUMBLE_STRING(servername);
	JUMBLE_FIELD(missing_ok);
}

static void
_jumbleImportForeignSchemaStmt(YbJumbleState *jstate, Node *node)
{
	ImportForeignSchemaStmt *expr = (ImportForeignSchemaStmt *) node;

	JUMBLE_STRING(server_name);
	JUMBLE_STRING(remote_schema);
	JUMBLE_STRING(local_schema);
	JUMBLE_FIELD(list_type);
	JUMBLE_NODE(table_list);
	JUMBLE_NODE(options);
}

static void
_jumbleCreatePolicyStmt(YbJumbleState *jstate, Node *node)
{
	CreatePolicyStmt *expr = (CreatePolicyStmt *) node;

	JUMBLE_STRING(policy_name);
	JUMBLE_NODE(table);
	JUMBLE_STRING(cmd_name);
	JUMBLE_FIELD(permissive);
	JUMBLE_NODE(roles);
	JUMBLE_NODE(qual);
	JUMBLE_NODE(with_check);
}

static void
_jumbleAlterPolicyStmt(YbJumbleState *jstate, Node *node)
{
	AlterPolicyStmt *expr = (AlterPolicyStmt *) node;

	JUMBLE_STRING(policy_name);
	JUMBLE_NODE(table);
	JUMBLE_NODE(roles);
	JUMBLE_NODE(qual);
	JUMBLE_NODE(with_check);
}

static void
_jumbleCreateAmStmt(YbJumbleState *jstate, Node *node)
{
	CreateAmStmt *expr = (CreateAmStmt *) node;

	JUMBLE_STRING(amname);
	JUMBLE_NODE(handler_name);
	JUMBLE_FIELD(amtype);
}

static void
_jumbleCreateTrigStmt(YbJumbleState *jstate, Node *node)
{
	CreateTrigStmt *expr = (CreateTrigStmt *) node;

	JUMBLE_FIELD(replace);
	JUMBLE_FIELD(isconstraint);
	JUMBLE_STRING(trigname);
	JUMBLE_NODE(relation);
	JUMBLE_NODE(funcname);
	JUMBLE_NODE(args);
	JUMBLE_FIELD(row);
	JUMBLE_FIELD(timing);
	JUMBLE_FIELD(events);
	JUMBLE_NODE(columns);
	JUMBLE_NODE(whenClause);
	JUMBLE_NODE(transitionRels);
	JUMBLE_FIELD(deferrable);
	JUMBLE_FIELD(initdeferred);
	JUMBLE_NODE(constrrel);
}

static void
_jumbleCreateEventTrigStmt(YbJumbleState *jstate, Node *node)
{
	CreateEventTrigStmt *expr = (CreateEventTrigStmt *) node;

	JUMBLE_STRING(trigname);
	JUMBLE_STRING(eventname);
	JUMBLE_NODE(whenclause);
	JUMBLE_NODE(funcname);
}

static void
_jumbleAlterEventTrigStmt(YbJumbleState *jstate, Node *node)
{
	AlterEventTrigStmt *expr = (AlterEventTrigStmt *) node;

	JUMBLE_STRING(trigname);
	JUMBLE_FIELD(tgenabled);
}

static void
_jumbleCreatePLangStmt(YbJumbleState *jstate, Node *node)
{
	CreatePLangStmt *expr = (CreatePLangStmt *) node;

	JUMBLE_FIELD(replace);
	JUMBLE_STRING(plname);
	JUMBLE_NODE(plhandler);
	JUMBLE_NODE(plinline);
	JUMBLE_NODE(plvalidator);
	JUMBLE_FIELD(pltrusted);
}

static void
_jumbleCreateRoleStmt(YbJumbleState *jstate, Node *node)
{
	CreateRoleStmt *expr = (CreateRoleStmt *) node;

	JUMBLE_FIELD(stmt_type);
	JUMBLE_STRING(role);
	JUMBLE_NODE(options);
}

static void
_jumbleAlterRoleStmt(YbJumbleState *jstate, Node *node)
{
	AlterRoleStmt *expr = (AlterRoleStmt *) node;

	JUMBLE_NODE(role);
	JUMBLE_NODE(options);
	JUMBLE_FIELD(action);
}

static void
_jumbleAlterRoleSetStmt(YbJumbleState *jstate, Node *node)
{
	AlterRoleSetStmt *expr = (AlterRoleSetStmt *) node;

	JUMBLE_NODE(role);
	JUMBLE_STRING(database);
	JUMBLE_NODE(setstmt);
}

static void
_jumbleDropRoleStmt(YbJumbleState *jstate, Node *node)
{
	DropRoleStmt *expr = (DropRoleStmt *) node;

	JUMBLE_NODE(roles);
	JUMBLE_FIELD(missing_ok);
}

static void
_jumbleCreateSeqStmt(YbJumbleState *jstate, Node *node)
{
	CreateSeqStmt *expr = (CreateSeqStmt *) node;

	JUMBLE_NODE(sequence);
	JUMBLE_NODE(options);
	JUMBLE_FIELD(ownerId);
	JUMBLE_FIELD(for_identity);
	JUMBLE_FIELD(if_not_exists);
}

static void
_jumbleAlterSeqStmt(YbJumbleState *jstate, Node *node)
{
	AlterSeqStmt *expr = (AlterSeqStmt *) node;

	JUMBLE_NODE(sequence);
	JUMBLE_NODE(options);
	JUMBLE_FIELD(for_identity);
	JUMBLE_FIELD(missing_ok);
}

static void
_jumbleDefineStmt(YbJumbleState *jstate, Node *node)
{
	DefineStmt *expr = (DefineStmt *) node;

	JUMBLE_FIELD(kind);
	JUMBLE_FIELD(oldstyle);
	JUMBLE_NODE(defnames);
	JUMBLE_NODE(args);
	JUMBLE_NODE(definition);
	JUMBLE_FIELD(if_not_exists);
	JUMBLE_FIELD(replace);
}

static void
_jumbleCreateDomainStmt(YbJumbleState *jstate, Node *node)
{
	CreateDomainStmt *expr = (CreateDomainStmt *) node;

	JUMBLE_NODE(domainname);
	JUMBLE_NODE(typeName);
	JUMBLE_NODE(collClause);
	JUMBLE_NODE(constraints);
}

static void
_jumbleCreateOpClassStmt(YbJumbleState *jstate, Node *node)
{
	CreateOpClassStmt *expr = (CreateOpClassStmt *) node;

	JUMBLE_NODE(opclassname);
	JUMBLE_NODE(opfamilyname);
	JUMBLE_STRING(amname);
	JUMBLE_NODE(datatype);
	JUMBLE_NODE(items);
	JUMBLE_FIELD(isDefault);
}

static void
_jumbleCreateOpClassItem(YbJumbleState *jstate, Node *node)
{
	CreateOpClassItem *expr = (CreateOpClassItem *) node;

	JUMBLE_FIELD(itemtype);
	JUMBLE_NODE(name);
	JUMBLE_FIELD(number);
	JUMBLE_NODE(order_family);
	JUMBLE_NODE(class_args);
	JUMBLE_NODE(storedtype);
}

static void
_jumbleCreateOpFamilyStmt(YbJumbleState *jstate, Node *node)
{
	CreateOpFamilyStmt *expr = (CreateOpFamilyStmt *) node;

	JUMBLE_NODE(opfamilyname);
	JUMBLE_STRING(amname);
}

static void
_jumbleAlterOpFamilyStmt(YbJumbleState *jstate, Node *node)
{
	AlterOpFamilyStmt *expr = (AlterOpFamilyStmt *) node;

	JUMBLE_NODE(opfamilyname);
	JUMBLE_STRING(amname);
	JUMBLE_FIELD(isDrop);
	JUMBLE_NODE(items);
}

static void
_jumbleDropStmt(YbJumbleState *jstate, Node *node)
{
	DropStmt   *expr = (DropStmt *) node;

	JUMBLE_NODE(objects);
	JUMBLE_FIELD(removeType);
	JUMBLE_FIELD(behavior);
	JUMBLE_FIELD(missing_ok);
	JUMBLE_FIELD(concurrent);
}

static void
_jumbleTruncateStmt(YbJumbleState *jstate, Node *node)
{
	TruncateStmt *expr = (TruncateStmt *) node;

	JUMBLE_NODE(relations);
	JUMBLE_FIELD(restart_seqs);
	JUMBLE_FIELD(behavior);
}

static void
_jumbleCommentStmt(YbJumbleState *jstate, Node *node)
{
	CommentStmt *expr = (CommentStmt *) node;

	JUMBLE_FIELD(objtype);
	JUMBLE_NODE(object);
	JUMBLE_STRING(comment);
}

static void
_jumbleSecLabelStmt(YbJumbleState *jstate, Node *node)
{
	SecLabelStmt *expr = (SecLabelStmt *) node;

	JUMBLE_FIELD(objtype);
	JUMBLE_NODE(object);
	JUMBLE_STRING(provider);
	JUMBLE_STRING(label);
}

static void
_jumbleDeclareCursorStmt(YbJumbleState *jstate, Node *node)
{
	DeclareCursorStmt *expr = (DeclareCursorStmt *) node;

	JUMBLE_STRING(portalname);
	JUMBLE_FIELD(options);
	JUMBLE_NODE(query);
}

static void
_jumbleClosePortalStmt(YbJumbleState *jstate, Node *node)
{
	ClosePortalStmt *expr = (ClosePortalStmt *) node;

	JUMBLE_STRING(portalname);
}

static void
_jumbleFetchStmt(YbJumbleState *jstate, Node *node)
{
	FetchStmt  *expr = (FetchStmt *) node;

	JUMBLE_FIELD(direction);
	JUMBLE_FIELD(howMany);
	JUMBLE_STRING(portalname);
	JUMBLE_FIELD(ismove);
}

static void
_jumbleIndexStmt(YbJumbleState *jstate, Node *node)
{
	IndexStmt  *expr = (IndexStmt *) node;

	JUMBLE_STRING(idxname);
	JUMBLE_NODE(relation);
	JUMBLE_STRING(accessMethod);
	JUMBLE_STRING(tableSpace);
	JUMBLE_NODE(indexParams);
	JUMBLE_NODE(indexIncludingParams);
	JUMBLE_NODE(options);
	JUMBLE_NODE(whereClause);
	JUMBLE_NODE(excludeOpNames);
	JUMBLE_STRING(idxcomment);
	JUMBLE_FIELD(indexOid);
	/* JUMBLE_FIELD(oldNumber); */
	JUMBLE_FIELD(oldCreateSubid);
	/* JUMBLE_FIELD(oldFirstRelfilelocatorSubid); */
	JUMBLE_FIELD(unique);
	JUMBLE_FIELD(nulls_not_distinct);
	JUMBLE_FIELD(primary);
	JUMBLE_FIELD(isconstraint);
	/* JUMBLE_FIELD(iswithoutoverlaps); */
	JUMBLE_FIELD(deferrable);
	JUMBLE_FIELD(initdeferred);
	JUMBLE_FIELD(transformed);
	JUMBLE_FIELD(concurrent);
	JUMBLE_FIELD(if_not_exists);
	JUMBLE_FIELD(reset_default_tblspc);
}

static void
_jumbleCreateStatsStmt(YbJumbleState *jstate, Node *node)
{
	CreateStatsStmt *expr = (CreateStatsStmt *) node;

	JUMBLE_NODE(defnames);
	JUMBLE_NODE(stat_types);
	JUMBLE_NODE(exprs);
	JUMBLE_NODE(relations);
	JUMBLE_STRING(stxcomment);
	JUMBLE_FIELD(transformed);
	JUMBLE_FIELD(if_not_exists);
}

static void
_jumbleStatsElem(YbJumbleState *jstate, Node *node)
{
	StatsElem  *expr = (StatsElem *) node;

	JUMBLE_STRING(name);
	JUMBLE_NODE(expr);
}

static void
_jumbleAlterStatsStmt(YbJumbleState *jstate, Node *node)
{
	AlterStatsStmt *expr = (AlterStatsStmt *) node;

	JUMBLE_NODE(defnames);
	/* JUMBLE_NODE(stxstattarget); */
	JUMBLE_FIELD(stxstattarget);
	JUMBLE_FIELD(missing_ok);
}

static void
_jumbleCreateFunctionStmt(YbJumbleState *jstate, Node *node)
{
	CreateFunctionStmt *expr = (CreateFunctionStmt *) node;

	JUMBLE_FIELD(is_procedure);
	JUMBLE_FIELD(replace);
	JUMBLE_NODE(funcname);
	JUMBLE_NODE(parameters);
	JUMBLE_NODE(returnType);
	JUMBLE_NODE(options);
	JUMBLE_NODE(sql_body);
}

static void
_jumbleFunctionParameter(YbJumbleState *jstate, Node *node)
{
	FunctionParameter *expr = (FunctionParameter *) node;

	JUMBLE_STRING(name);
	JUMBLE_NODE(argType);
	JUMBLE_FIELD(mode);
	JUMBLE_NODE(defexpr);
}

static void
_jumbleAlterFunctionStmt(YbJumbleState *jstate, Node *node)
{
	AlterFunctionStmt *expr = (AlterFunctionStmt *) node;

	JUMBLE_FIELD(objtype);
	JUMBLE_NODE(func);
	JUMBLE_NODE(actions);
}

static void
_jumbleDoStmt(YbJumbleState *jstate, Node *node)
{
	DoStmt	   *expr = (DoStmt *) node;

	JUMBLE_NODE(args);
}

static void
_jumbleCallStmt(YbJumbleState *jstate, Node *node)
{
	CallStmt   *expr = (CallStmt *) node;

	JUMBLE_NODE(funcexpr);
	JUMBLE_NODE(outargs);
}

static void
_jumbleRenameStmt(YbJumbleState *jstate, Node *node)
{
	RenameStmt *expr = (RenameStmt *) node;

	JUMBLE_FIELD(renameType);
	JUMBLE_FIELD(relationType);
	JUMBLE_NODE(relation);
	JUMBLE_NODE(object);
	JUMBLE_STRING(subname);
	JUMBLE_STRING(newname);
	JUMBLE_FIELD(behavior);
	JUMBLE_FIELD(missing_ok);
}

static void
_jumbleAlterObjectDependsStmt(YbJumbleState *jstate, Node *node)
{
	AlterObjectDependsStmt *expr = (AlterObjectDependsStmt *) node;

	JUMBLE_FIELD(objectType);
	JUMBLE_NODE(relation);
	JUMBLE_NODE(object);
	JUMBLE_NODE(extname);
	JUMBLE_FIELD(remove);
}

static void
_jumbleAlterObjectSchemaStmt(YbJumbleState *jstate, Node *node)
{
	AlterObjectSchemaStmt *expr = (AlterObjectSchemaStmt *) node;

	JUMBLE_FIELD(objectType);
	JUMBLE_NODE(relation);
	JUMBLE_NODE(object);
	JUMBLE_STRING(newschema);
	JUMBLE_FIELD(missing_ok);
}

static void
_jumbleAlterOwnerStmt(YbJumbleState *jstate, Node *node)
{
	AlterOwnerStmt *expr = (AlterOwnerStmt *) node;

	JUMBLE_FIELD(objectType);
	JUMBLE_NODE(relation);
	JUMBLE_NODE(object);
	JUMBLE_NODE(newowner);
}

static void
_jumbleAlterOperatorStmt(YbJumbleState *jstate, Node *node)
{
	AlterOperatorStmt *expr = (AlterOperatorStmt *) node;

	JUMBLE_NODE(opername);
	JUMBLE_NODE(options);
}

static void
_jumbleAlterTypeStmt(YbJumbleState *jstate, Node *node)
{
	AlterTypeStmt *expr = (AlterTypeStmt *) node;

	JUMBLE_NODE(typeName);
	JUMBLE_NODE(options);
}

static void
_jumbleRuleStmt(YbJumbleState *jstate, Node *node)
{
	RuleStmt   *expr = (RuleStmt *) node;

	JUMBLE_NODE(relation);
	JUMBLE_STRING(rulename);
	JUMBLE_NODE(whereClause);
	JUMBLE_FIELD(event);
	JUMBLE_FIELD(instead);
	JUMBLE_NODE(actions);
	JUMBLE_FIELD(replace);
}

static void
_jumbleNotifyStmt(YbJumbleState *jstate, Node *node)
{
	NotifyStmt *expr = (NotifyStmt *) node;

	JUMBLE_STRING(conditionname);
	JUMBLE_STRING(payload);
}

static void
_jumbleListenStmt(YbJumbleState *jstate, Node *node)
{
	ListenStmt *expr = (ListenStmt *) node;

	JUMBLE_STRING(conditionname);
}

static void
_jumbleUnlistenStmt(YbJumbleState *jstate, Node *node)
{
	UnlistenStmt *expr = (UnlistenStmt *) node;

	JUMBLE_STRING(conditionname);
}

static void
_jumbleTransactionStmt(YbJumbleState *jstate, Node *node)
{
	TransactionStmt *expr = (TransactionStmt *) node;

	JUMBLE_FIELD(kind);
	JUMBLE_NODE(options);
	JUMBLE_FIELD(chain);
	/* JUMBLE_LOCATION(location); */
}

static void
_jumbleCompositeTypeStmt(YbJumbleState *jstate, Node *node)
{
	CompositeTypeStmt *expr = (CompositeTypeStmt *) node;

	JUMBLE_NODE(typevar);
	JUMBLE_NODE(coldeflist);
}

static void
_jumbleCreateEnumStmt(YbJumbleState *jstate, Node *node)
{
	CreateEnumStmt *expr = (CreateEnumStmt *) node;

	JUMBLE_NODE(typeName);
	JUMBLE_NODE(vals);
}

static void
_jumbleCreateRangeStmt(YbJumbleState *jstate, Node *node)
{
	CreateRangeStmt *expr = (CreateRangeStmt *) node;

	JUMBLE_NODE(typeName);
	JUMBLE_NODE(params);
}

static void
_jumbleAlterEnumStmt(YbJumbleState *jstate, Node *node)
{
	AlterEnumStmt *expr = (AlterEnumStmt *) node;

	JUMBLE_NODE(typeName);
	JUMBLE_STRING(oldVal);
	JUMBLE_STRING(newVal);
	JUMBLE_STRING(newValNeighbor);
	JUMBLE_FIELD(newValIsAfter);
	JUMBLE_FIELD(skipIfNewValExists);
}

static void
_jumbleViewStmt(YbJumbleState *jstate, Node *node)
{
	ViewStmt   *expr = (ViewStmt *) node;

	JUMBLE_NODE(view);
	JUMBLE_NODE(aliases);
	JUMBLE_NODE(query);
	JUMBLE_FIELD(replace);
	JUMBLE_NODE(options);
	JUMBLE_FIELD(withCheckOption);
}

static void
_jumbleLoadStmt(YbJumbleState *jstate, Node *node)
{
	LoadStmt   *expr = (LoadStmt *) node;

	JUMBLE_STRING(filename);
}

static void
_jumbleCreatedbStmt(YbJumbleState *jstate, Node *node)
{
	CreatedbStmt *expr = (CreatedbStmt *) node;

	JUMBLE_STRING(dbname);
	JUMBLE_NODE(options);
}

static void
_jumbleAlterDatabaseStmt(YbJumbleState *jstate, Node *node)
{
	AlterDatabaseStmt *expr = (AlterDatabaseStmt *) node;

	JUMBLE_STRING(dbname);
	JUMBLE_NODE(options);
}

static void
_jumbleAlterDatabaseRefreshCollStmt(YbJumbleState *jstate, Node *node)
{
	AlterDatabaseRefreshCollStmt *expr = (AlterDatabaseRefreshCollStmt *) node;

	JUMBLE_STRING(dbname);
}

static void
_jumbleAlterDatabaseSetStmt(YbJumbleState *jstate, Node *node)
{
	AlterDatabaseSetStmt *expr = (AlterDatabaseSetStmt *) node;

	JUMBLE_STRING(dbname);
	JUMBLE_NODE(setstmt);
}

static void
_jumbleDropdbStmt(YbJumbleState *jstate, Node *node)
{
	DropdbStmt *expr = (DropdbStmt *) node;

	JUMBLE_STRING(dbname);
	JUMBLE_FIELD(missing_ok);
	JUMBLE_NODE(options);
}

static void
_jumbleAlterSystemStmt(YbJumbleState *jstate, Node *node)
{
	AlterSystemStmt *expr = (AlterSystemStmt *) node;

	JUMBLE_NODE(setstmt);
}

static void
_jumbleClusterStmt(YbJumbleState *jstate, Node *node)
{
	ClusterStmt *expr = (ClusterStmt *) node;

	JUMBLE_NODE(relation);
	JUMBLE_STRING(indexname);
	JUMBLE_NODE(params);
}

static void
_jumbleVacuumStmt(YbJumbleState *jstate, Node *node)
{
	VacuumStmt *expr = (VacuumStmt *) node;

	JUMBLE_NODE(options);
	JUMBLE_NODE(rels);
	JUMBLE_FIELD(is_vacuumcmd);
}

static void
_jumbleVacuumRelation(YbJumbleState *jstate, Node *node)
{
	VacuumRelation *expr = (VacuumRelation *) node;

	JUMBLE_NODE(relation);
	JUMBLE_FIELD(oid);
	JUMBLE_NODE(va_cols);
}

static void
_jumbleExplainStmt(YbJumbleState *jstate, Node *node)
{
	ExplainStmt *expr = (ExplainStmt *) node;

	JUMBLE_NODE(query);
	JUMBLE_NODE(options);
}

static void
_jumbleCreateTableAsStmt(YbJumbleState *jstate, Node *node)
{
	CreateTableAsStmt *expr = (CreateTableAsStmt *) node;

	JUMBLE_NODE(query);
	JUMBLE_NODE(into);
	JUMBLE_FIELD(objtype);
	JUMBLE_FIELD(is_select_into);
	JUMBLE_FIELD(if_not_exists);
}

static void
_jumbleRefreshMatViewStmt(YbJumbleState *jstate, Node *node)
{
	RefreshMatViewStmt *expr = (RefreshMatViewStmt *) node;

	JUMBLE_FIELD(concurrent);
	JUMBLE_FIELD(skipData);
	JUMBLE_NODE(relation);
}

static void
_jumbleCheckPointStmt(YbJumbleState *jstate, Node *node)
{
	CheckPointStmt *expr = (CheckPointStmt *) node;

	(void) expr;
}

static void
_jumbleDiscardStmt(YbJumbleState *jstate, Node *node)
{
	DiscardStmt *expr = (DiscardStmt *) node;

	JUMBLE_FIELD(target);
}

static void
_jumbleLockStmt(YbJumbleState *jstate, Node *node)
{
	LockStmt   *expr = (LockStmt *) node;

	JUMBLE_NODE(relations);
	JUMBLE_FIELD(mode);
	JUMBLE_FIELD(nowait);
}

static void
_jumbleConstraintsSetStmt(YbJumbleState *jstate, Node *node)
{
	ConstraintsSetStmt *expr = (ConstraintsSetStmt *) node;

	JUMBLE_NODE(constraints);
	JUMBLE_FIELD(deferred);
}

static void
_jumbleReindexStmt(YbJumbleState *jstate, Node *node)
{
	ReindexStmt *expr = (ReindexStmt *) node;

	JUMBLE_FIELD(kind);
	JUMBLE_NODE(relation);
	JUMBLE_STRING(name);
	JUMBLE_NODE(params);
}

static void
_jumbleCreateConversionStmt(YbJumbleState *jstate, Node *node)
{
	CreateConversionStmt *expr = (CreateConversionStmt *) node;

	JUMBLE_NODE(conversion_name);
	JUMBLE_STRING(for_encoding_name);
	JUMBLE_STRING(to_encoding_name);
	JUMBLE_NODE(func_name);
	JUMBLE_FIELD(def);
}

static void
_jumbleCreateCastStmt(YbJumbleState *jstate, Node *node)
{
	CreateCastStmt *expr = (CreateCastStmt *) node;

	JUMBLE_NODE(sourcetype);
	JUMBLE_NODE(targettype);
	JUMBLE_NODE(func);
	JUMBLE_FIELD(context);
	JUMBLE_FIELD(inout);
}

static void
_jumbleCreateTransformStmt(YbJumbleState *jstate, Node *node)
{
	CreateTransformStmt *expr = (CreateTransformStmt *) node;

	JUMBLE_FIELD(replace);
	JUMBLE_NODE(type_name);
	JUMBLE_STRING(lang);
	JUMBLE_NODE(fromsql);
	JUMBLE_NODE(tosql);
}

static void
_jumblePrepareStmt(YbJumbleState *jstate, Node *node)
{
	PrepareStmt *expr = (PrepareStmt *) node;

	JUMBLE_STRING(name);
	JUMBLE_NODE(argtypes);
	JUMBLE_NODE(query);
}

static void
_jumbleExecuteStmt(YbJumbleState *jstate, Node *node)
{
	ExecuteStmt *expr = (ExecuteStmt *) node;

	JUMBLE_STRING(name);
	JUMBLE_NODE(params);
}

#if 0
static void
_jumbleDeallocateStmt(YbJumbleState *jstate, Node *node)
{
	DeallocateStmt *expr = (DeallocateStmt *) node;

	JUMBLE_FIELD(isall);
	JUMBLE_LOCATION(location);
}
#endif

static void
_jumbleDropOwnedStmt(YbJumbleState *jstate, Node *node)
{
	DropOwnedStmt *expr = (DropOwnedStmt *) node;

	JUMBLE_NODE(roles);
	JUMBLE_FIELD(behavior);
}

static void
_jumbleReassignOwnedStmt(YbJumbleState *jstate, Node *node)
{
	ReassignOwnedStmt *expr = (ReassignOwnedStmt *) node;

	JUMBLE_NODE(roles);
	JUMBLE_NODE(newrole);
}

static void
_jumbleAlterTSDictionaryStmt(YbJumbleState *jstate, Node *node)
{
	AlterTSDictionaryStmt *expr = (AlterTSDictionaryStmt *) node;

	JUMBLE_NODE(dictname);
	JUMBLE_NODE(options);
}

static void
_jumbleAlterTSConfigurationStmt(YbJumbleState *jstate, Node *node)
{
	AlterTSConfigurationStmt *expr = (AlterTSConfigurationStmt *) node;

	JUMBLE_FIELD(kind);
	JUMBLE_NODE(cfgname);
	JUMBLE_NODE(tokentype);
	JUMBLE_NODE(dicts);
	JUMBLE_FIELD(override);
	JUMBLE_FIELD(replace);
	JUMBLE_FIELD(missing_ok);
}

static void
_jumblePublicationTable(YbJumbleState *jstate, Node *node)
{
	PublicationTable *expr = (PublicationTable *) node;

	JUMBLE_NODE(relation);
	JUMBLE_NODE(whereClause);
	JUMBLE_NODE(columns);
}

static void
_jumblePublicationObjSpec(YbJumbleState *jstate, Node *node)
{
	PublicationObjSpec *expr = (PublicationObjSpec *) node;

	JUMBLE_FIELD(pubobjtype);
	JUMBLE_STRING(name);
	JUMBLE_NODE(pubtable);
}

static void
_jumbleCreatePublicationStmt(YbJumbleState *jstate, Node *node)
{
	CreatePublicationStmt *expr = (CreatePublicationStmt *) node;

	JUMBLE_STRING(pubname);
	JUMBLE_NODE(options);
	JUMBLE_NODE(pubobjects);
	JUMBLE_FIELD(for_all_tables);
}

static void
_jumbleAlterPublicationStmt(YbJumbleState *jstate, Node *node)
{
	AlterPublicationStmt *expr = (AlterPublicationStmt *) node;

	JUMBLE_STRING(pubname);
	JUMBLE_NODE(options);
	JUMBLE_NODE(pubobjects);
	JUMBLE_FIELD(for_all_tables);
	JUMBLE_FIELD(action);
}

static void
_jumbleCreateSubscriptionStmt(YbJumbleState *jstate, Node *node)
{
	CreateSubscriptionStmt *expr = (CreateSubscriptionStmt *) node;

	JUMBLE_STRING(subname);
	JUMBLE_STRING(conninfo);
	JUMBLE_NODE(publication);
	JUMBLE_NODE(options);
}

static void
_jumbleAlterSubscriptionStmt(YbJumbleState *jstate, Node *node)
{
	AlterSubscriptionStmt *expr = (AlterSubscriptionStmt *) node;

	JUMBLE_FIELD(kind);
	JUMBLE_STRING(subname);
	JUMBLE_STRING(conninfo);
	JUMBLE_NODE(publication);
	JUMBLE_NODE(options);
}

static void
_jumbleDropSubscriptionStmt(YbJumbleState *jstate, Node *node)
{
	DropSubscriptionStmt *expr = (DropSubscriptionStmt *) node;

	JUMBLE_STRING(subname);
	JUMBLE_FIELD(missing_ok);
	JUMBLE_FIELD(behavior);
}

#if 0
static void
_jumbleGroupByOrdering(YbJumbleState *jstate, Node *node)
{
	GroupByOrdering *expr = (GroupByOrdering *) node;

	JUMBLE_NODE(pathkeys);
	JUMBLE_NODE(clauses);
}
#endif

static void
_jumbleResult(YbJumbleState *jstate, Node *node)
{
	Result	   *expr = (Result *) node;

	JUMBLE_FIELD(plan.parallel_aware);
	JUMBLE_FIELD(plan.parallel_safe);
	JUMBLE_FIELD(plan.async_capable);
	JUMBLE_FIELD(plan.plan_node_id);
	JUMBLE_NODE(plan.targetlist);
	JUMBLE_NODE(plan.qual);
	JUMBLE_BITMAPSET(plan.extParam);
	JUMBLE_BITMAPSET(plan.allParam);
	JUMBLE_NODE(resconstantqual);
}

static void
_jumbleProjectSet(YbJumbleState *jstate, Node *node)
{
	ProjectSet *expr = (ProjectSet *) node;

	JUMBLE_FIELD(plan.parallel_aware);
	JUMBLE_FIELD(plan.parallel_safe);
	JUMBLE_FIELD(plan.async_capable);
	JUMBLE_FIELD(plan.plan_node_id);
	JUMBLE_NODE(plan.targetlist);
	JUMBLE_NODE(plan.qual);
	JUMBLE_BITMAPSET(plan.extParam);
	JUMBLE_BITMAPSET(plan.allParam);
}

static void
_jumbleModifyTable(YbJumbleState *jstate, Node *node)
{
	ModifyTable *expr = (ModifyTable *) node;

	JUMBLE_FIELD(plan.parallel_aware);
	JUMBLE_FIELD(plan.parallel_safe);
	JUMBLE_FIELD(plan.async_capable);
	JUMBLE_FIELD(plan.plan_node_id);
	JUMBLE_NODE(plan.targetlist);
	JUMBLE_NODE(plan.qual);
	JUMBLE_BITMAPSET(plan.extParam);
	JUMBLE_BITMAPSET(plan.allParam);
	JUMBLE_FIELD(operation);
	JUMBLE_FIELD(canSetTag);
	JUMBLE_FIELD(nominalRelation);
	JUMBLE_FIELD(rootRelation);
	JUMBLE_FIELD(partColsUpdated);
	JUMBLE_NODE(resultRelations);
	JUMBLE_NODE(updateColnosLists);
	JUMBLE_NODE(withCheckOptionLists);
	/* JUMBLE_STRING(returningOldAlias); */
	/* JUMBLE_STRING(returningNewAlias); */
	JUMBLE_NODE(returningLists);
	JUMBLE_BITMAPSET(fdwDirectModifyPlans);
	JUMBLE_NODE(rowMarks);
	JUMBLE_FIELD(epqParam);
	JUMBLE_FIELD(onConflictAction);
	JUMBLE_NODE(arbiterIndexes);
	JUMBLE_NODE(onConflictSet);
	JUMBLE_NODE(onConflictCols);
	JUMBLE_NODE(onConflictWhere);
	JUMBLE_FIELD(exclRelRTI);
	JUMBLE_NODE(exclRelTlist);
	JUMBLE_NODE(mergeActionLists);
	/* JUMBLE_NODE(mergeJoinConditions); */
}

static void
_jumbleAppend(YbJumbleState *jstate, Node *node)
{
	Append	   *expr = (Append *) node;

	JUMBLE_FIELD(plan.parallel_aware);
	JUMBLE_FIELD(plan.parallel_safe);
	JUMBLE_FIELD(plan.async_capable);
	JUMBLE_FIELD(plan.plan_node_id);
	JUMBLE_NODE(plan.targetlist);
	JUMBLE_NODE(plan.qual);
	JUMBLE_BITMAPSET(plan.extParam);
	JUMBLE_BITMAPSET(plan.allParam);
	JUMBLE_BITMAPSET(apprelids);
	JUMBLE_FIELD(nasyncplans);
	JUMBLE_FIELD(first_partial_plan);
	/* JUMBLE_FIELD(part_prune_index); */
}

static void
_jumbleMergeAppend(YbJumbleState *jstate, Node *node)
{
	MergeAppend *expr = (MergeAppend *) node;

	JUMBLE_FIELD(plan.parallel_aware);
	JUMBLE_FIELD(plan.parallel_safe);
	JUMBLE_FIELD(plan.async_capable);
	JUMBLE_FIELD(plan.plan_node_id);
	JUMBLE_NODE(plan.targetlist);
	JUMBLE_NODE(plan.qual);
	JUMBLE_BITMAPSET(plan.extParam);
	JUMBLE_BITMAPSET(plan.allParam);
	JUMBLE_BITMAPSET(apprelids);
	JUMBLE_FIELD(numCols);
	/* JUMBLE_ARRAY(sortColIdx, expr->numCols); */
	_jumbleSortColIdx(jstate, expr->numCols, expr->sortColIdx);
	JUMBLE_ARRAY(sortOperators, expr->numCols);
	JUMBLE_ARRAY(collations, expr->numCols);
	JUMBLE_ARRAY(nullsFirst, expr->numCols);
	/* JUMBLE_FIELD(part_prune_index); */
}

static void
_jumbleRecursiveUnion(YbJumbleState *jstate, Node *node)
{
	RecursiveUnion *expr = (RecursiveUnion *) node;

	JUMBLE_FIELD(plan.parallel_aware);
	JUMBLE_FIELD(plan.parallel_safe);
	JUMBLE_FIELD(plan.async_capable);
	JUMBLE_FIELD(plan.plan_node_id);
	JUMBLE_NODE(plan.targetlist);
	JUMBLE_NODE(plan.qual);
	JUMBLE_BITMAPSET(plan.extParam);
	JUMBLE_BITMAPSET(plan.allParam);
	JUMBLE_FIELD(wtParam);
	JUMBLE_FIELD(numCols);
	JUMBLE_ARRAY(dupColIdx, expr->numCols);
	JUMBLE_ARRAY(dupOperators, expr->numCols);
	JUMBLE_ARRAY(dupCollations, expr->numCols);
}

static void
_jumbleBitmapAnd(YbJumbleState *jstate, Node *node)
{
	BitmapAnd  *expr = (BitmapAnd *) node;

	JUMBLE_FIELD(plan.parallel_aware);
	JUMBLE_FIELD(plan.parallel_safe);
	JUMBLE_FIELD(plan.async_capable);
	JUMBLE_FIELD(plan.plan_node_id);
	JUMBLE_NODE(plan.targetlist);
	JUMBLE_NODE(plan.qual);
	JUMBLE_BITMAPSET(plan.extParam);
	JUMBLE_BITMAPSET(plan.allParam);
}

static void
_jumbleBitmapOr(YbJumbleState *jstate, Node *node)
{
	BitmapOr   *expr = (BitmapOr *) node;

	JUMBLE_FIELD(plan.parallel_aware);
	JUMBLE_FIELD(plan.parallel_safe);
	JUMBLE_FIELD(plan.async_capable);
	JUMBLE_FIELD(plan.plan_node_id);
	JUMBLE_NODE(plan.targetlist);
	JUMBLE_NODE(plan.qual);
	JUMBLE_BITMAPSET(plan.extParam);
	JUMBLE_BITMAPSET(plan.allParam);
	JUMBLE_FIELD(isshared);
}

static void
_jumbleSeqScan(YbJumbleState *jstate, Node *node)
{
	SeqScan    *expr = (SeqScan *) node;

	JUMBLE_FIELD(scan.plan.parallel_aware);
	JUMBLE_FIELD(scan.plan.parallel_safe);
	JUMBLE_FIELD(scan.plan.async_capable);
	JUMBLE_FIELD(scan.plan.plan_node_id);
	JUMBLE_NODE(scan.plan.targetlist);
	JUMBLE_NODE(scan.plan.qual);
	JUMBLE_BITMAPSET(scan.plan.extParam);
	JUMBLE_BITMAPSET(scan.plan.allParam);
	_jumbleRtIndex(jstate, expr->scan.scanrelid);
	/* JUMBLE_FIELD(scan.scanrelid); */
}

static void
_jumbleSampleScan(YbJumbleState *jstate, Node *node)
{
	SampleScan *expr = (SampleScan *) node;

	JUMBLE_FIELD(scan.plan.parallel_aware);
	JUMBLE_FIELD(scan.plan.parallel_safe);
	JUMBLE_FIELD(scan.plan.async_capable);
	JUMBLE_FIELD(scan.plan.plan_node_id);
	JUMBLE_NODE(scan.plan.targetlist);
	JUMBLE_NODE(scan.plan.qual);
	JUMBLE_BITMAPSET(scan.plan.extParam);
	JUMBLE_BITMAPSET(scan.plan.allParam);
	_jumbleRtIndex(jstate, expr->scan.scanrelid);
	/* JUMBLE_FIELD(scan.scanrelid); */
	JUMBLE_NODE(tablesample);
}

static void
_jumbleYbPushdownExprs(YbJumbleState *jstate, YbPushdownExprs *expr)
{
	/* JUMBLE_NODE(quals); */
	YbJumbleList(jstate, expr->quals, true);
	JUMBLE_NODE(colrefs);
}

static void
_jumbleIndexScan(YbJumbleState *jstate, Node *node)
{
	IndexScan  *expr = (IndexScan *) node;

	JUMBLE_FIELD(scan.plan.parallel_aware);
	JUMBLE_FIELD(scan.plan.parallel_safe);
	JUMBLE_FIELD(scan.plan.async_capable);
	JUMBLE_FIELD(scan.plan.plan_node_id);
	JUMBLE_NODE(scan.plan.targetlist);
	JUMBLE_NODE(scan.plan.qual);
	JUMBLE_BITMAPSET(scan.plan.extParam);
	JUMBLE_BITMAPSET(scan.plan.allParam);
	_jumbleRtIndex(jstate, expr->scan.scanrelid);
	/* JUMBLE_FIELD(scan.scanrelid); */

	if (jstate->ybUseNames)
	{
		char	   *indexName = expr->scan.ybScannedObjectName;

		Assert(indexName != NULL);

		if (indexName != NULL)
			AppendJumble(jstate, (const unsigned char *) indexName, strlen(indexName) + 1);
		else
			AppendJumbleNull(jstate);
	}
	else
		JUMBLE_FIELD(indexid);

	JUMBLE_NODE(indexqual);
	JUMBLE_NODE(indexqualorig);
	JUMBLE_NODE(indexorderby);
	JUMBLE_NODE(indexorderbyorig);
	JUMBLE_NODE(indexorderbyops);
	JUMBLE_FIELD(indexorderdir);

	_jumbleYbPushdownExprs(jstate, &(expr->yb_idx_pushdown));
	_jumbleYbPushdownExprs(jstate, &(expr->yb_rel_pushdown));
}

static void
_jumbleIndexOnlyScan(YbJumbleState *jstate, Node *node)
{
	IndexOnlyScan *expr = (IndexOnlyScan *) node;

	JUMBLE_FIELD(scan.plan.parallel_aware);
	JUMBLE_FIELD(scan.plan.parallel_safe);
	JUMBLE_FIELD(scan.plan.async_capable);
	JUMBLE_FIELD(scan.plan.plan_node_id);
	JUMBLE_NODE(scan.plan.targetlist);
	JUMBLE_NODE(scan.plan.qual);
	JUMBLE_BITMAPSET(scan.plan.extParam);
	JUMBLE_BITMAPSET(scan.plan.allParam);
	_jumbleRtIndex(jstate, expr->scan.scanrelid);
	/* JUMBLE_FIELD(scan.scanrelid); */

	if (jstate->ybUseNames)
	{
		char	   *indexName = expr->scan.ybScannedObjectName;

		Assert(indexName != NULL);

		if (indexName != NULL)
			AppendJumble(jstate, (const unsigned char *) indexName, strlen(indexName) + 1);
		else
			AppendJumbleNull(jstate);
	}
	else
		JUMBLE_FIELD(indexid);


	JUMBLE_NODE(indexqual);
	JUMBLE_NODE(recheckqual);
	JUMBLE_NODE(indexorderby);

	/*
	 * 'indextlist' describes the columns in the index, even if they are not referenced.
	 * Skip this since we already identify the index itself.
	 */
	/* JUMBLE_NODE(indextlist); */
	JUMBLE_FIELD(indexorderdir);

	_jumbleYbPushdownExprs(jstate, &(expr->yb_pushdown));
}

static void
_jumbleBitmapIndexScan(YbJumbleState *jstate, Node *node)
{
	BitmapIndexScan *expr = (BitmapIndexScan *) node;

	JUMBLE_FIELD(scan.plan.parallel_aware);
	JUMBLE_FIELD(scan.plan.parallel_safe);
	JUMBLE_FIELD(scan.plan.async_capable);
	JUMBLE_FIELD(scan.plan.plan_node_id);
	JUMBLE_NODE(scan.plan.targetlist);
	JUMBLE_NODE(scan.plan.qual);
	JUMBLE_BITMAPSET(scan.plan.extParam);
	JUMBLE_BITMAPSET(scan.plan.allParam);
	_jumbleRtIndex(jstate, expr->scan.scanrelid);
	/* JUMBLE_FIELD(scan.scanrelid); */

	if (jstate->ybUseNames)
	{
		char	   *indexName = expr->scan.ybScannedObjectName;

		Assert(indexName != NULL);

		if (indexName != NULL)
			AppendJumble(jstate, (const unsigned char *) indexName, strlen(indexName) + 1);
		else
			AppendJumbleNull(jstate);
	}
	else
		JUMBLE_FIELD(indexid);

	JUMBLE_FIELD(isshared);
	JUMBLE_NODE(indexqual);
	JUMBLE_NODE(indexqualorig);
}

static void
_jumbleBitmapHeapScan(YbJumbleState *jstate, Node *node)
{
	BitmapHeapScan *expr = (BitmapHeapScan *) node;

	JUMBLE_FIELD(scan.plan.parallel_aware);
	JUMBLE_FIELD(scan.plan.parallel_safe);
	JUMBLE_FIELD(scan.plan.async_capable);
	JUMBLE_FIELD(scan.plan.plan_node_id);
	JUMBLE_NODE(scan.plan.targetlist);
	JUMBLE_NODE(scan.plan.qual);
	JUMBLE_BITMAPSET(scan.plan.extParam);
	JUMBLE_BITMAPSET(scan.plan.allParam);
	_jumbleRtIndex(jstate, expr->scan.scanrelid);
	/* JUMBLE_FIELD(scan.scanrelid); */
	JUMBLE_NODE(bitmapqualorig);
}

static void
_jumbleTidScan(YbJumbleState *jstate, Node *node)
{
	TidScan    *expr = (TidScan *) node;

	JUMBLE_FIELD(scan.plan.parallel_aware);
	JUMBLE_FIELD(scan.plan.parallel_safe);
	JUMBLE_FIELD(scan.plan.async_capable);
	JUMBLE_FIELD(scan.plan.plan_node_id);
	JUMBLE_NODE(scan.plan.targetlist);
	JUMBLE_NODE(scan.plan.qual);
	JUMBLE_BITMAPSET(scan.plan.extParam);
	JUMBLE_BITMAPSET(scan.plan.allParam);
	_jumbleRtIndex(jstate, expr->scan.scanrelid);
	/* JUMBLE_FIELD(scan.scanrelid); */
	JUMBLE_NODE(tidquals);

	_jumbleYbPushdownExprs(jstate, &(expr->yb_rel_pushdown));
}

static void
_jumbleTidRangeScan(YbJumbleState *jstate, Node *node)
{
	TidRangeScan *expr = (TidRangeScan *) node;

	JUMBLE_FIELD(scan.plan.parallel_aware);
	JUMBLE_FIELD(scan.plan.parallel_safe);
	JUMBLE_FIELD(scan.plan.async_capable);
	JUMBLE_FIELD(scan.plan.plan_node_id);
	JUMBLE_NODE(scan.plan.targetlist);
	JUMBLE_NODE(scan.plan.qual);
	JUMBLE_BITMAPSET(scan.plan.extParam);
	JUMBLE_BITMAPSET(scan.plan.allParam);
	_jumbleRtIndex(jstate, expr->scan.scanrelid);
	/* JUMBLE_FIELD(scan.scanrelid); */
	JUMBLE_NODE(tidrangequals);
}

static void
_jumbleSubqueryScan(YbJumbleState *jstate, Node *node)
{
	SubqueryScan *expr = (SubqueryScan *) node;

	JUMBLE_FIELD(scan.plan.parallel_aware);
	JUMBLE_FIELD(scan.plan.parallel_safe);
	JUMBLE_FIELD(scan.plan.async_capable);
	JUMBLE_FIELD(scan.plan.plan_node_id);
	JUMBLE_NODE(scan.plan.targetlist);
	JUMBLE_NODE(scan.plan.qual);
	JUMBLE_BITMAPSET(scan.plan.extParam);
	JUMBLE_BITMAPSET(scan.plan.allParam);
	_jumbleRtIndex(jstate, expr->scan.scanrelid);
	/* JUMBLE_FIELD(scan.scanrelid); */
	JUMBLE_FIELD(scanstatus);
}

static void
_jumbleFunctionScan(YbJumbleState *jstate, Node *node)
{
	FunctionScan *expr = (FunctionScan *) node;

	JUMBLE_FIELD(scan.plan.parallel_aware);
	JUMBLE_FIELD(scan.plan.parallel_safe);
	JUMBLE_FIELD(scan.plan.async_capable);
	JUMBLE_FIELD(scan.plan.plan_node_id);
	JUMBLE_NODE(scan.plan.targetlist);
	JUMBLE_NODE(scan.plan.qual);
	JUMBLE_BITMAPSET(scan.plan.extParam);
	JUMBLE_BITMAPSET(scan.plan.allParam);
	_jumbleRtIndex(jstate, expr->scan.scanrelid);
	/* JUMBLE_FIELD(scan.scanrelid); */
	JUMBLE_NODE(functions);
	JUMBLE_FIELD(funcordinality);
}

static void
_jumbleValuesScan(YbJumbleState *jstate, Node *node)
{
	ValuesScan *expr = (ValuesScan *) node;

	JUMBLE_FIELD(scan.plan.parallel_aware);
	JUMBLE_FIELD(scan.plan.parallel_safe);
	JUMBLE_FIELD(scan.plan.async_capable);
	JUMBLE_FIELD(scan.plan.plan_node_id);
	JUMBLE_NODE(scan.plan.targetlist);
	JUMBLE_NODE(scan.plan.qual);
	JUMBLE_BITMAPSET(scan.plan.extParam);
	JUMBLE_BITMAPSET(scan.plan.allParam);
	_jumbleRtIndex(jstate, expr->scan.scanrelid);
	/* JUMBLE_FIELD(scan.scanrelid); */
	JUMBLE_NODE(values_lists);
}

static void
_jumbleTableFuncScan(YbJumbleState *jstate, Node *node)
{
	TableFuncScan *expr = (TableFuncScan *) node;

	JUMBLE_FIELD(scan.plan.parallel_aware);
	JUMBLE_FIELD(scan.plan.parallel_safe);
	JUMBLE_FIELD(scan.plan.async_capable);
	JUMBLE_FIELD(scan.plan.plan_node_id);
	JUMBLE_NODE(scan.plan.targetlist);
	JUMBLE_NODE(scan.plan.qual);
	JUMBLE_BITMAPSET(scan.plan.extParam);
	JUMBLE_BITMAPSET(scan.plan.allParam);
	_jumbleRtIndex(jstate, expr->scan.scanrelid);
	/* JUMBLE_FIELD(scan.scanrelid); */
	JUMBLE_NODE(tablefunc);
}

static void
_jumbleCteScan(YbJumbleState *jstate, Node *node)
{
	CteScan    *expr = (CteScan *) node;

	JUMBLE_FIELD(scan.plan.parallel_aware);
	JUMBLE_FIELD(scan.plan.parallel_safe);
	JUMBLE_FIELD(scan.plan.async_capable);
	JUMBLE_FIELD(scan.plan.plan_node_id);
	JUMBLE_NODE(scan.plan.targetlist);
	JUMBLE_NODE(scan.plan.qual);
	JUMBLE_BITMAPSET(scan.plan.extParam);
	JUMBLE_BITMAPSET(scan.plan.allParam);
	_jumbleRtIndex(jstate, expr->scan.scanrelid);
	/* JUMBLE_FIELD(scan.scanrelid); */
	JUMBLE_FIELD(ctePlanId);
	JUMBLE_FIELD(cteParam);
}

static void
_jumbleNamedTuplestoreScan(YbJumbleState *jstate, Node *node)
{
	NamedTuplestoreScan *expr = (NamedTuplestoreScan *) node;

	JUMBLE_FIELD(scan.plan.parallel_aware);
	JUMBLE_FIELD(scan.plan.parallel_safe);
	JUMBLE_FIELD(scan.plan.async_capable);
	JUMBLE_FIELD(scan.plan.plan_node_id);
	JUMBLE_NODE(scan.plan.targetlist);
	JUMBLE_NODE(scan.plan.qual);
	JUMBLE_BITMAPSET(scan.plan.extParam);
	JUMBLE_BITMAPSET(scan.plan.allParam);
	_jumbleRtIndex(jstate, expr->scan.scanrelid);
	/* JUMBLE_FIELD(scan.scanrelid); */
	JUMBLE_STRING(enrname);
}

static void
_jumbleWorkTableScan(YbJumbleState *jstate, Node *node)
{
	WorkTableScan *expr = (WorkTableScan *) node;

	JUMBLE_FIELD(scan.plan.parallel_aware);
	JUMBLE_FIELD(scan.plan.parallel_safe);
	JUMBLE_FIELD(scan.plan.async_capable);
	JUMBLE_FIELD(scan.plan.plan_node_id);
	JUMBLE_NODE(scan.plan.targetlist);
	JUMBLE_NODE(scan.plan.qual);
	JUMBLE_BITMAPSET(scan.plan.extParam);
	JUMBLE_BITMAPSET(scan.plan.allParam);
	_jumbleRtIndex(jstate, expr->scan.scanrelid);
	/* JUMBLE_FIELD(scan.scanrelid); */
	JUMBLE_FIELD(wtParam);
}

static void
_jumbleForeignScan(YbJumbleState *jstate, Node *node)
{
	ForeignScan *expr = (ForeignScan *) node;

	JUMBLE_FIELD(scan.plan.parallel_aware);
	JUMBLE_FIELD(scan.plan.parallel_safe);
	JUMBLE_FIELD(scan.plan.async_capable);
	JUMBLE_FIELD(scan.plan.plan_node_id);
	JUMBLE_NODE(scan.plan.targetlist);
	JUMBLE_NODE(scan.plan.qual);
	JUMBLE_BITMAPSET(scan.plan.extParam);
	JUMBLE_BITMAPSET(scan.plan.allParam);
	_jumbleRtIndex(jstate, expr->scan.scanrelid);
	/* JUMBLE_FIELD(scan.scanrelid); */
	JUMBLE_FIELD(operation);
	JUMBLE_FIELD(resultRelation);
	/* JUMBLE_FIELD(checkAsUser); */
	JUMBLE_FIELD(fs_server);
	JUMBLE_NODE(fdw_exprs);
	JUMBLE_NODE(fdw_scan_tlist);
	JUMBLE_NODE(fdw_recheck_quals);
	JUMBLE_BITMAPSET(fs_relids);
	/* JUMBLE_BITMAPSET(fs_base_relids); */
	JUMBLE_FIELD(fsSystemCol);
}

static void
_jumbleCustomScan(YbJumbleState *jstate, Node *node)
{
	CustomScan *expr = (CustomScan *) node;

	JUMBLE_FIELD(scan.plan.parallel_aware);
	JUMBLE_FIELD(scan.plan.parallel_safe);
	JUMBLE_FIELD(scan.plan.async_capable);
	JUMBLE_FIELD(scan.plan.plan_node_id);
	JUMBLE_NODE(scan.plan.targetlist);
	JUMBLE_NODE(scan.plan.qual);
	JUMBLE_BITMAPSET(scan.plan.extParam);
	JUMBLE_BITMAPSET(scan.plan.allParam);
	_jumbleRtIndex(jstate, expr->scan.scanrelid);
	/* JUMBLE_FIELD(scan.scanrelid); */
	JUMBLE_FIELD(flags);
	JUMBLE_NODE(custom_plans);
	JUMBLE_NODE(custom_exprs);
	JUMBLE_NODE(custom_scan_tlist);
	JUMBLE_BITMAPSET(custom_relids);
}

static void
_jumbleNestLoop(YbJumbleState *jstate, Node *node)
{
	NestLoop   *expr = (NestLoop *) node;

	JUMBLE_FIELD(join.plan.parallel_aware);
	JUMBLE_FIELD(join.plan.parallel_safe);
	JUMBLE_FIELD(join.plan.async_capable);
	JUMBLE_FIELD(join.plan.plan_node_id);
	JUMBLE_NODE(join.plan.targetlist);
	JUMBLE_NODE(join.plan.qual);
	JUMBLE_BITMAPSET(join.plan.extParam);
	JUMBLE_BITMAPSET(join.plan.allParam);
	JUMBLE_FIELD(join.jointype);
	JUMBLE_FIELD(join.inner_unique);
	/* JUMBLE_NODE(join.joinqual); */
	YbJumbleList(jstate, expr->join.joinqual, true);
	JUMBLE_NODE(nestParams);
}

static void
_jumbleNestLoopParam(YbJumbleState *jstate, Node *node)
{
	NestLoopParam *expr = (NestLoopParam *) node;

	JUMBLE_FIELD(paramno);
	JUMBLE_NODE(paramval);
}

static void
_jumbleMergeJoin(YbJumbleState *jstate, Node *node)
{
	MergeJoin  *expr = (MergeJoin *) node;

	JUMBLE_FIELD(join.plan.parallel_aware);
	JUMBLE_FIELD(join.plan.parallel_safe);
	JUMBLE_FIELD(join.plan.async_capable);
	JUMBLE_FIELD(join.plan.plan_node_id);
	JUMBLE_NODE(join.plan.targetlist);
	JUMBLE_NODE(join.plan.qual);
	JUMBLE_BITMAPSET(join.plan.extParam);
	JUMBLE_BITMAPSET(join.plan.allParam);
	JUMBLE_FIELD(join.jointype);
	JUMBLE_FIELD(join.inner_unique);
	/* JUMBLE_NODE(join.joinqual); */
	YbJumbleList(jstate, expr->join.joinqual, true);
	JUMBLE_FIELD(skip_mark_restore);
	JUMBLE_NODE(mergeclauses);
	JUMBLE_ARRAY(mergeFamilies, list_length(expr->mergeclauses));
	JUMBLE_ARRAY(mergeCollations, list_length(expr->mergeclauses));
	/* JUMBLE_ARRAY(mergeReversals, list_length(expr->mergeclauses)); */
	JUMBLE_ARRAY(mergeNullsFirst, list_length(expr->mergeclauses));
}

static void
_jumbleHashJoin(YbJumbleState *jstate, Node *node)
{
	HashJoin   *expr = (HashJoin *) node;

	JUMBLE_FIELD(join.plan.parallel_aware);
	JUMBLE_FIELD(join.plan.parallel_safe);
	JUMBLE_FIELD(join.plan.async_capable);
	JUMBLE_FIELD(join.plan.plan_node_id);
	JUMBLE_NODE(join.plan.targetlist);
	JUMBLE_NODE(join.plan.qual);
	JUMBLE_BITMAPSET(join.plan.extParam);
	JUMBLE_BITMAPSET(join.plan.allParam);
	JUMBLE_FIELD(join.jointype);
	JUMBLE_FIELD(join.inner_unique);
	/* JUMBLE_NODE(join.joinqual); */
	YbJumbleList(jstate, expr->join.joinqual, true);
	JUMBLE_NODE(hashclauses);
	JUMBLE_NODE(hashoperators);
	JUMBLE_NODE(hashcollations);
	JUMBLE_NODE(hashkeys);
}

static void
_jumbleMaterial(YbJumbleState *jstate, Node *node)
{
	Material   *expr = (Material *) node;

	JUMBLE_FIELD(plan.parallel_aware);
	JUMBLE_FIELD(plan.parallel_safe);
	JUMBLE_FIELD(plan.async_capable);
	JUMBLE_FIELD(plan.plan_node_id);
	JUMBLE_NODE(plan.targetlist);
	JUMBLE_NODE(plan.qual);
	JUMBLE_BITMAPSET(plan.extParam);
	JUMBLE_BITMAPSET(plan.allParam);
}

static void
_jumbleMemoize(YbJumbleState *jstate, Node *node)
{
	Memoize    *expr = (Memoize *) node;

	JUMBLE_FIELD(plan.parallel_aware);
	JUMBLE_FIELD(plan.parallel_safe);
	JUMBLE_FIELD(plan.async_capable);
	JUMBLE_FIELD(plan.plan_node_id);
	JUMBLE_NODE(plan.targetlist);
	JUMBLE_NODE(plan.qual);
	JUMBLE_BITMAPSET(plan.extParam);
	JUMBLE_BITMAPSET(plan.allParam);
	JUMBLE_FIELD(numKeys);
	JUMBLE_ARRAY(hashOperators, expr->numKeys);
	JUMBLE_ARRAY(collations, expr->numKeys);
	JUMBLE_NODE(param_exprs);
	JUMBLE_FIELD(singlerow);
	JUMBLE_FIELD(binary_mode);
	JUMBLE_BITMAPSET(keyparamids);
}

static void
_jumbleSortColIdx(YbJumbleState *jstate, int numCols, AttrNumber *sortColIdx)
{
	if (numCols > 0)
	{
		if (jstate->ybTargetList != NULL)
		{
			int			i;

			for (i = 0; i < numCols; ++i)
			{
				TargetEntry *tle = get_tle_by_resno(jstate->ybTargetList, sortColIdx[i]);

				if (tle != NULL)
					_jumbleTargetEntry(jstate, (Node *) tle);
			}
		}
		else
			AppendJumble(jstate, (const unsigned char *) sortColIdx,
						 sizeof(AttrNumber) * numCols);
	}
}

static void
_jumbleSort(YbJumbleState *jstate, Node *node)
{
	Sort	   *expr = (Sort *) node;

	JUMBLE_FIELD(plan.parallel_aware);
	JUMBLE_FIELD(plan.parallel_safe);
	JUMBLE_FIELD(plan.async_capable);
	JUMBLE_FIELD(plan.plan_node_id);
	JUMBLE_NODE(plan.targetlist);
	JUMBLE_NODE(plan.qual);
	JUMBLE_BITMAPSET(plan.extParam);
	JUMBLE_BITMAPSET(plan.allParam);
	JUMBLE_FIELD(numCols);
	/* JUMBLE_ARRAY(sortColIdx, expr->numCols); */
	_jumbleSortColIdx(jstate, expr->numCols, expr->sortColIdx);
	JUMBLE_ARRAY(sortOperators, expr->numCols);
	JUMBLE_ARRAY(collations, expr->numCols);
	JUMBLE_ARRAY(nullsFirst, expr->numCols);
}

static void
_jumbleIncrementalSort(YbJumbleState *jstate, Node *node)
{
	IncrementalSort *expr = (IncrementalSort *) node;

	JUMBLE_FIELD(sort.plan.parallel_aware);
	JUMBLE_FIELD(sort.plan.parallel_safe);
	JUMBLE_FIELD(sort.plan.async_capable);
	JUMBLE_FIELD(sort.plan.plan_node_id);
	JUMBLE_NODE(sort.plan.targetlist);
	JUMBLE_NODE(sort.plan.qual);
	JUMBLE_BITMAPSET(sort.plan.extParam);
	JUMBLE_BITMAPSET(sort.plan.allParam);
	JUMBLE_FIELD(sort.numCols);
	/* JUMBLE_ARRAY(sort.sortColIdx, expr->sort.numCols); */
	_jumbleSortColIdx(jstate, expr->sort.numCols, expr->sort.sortColIdx);
	JUMBLE_ARRAY(sort.sortOperators, expr->sort.numCols);
	JUMBLE_ARRAY(sort.collations, expr->sort.numCols);
	JUMBLE_ARRAY(sort.nullsFirst, expr->sort.numCols);
	JUMBLE_FIELD(nPresortedCols);
}

static void
_jumbleGroup(YbJumbleState *jstate, Node *node)
{
	Group	   *expr = (Group *) node;

	JUMBLE_FIELD(plan.parallel_aware);
	JUMBLE_FIELD(plan.parallel_safe);
	JUMBLE_FIELD(plan.async_capable);
	JUMBLE_FIELD(plan.plan_node_id);
	JUMBLE_NODE(plan.targetlist);
	JUMBLE_NODE(plan.qual);
	JUMBLE_BITMAPSET(plan.extParam);
	JUMBLE_BITMAPSET(plan.allParam);
	JUMBLE_FIELD(numCols);
	JUMBLE_ARRAY(grpColIdx, expr->numCols);
	JUMBLE_ARRAY(grpOperators, expr->numCols);
	JUMBLE_ARRAY(grpCollations, expr->numCols);
}

static void
_jumbleAgg(YbJumbleState *jstate, Node *node)
{
	Agg		   *expr = (Agg *) node;

	JUMBLE_FIELD(plan.parallel_aware);
	JUMBLE_FIELD(plan.parallel_safe);
	JUMBLE_FIELD(plan.async_capable);
	JUMBLE_FIELD(plan.plan_node_id);
	JUMBLE_NODE(plan.targetlist);
	JUMBLE_NODE(plan.qual);
	JUMBLE_BITMAPSET(plan.extParam);
	JUMBLE_BITMAPSET(plan.allParam);
	JUMBLE_FIELD(aggstrategy);
	JUMBLE_FIELD(aggsplit);
	JUMBLE_FIELD(numCols);
	JUMBLE_ARRAY(grpColIdx, expr->numCols);
	JUMBLE_ARRAY(grpOperators, expr->numCols);
	JUMBLE_ARRAY(grpCollations, expr->numCols);
	JUMBLE_FIELD(transitionSpace);
	JUMBLE_BITMAPSET(aggParams);
	JUMBLE_NODE(groupingSets);
	JUMBLE_NODE(chain);
}

static void
_jumbleWindowAgg(YbJumbleState *jstate, Node *node)
{
	WindowAgg  *expr = (WindowAgg *) node;

	JUMBLE_FIELD(plan.parallel_aware);
	JUMBLE_FIELD(plan.parallel_safe);
	JUMBLE_FIELD(plan.async_capable);
	JUMBLE_FIELD(plan.plan_node_id);
	JUMBLE_NODE(plan.targetlist);
	JUMBLE_NODE(plan.qual);
	JUMBLE_BITMAPSET(plan.extParam);
	JUMBLE_BITMAPSET(plan.allParam);
	/* JUMBLE_STRING(winname); */
	JUMBLE_FIELD(winref);
	JUMBLE_FIELD(partNumCols);
	JUMBLE_ARRAY(partColIdx, expr->partNumCols);
	JUMBLE_ARRAY(partOperators, expr->partNumCols);
	JUMBLE_ARRAY(partCollations, expr->partNumCols);
	JUMBLE_FIELD(ordNumCols);
	JUMBLE_ARRAY(ordColIdx, expr->ordNumCols);
	JUMBLE_ARRAY(ordOperators, expr->ordNumCols);
	JUMBLE_ARRAY(ordCollations, expr->ordNumCols);
	JUMBLE_FIELD(frameOptions);
	JUMBLE_NODE(startOffset);
	JUMBLE_NODE(endOffset);
	JUMBLE_NODE(runCondition);
	JUMBLE_NODE(runConditionOrig);
	JUMBLE_FIELD(startInRangeFunc);
	JUMBLE_FIELD(endInRangeFunc);
	JUMBLE_FIELD(inRangeColl);
	JUMBLE_FIELD(inRangeAsc);
	JUMBLE_FIELD(inRangeNullsFirst);
	JUMBLE_FIELD(topWindow);
}

static void
_jumbleUnique(YbJumbleState *jstate, Node *node)
{
	Unique	   *expr = (Unique *) node;

	JUMBLE_FIELD(plan.parallel_aware);
	JUMBLE_FIELD(plan.parallel_safe);
	JUMBLE_FIELD(plan.async_capable);
	JUMBLE_FIELD(plan.plan_node_id);
	JUMBLE_NODE(plan.targetlist);
	JUMBLE_NODE(plan.qual);
	JUMBLE_BITMAPSET(plan.extParam);
	JUMBLE_BITMAPSET(plan.allParam);
	JUMBLE_FIELD(numCols);
	JUMBLE_ARRAY(uniqColIdx, expr->numCols);
	JUMBLE_ARRAY(uniqOperators, expr->numCols);
	JUMBLE_ARRAY(uniqCollations, expr->numCols);
}

static void
_jumbleGather(YbJumbleState *jstate, Node *node)
{
	Gather	   *expr = (Gather *) node;

	JUMBLE_FIELD(plan.parallel_aware);
	JUMBLE_FIELD(plan.parallel_safe);
	JUMBLE_FIELD(plan.async_capable);
	JUMBLE_FIELD(plan.plan_node_id);
	JUMBLE_NODE(plan.targetlist);
	JUMBLE_NODE(plan.qual);
	JUMBLE_BITMAPSET(plan.extParam);
	JUMBLE_BITMAPSET(plan.allParam);
	JUMBLE_FIELD(num_workers);
	JUMBLE_FIELD(rescan_param);
	JUMBLE_FIELD(single_copy);
	JUMBLE_FIELD(invisible);
	JUMBLE_BITMAPSET(initParam);
}

static void
_jumbleGatherMerge(YbJumbleState *jstate, Node *node)
{
	GatherMerge *expr = (GatherMerge *) node;

	JUMBLE_FIELD(plan.parallel_aware);
	JUMBLE_FIELD(plan.parallel_safe);
	JUMBLE_FIELD(plan.async_capable);
	JUMBLE_FIELD(plan.plan_node_id);
	JUMBLE_NODE(plan.targetlist);
	JUMBLE_NODE(plan.qual);
	JUMBLE_BITMAPSET(plan.extParam);
	JUMBLE_BITMAPSET(plan.allParam);
	JUMBLE_FIELD(num_workers);
	JUMBLE_FIELD(rescan_param);
	JUMBLE_FIELD(numCols);
	/* JUMBLE_ARRAY(sortColIdx, expr->numCols); */
	_jumbleSortColIdx(jstate, expr->numCols, expr->sortColIdx);
	JUMBLE_ARRAY(sortOperators, expr->numCols);
	JUMBLE_ARRAY(collations, expr->numCols);
	JUMBLE_ARRAY(nullsFirst, expr->numCols);
	JUMBLE_BITMAPSET(initParam);
}

static void
_jumbleHash(YbJumbleState *jstate, Node *node)
{
	Hash	   *expr = (Hash *) node;

	JUMBLE_FIELD(plan.parallel_aware);
	JUMBLE_FIELD(plan.parallel_safe);
	JUMBLE_FIELD(plan.async_capable);
	JUMBLE_FIELD(plan.plan_node_id);
	JUMBLE_NODE(plan.targetlist);
	JUMBLE_NODE(plan.qual);
	JUMBLE_BITMAPSET(plan.extParam);
	JUMBLE_BITMAPSET(plan.allParam);
	JUMBLE_NODE(hashkeys);
	/* JUMBLE_FIELD(skewTable); */

	if (expr->skewTable > 0)
	{
		char	   *skewTableName = expr->ybSkewTableName;

		if (skewTableName == NULL)
		{
			skewTableName = get_rel_name(expr->skewTable);
			expr->ybSkewTableName = skewTableName;
		}

		if (skewTableName != NULL)
			AppendJumble(jstate, (const unsigned char *) skewTableName,
						 strlen(skewTableName) + 1);
		else
			AppendJumbleNull(jstate);
	}

	JUMBLE_FIELD(skewColumn);
	JUMBLE_FIELD(skewInherit);
}

static void
_jumbleSetOp(YbJumbleState *jstate, Node *node)
{
	SetOp	   *expr = (SetOp *) node;

	JUMBLE_FIELD(plan.parallel_aware);
	JUMBLE_FIELD(plan.parallel_safe);
	JUMBLE_FIELD(plan.async_capable);
	JUMBLE_FIELD(plan.plan_node_id);
	JUMBLE_NODE(plan.targetlist);
	JUMBLE_NODE(plan.qual);
	JUMBLE_BITMAPSET(plan.extParam);
	JUMBLE_BITMAPSET(plan.allParam);
	JUMBLE_FIELD(cmd);
	JUMBLE_FIELD(strategy);
	JUMBLE_FIELD(numCols);
	/* JUMBLE_ARRAY(cmpColIdx, expr->numCols); */
	/* JUMBLE_ARRAY(cmpOperators, expr->numCols); */
	/* JUMBLE_ARRAY(cmpCollations, expr->numCols); */
	/* JUMBLE_ARRAY(cmpNullsFirst, expr->numCols); */
}

static void
_jumbleLockRows(YbJumbleState *jstate, Node *node)
{
	LockRows   *expr = (LockRows *) node;

	JUMBLE_FIELD(plan.parallel_aware);
	JUMBLE_FIELD(plan.parallel_safe);
	JUMBLE_FIELD(plan.async_capable);
	JUMBLE_FIELD(plan.plan_node_id);
	JUMBLE_NODE(plan.targetlist);
	JUMBLE_NODE(plan.qual);
	JUMBLE_BITMAPSET(plan.extParam);
	JUMBLE_BITMAPSET(plan.allParam);
	JUMBLE_NODE(rowMarks);
	JUMBLE_FIELD(epqParam);
}

static void
_jumbleLimit(YbJumbleState *jstate, Node *node)
{
	Limit	   *expr = (Limit *) node;

	JUMBLE_FIELD(plan.parallel_aware);
	JUMBLE_FIELD(plan.parallel_safe);
	JUMBLE_FIELD(plan.async_capable);
	JUMBLE_FIELD(plan.plan_node_id);
	JUMBLE_NODE(plan.targetlist);
	JUMBLE_NODE(plan.qual);
	JUMBLE_BITMAPSET(plan.extParam);
	JUMBLE_BITMAPSET(plan.allParam);
	JUMBLE_NODE(limitOffset);
	JUMBLE_NODE(limitCount);
	JUMBLE_FIELD(limitOption);
	JUMBLE_FIELD(uniqNumCols);
	JUMBLE_ARRAY(uniqColIdx, expr->uniqNumCols);
	JUMBLE_ARRAY(uniqOperators, expr->uniqNumCols);
	JUMBLE_ARRAY(uniqCollations, expr->uniqNumCols);
}

static void
_jumbleExtensibleNode(YbJumbleState *jstate, Node *node)
{
	ExtensibleNode *expr = (ExtensibleNode *) node;

	JUMBLE_STRING(extnodename);
}

static void
_jumbleInteger(YbJumbleState *jstate, Node *node)
{
	Integer    *expr = (Integer *) node;

	JUMBLE_FIELD(ival);
}

static void
_jumbleFloat(YbJumbleState *jstate, Node *node)
{
	Float	   *expr = (Float *) node;

	JUMBLE_STRING(fval);
}

static void
_jumbleBoolean(YbJumbleState *jstate, Node *node)
{
	Boolean    *expr = (Boolean *) node;

	JUMBLE_FIELD(boolval);
}

static void
_jumbleString(YbJumbleState *jstate, Node *node)
{
	String	   *expr = (String *) node;

	JUMBLE_STRING(sval);
}

static void
_jumbleBitString(YbJumbleState *jstate, Node *node)
{
	BitString  *expr = (BitString *) node;

	JUMBLE_STRING(bsval);
}

static void
_jumblePlanRowMark(YbJumbleState *jstate, Node *node)
{
	PlanRowMark *expr = (PlanRowMark *) node;

	JUMBLE_FIELD(rti);
	JUMBLE_FIELD(prti);
	JUMBLE_FIELD(rowmarkId);
	JUMBLE_FIELD(markType);
	JUMBLE_FIELD(allMarkTypes);
}

/*
 * YB
 */
static void
_jumbleYbBitmapTablePath(YbJumbleState *jstate, Node *node)
{
	YbBitmapTablePath *expr = (YbBitmapTablePath *) node;

	YbJumbleNode(jstate, (Node *) expr->bitmapqual);
}

static void
_jumbleYbSeqScan(YbJumbleState *jstate, Node *node)
{
	YbSeqScan  *expr = (YbSeqScan *) node;

	JUMBLE_FIELD(scan.plan.parallel_aware);
	JUMBLE_FIELD(scan.plan.parallel_safe);
	JUMBLE_FIELD(scan.plan.async_capable);
	JUMBLE_FIELD(scan.plan.plan_node_id);
	JUMBLE_NODE(scan.plan.targetlist);
	JUMBLE_NODE(scan.plan.qual);
	JUMBLE_BITMAPSET(scan.plan.extParam);
	JUMBLE_BITMAPSET(scan.plan.allParam);
	_jumbleRtIndex(jstate, expr->scan.scanrelid);
	/* JUMBLE_FIELD(scan.scanrelid); */

	JUMBLE_NODE(scan.plan.qual);

	_jumbleYbPushdownExprs(jstate, &(expr->yb_pushdown));
}

static void
_jumbleYbBitmapIndexScan(YbJumbleState *jstate, Node *node)
{
	YbBitmapIndexScan *expr = (YbBitmapIndexScan *) node;

	JUMBLE_FIELD(scan.plan.parallel_aware);
	JUMBLE_FIELD(scan.plan.parallel_safe);
	JUMBLE_FIELD(scan.plan.async_capable);
	JUMBLE_FIELD(scan.plan.plan_node_id);
	JUMBLE_NODE(scan.plan.targetlist);
	JUMBLE_NODE(scan.plan.qual);
	JUMBLE_BITMAPSET(scan.plan.extParam);
	JUMBLE_BITMAPSET(scan.plan.allParam);
	_jumbleRtIndex(jstate, expr->scan.scanrelid);
	/* JUMBLE_FIELD(scan.scanrelid); */

	if (jstate->ybUseNames)
	{
		char	   *indexName = expr->scan.ybScannedObjectName;

		Assert(indexName != NULL);

		if (indexName != NULL)
			AppendJumble(jstate, (const unsigned char *) indexName, strlen(indexName) + 1);
		else
			AppendJumbleNull(jstate);
	}
	else
		JUMBLE_FIELD(indexid);

	JUMBLE_NODE(indexqual);
	JUMBLE_NODE(indexqualorig);

	/*
	 * 'indextlist' describes the columns in the index, even if they are not referenced.
	 * Skip this since we already identify the index itself.
	 */
	/* JUMBLE_NODE(indextlist); */

	_jumbleYbPushdownExprs(jstate, &(expr->yb_idx_pushdown));
}

static void
_jumbleYbBitmapTableScan(YbJumbleState *jstate, Node *node)
{
	YbBitmapTableScan *expr = (YbBitmapTableScan *) node;

	JUMBLE_FIELD(scan.plan.parallel_aware);
	JUMBLE_FIELD(scan.plan.parallel_safe);
	JUMBLE_FIELD(scan.plan.async_capable);
	JUMBLE_FIELD(scan.plan.plan_node_id);
	JUMBLE_NODE(scan.plan.targetlist);
	JUMBLE_NODE(scan.plan.qual);
	JUMBLE_BITMAPSET(scan.plan.extParam);
	JUMBLE_BITMAPSET(scan.plan.allParam);
	_jumbleRtIndex(jstate, expr->scan.scanrelid);
	/* JUMBLE_FIELD(scan.scanrelid); */

	_jumbleYbPushdownExprs(jstate, &(expr->rel_pushdown));
	_jumbleYbPushdownExprs(jstate, &(expr->recheck_pushdown));
	_jumbleYbPushdownExprs(jstate, &(expr->fallback_pushdown));

	JUMBLE_NODE(recheck_local_quals);
	JUMBLE_NODE(fallback_local_quals);
}

static void
_jumbleYbBatchedNestLoop(YbJumbleState *jstate, Node *node)
{
	YbBatchedNestLoop *expr = (YbBatchedNestLoop *) node;

	JUMBLE_FIELD(nl.join.plan.parallel_aware);
	JUMBLE_FIELD(nl.join.plan.parallel_safe);
	JUMBLE_FIELD(nl.join.plan.async_capable);
	JUMBLE_FIELD(nl.join.plan.plan_node_id);
	JUMBLE_NODE(nl.join.plan.targetlist);
	JUMBLE_NODE(nl.join.plan.qual);
	JUMBLE_BITMAPSET(nl.join.plan.extParam);
	JUMBLE_BITMAPSET(nl.join.plan.allParam);
	JUMBLE_FIELD(nl.join.jointype);
	JUMBLE_FIELD(nl.join.inner_unique);
	/* JUMBLE_NODE(nl.join.joinqual); */
	YbJumbleList(jstate, expr->nl.join.joinqual, true);
	JUMBLE_NODE(nl.nestParams);

	/*
	 * YB TODO - jumble other BNL fields?
	 */
}

static void
_jumbleYbExprColrefDesc(YbJumbleState *jstate, Node *node)
{
	YbExprColrefDesc *expr = (YbExprColrefDesc *) node;

	JUMBLE_FIELD(attno);
	JUMBLE_FIELD(typid);
	JUMBLE_FIELD(typmod);
	JUMBLE_FIELD(collid);
}

/*
 * We jumble lists of constant elements as one individual item regardless
 * of how many elements are in the list.  This means different queries
 * jumble to the same query_id, if the only difference is the number of
 * elements in the list.
 */
static void
_jumbleElements(YbJumbleState *jstate, List *elements)
{
	Node	   *first,
			   *last;

	if (IsSquashableConstList(elements, &first, &last))
	{
		/*
		 * If this list of elements is squashable, keep track of the location
		 * of its first and last elements.  When reading back the locations
		 * array, we'll see two consecutive locations with ->squashed set to
		 * true, indicating the location of initial and final elements of this
		 * list.
		 *
		 * For the limited set of cases we support now (implicit coerce via
		 * FuncExpr, Const) it's fine to use exprLocation of the 'last'
		 * expression, but if more complex composite expressions are to be
		 * supported (e.g., OpExpr or FuncExpr as an explicit call), more
		 * sophisticated tracking will be needed.
		 */
		RecordConstLocation(jstate, exprLocation(first), true);
		RecordConstLocation(jstate, exprLocation(last), true);
	}
	else
		YbJumbleNode(jstate, (Node *) elements);
}

void
YbJumbleNode(YbJumbleState *jstate, Node *node)
{
	Node	   *expr = node;
#ifdef USE_ASSERT_CHECKING
	Size		prev_jumble_len = jstate->total_jumble_len;
#endif

	if (expr == NULL)
	{
		AppendJumbleNull(jstate);
		return;
	}

	/* Guard against stack overflow due to overly complex expressions */
	check_stack_depth();

	/*
	 * We always emit the node's NodeTag, then any additional fields that are
	 * considered significant, and then we recurse to any child nodes.
	 */
	JUMBLE_FIELD(type);

	switch (nodeTag(expr))
	{
		case T_Alias:
			_jumbleAlias(jstate, expr);
			break;
		case T_RangeVar:
			_jumbleRangeVar(jstate, expr);
			break;
		case T_TableFunc:
			_jumbleTableFunc(jstate, expr);
			break;
		case T_IntoClause:
			_jumbleIntoClause(jstate, expr);
			break;
		case T_Var:
			_jumbleVar(jstate, expr);
			break;
		case T_Const:
			_jumbleConst(jstate, expr);
			break;
		case T_Param:
			_jumbleParam(jstate, expr);
			break;
		case T_Aggref:
			_jumbleAggref(jstate, expr);
			break;
		case T_GroupingFunc:
			_jumbleGroupingFunc(jstate, expr);
			break;
		case T_WindowFunc:
			_jumbleWindowFunc(jstate, expr);
			break;
#if 0
		case T_WindowFuncRunCondition:
			_jumbleWindowFuncRunCondition(jstate, expr);
			break;
		case T_MergeSupportFunc:
			_jumbleMergeSupportFunc(jstate, expr);
			break;
#endif
		case T_SubscriptingRef:
			_jumbleSubscriptingRef(jstate, expr);
			break;
		case T_FuncExpr:
			_jumbleFuncExpr(jstate, expr);
			break;
		case T_NamedArgExpr:
			_jumbleNamedArgExpr(jstate, expr);
			break;
		case T_OpExpr:
			_jumbleOpExpr(jstate, expr);
			break;
		case T_DistinctExpr:
			_jumbleDistinctExpr(jstate, expr);
			break;
		case T_NullIfExpr:
			_jumbleNullIfExpr(jstate, expr);
			break;
		case T_ScalarArrayOpExpr:
			_jumbleScalarArrayOpExpr(jstate, expr);
			break;
		case T_BoolExpr:
			_jumbleBoolExpr(jstate, expr);
			break;
		case T_SubLink:
			_jumbleSubLink(jstate, expr);
			break;
		case T_SubPlan:
			_jumbleSubPlan(jstate, expr);
			break;
		case T_FieldSelect:
			_jumbleFieldSelect(jstate, expr);
			break;
		case T_FieldStore:
			_jumbleFieldStore(jstate, expr);
			break;
		case T_RelabelType:
			_jumbleRelabelType(jstate, expr);
			break;
		case T_CoerceViaIO:
			_jumbleCoerceViaIO(jstate, expr);
			break;
		case T_ArrayCoerceExpr:
			_jumbleArrayCoerceExpr(jstate, expr);
			break;
		case T_ConvertRowtypeExpr:
			_jumbleConvertRowtypeExpr(jstate, expr);
			break;
		case T_CollateExpr:
			_jumbleCollateExpr(jstate, expr);
			break;
		case T_CaseExpr:
			_jumbleCaseExpr(jstate, expr);
			break;
		case T_CaseWhen:
			_jumbleCaseWhen(jstate, expr);
			break;
		case T_CaseTestExpr:
			_jumbleCaseTestExpr(jstate, expr);
			break;
		case T_ArrayExpr:
			_jumbleArrayExpr(jstate, expr);
			break;
		case T_RowExpr:
			_jumbleRowExpr(jstate, expr);
			break;
		case T_RowCompareExpr:
			_jumbleRowCompareExpr(jstate, expr);
			break;
		case T_CoalesceExpr:
			_jumbleCoalesceExpr(jstate, expr);
			break;
		case T_MinMaxExpr:
			_jumbleMinMaxExpr(jstate, expr);
			break;
		case T_SQLValueFunction:
			_jumbleSQLValueFunction(jstate, expr);
			break;
		case T_XmlExpr:
			_jumbleXmlExpr(jstate, expr);
			break;
#if 0
		case T_JsonFormat:
			_jumbleJsonFormat(jstate, expr);
			break;
		case T_JsonReturning:
			_jumbleJsonReturning(jstate, expr);
			break;
		case T_JsonValueExpr:
			_jumbleJsonValueExpr(jstate, expr);
			break;
		case T_JsonConstructorExpr:
			_jumbleJsonConstructorExpr(jstate, expr);
			break;
		case T_JsonIsPredicate:
			_jumbleJsonIsPredicate(jstate, expr);
			break;
		case T_JsonBehavior:
			_jumbleJsonBehavior(jstate, expr);
			break;
		case T_JsonExpr:
			_jumbleJsonExpr(jstate, expr);
			break;
		case T_JsonTablePath:
			_jumbleJsonTablePath(jstate, expr);
			break;
		case T_JsonTablePathScan:
			_jumbleJsonTablePathScan(jstate, expr);
			break;
		case T_JsonTableSiblingJoin:
			_jumbleJsonTableSiblingJoin(jstate, expr);
			break;
#endif
		case T_NullTest:
			_jumbleNullTest(jstate, expr);
			break;
		case T_BooleanTest:
			_jumbleBooleanTest(jstate, expr);
			break;
		case T_MergeAction:
			_jumbleMergeAction(jstate, expr);
			break;
		case T_CoerceToDomain:
			_jumbleCoerceToDomain(jstate, expr);
			break;
		case T_CoerceToDomainValue:
			_jumbleCoerceToDomainValue(jstate, expr);
			break;
		case T_SetToDefault:
			_jumbleSetToDefault(jstate, expr);
			break;
		case T_CurrentOfExpr:
			_jumbleCurrentOfExpr(jstate, expr);
			break;
		case T_NextValueExpr:
			_jumbleNextValueExpr(jstate, expr);
			break;
		case T_InferenceElem:
			_jumbleInferenceElem(jstate, expr);
			break;
#if 0
		case T_ReturningExpr:
			_jumbleReturningExpr(jstate, expr);
			break;
#endif
		case T_TargetEntry:
			_jumbleTargetEntry(jstate, expr);
			break;
		case T_RangeTblRef:
			_jumbleRangeTblRef(jstate, expr);
			break;
		case T_JoinExpr:
			_jumbleJoinExpr(jstate, expr);
			break;
		case T_FromExpr:
			_jumbleFromExpr(jstate, expr);
			break;
		case T_OnConflictExpr:
			_jumbleOnConflictExpr(jstate, expr);
			break;
		case T_Query:
			_jumbleQuery(jstate, expr);
			break;
		case T_TypeName:
			_jumbleTypeName(jstate, expr);
			break;
		case T_ColumnRef:
			_jumbleColumnRef(jstate, expr);
			break;
		case T_ParamRef:
			_jumbleParamRef(jstate, expr);
			break;
		case T_A_Expr:
			_jumbleA_Expr(jstate, expr);
			break;
		case T_A_Const:
			_jumbleA_Const(jstate, expr);
			break;
		case T_TypeCast:
			_jumbleTypeCast(jstate, expr);
			break;
		case T_CollateClause:
			_jumbleCollateClause(jstate, expr);
			break;
		case T_RoleSpec:
			_jumbleRoleSpec(jstate, expr);
			break;
		case T_FuncCall:
			_jumbleFuncCall(jstate, expr);
			break;
		case T_A_Star:
			_jumbleA_Star(jstate, expr);
			break;
		case T_A_Indices:
			_jumbleA_Indices(jstate, expr);
			break;
		case T_A_Indirection:
			_jumbleA_Indirection(jstate, expr);
			break;
		case T_A_ArrayExpr:
			_jumbleA_ArrayExpr(jstate, expr);
			break;
		case T_ResTarget:
			_jumbleResTarget(jstate, expr);
			break;
		case T_MultiAssignRef:
			_jumbleMultiAssignRef(jstate, expr);
			break;
		case T_SortBy:
			_jumbleSortBy(jstate, expr);
			break;
		case T_WindowDef:
			_jumbleWindowDef(jstate, expr);
			break;
		case T_RangeSubselect:
			_jumbleRangeSubselect(jstate, expr);
			break;
		case T_RangeFunction:
			_jumbleRangeFunction(jstate, expr);
			break;
		case T_RangeTableFunc:
			_jumbleRangeTableFunc(jstate, expr);
			break;
		case T_RangeTableFuncCol:
			_jumbleRangeTableFuncCol(jstate, expr);
			break;
		case T_RangeTableSample:
			_jumbleRangeTableSample(jstate, expr);
			break;
		case T_ColumnDef:
			_jumbleColumnDef(jstate, expr);
			break;
		case T_TableLikeClause:
			_jumbleTableLikeClause(jstate, expr);
			break;
		case T_IndexElem:
			_jumbleIndexElem(jstate, expr);
			break;
		case T_DefElem:
			_jumbleDefElem(jstate, expr);
			break;
		case T_LockingClause:
			_jumbleLockingClause(jstate, expr);
			break;
		case T_XmlSerialize:
			_jumbleXmlSerialize(jstate, expr);
			break;
		case T_PartitionElem:
			_jumblePartitionElem(jstate, expr);
			break;
		case T_PartitionSpec:
			_jumblePartitionSpec(jstate, expr);
			break;
		case T_PartitionBoundSpec:
			_jumblePartitionBoundSpec(jstate, expr);
			break;
		case T_PartitionRangeDatum:
			_jumblePartitionRangeDatum(jstate, expr);
			break;
		case T_PartitionCmd:
			_jumblePartitionCmd(jstate, expr);
			break;
		case T_RangeTblEntry:
			_jumbleRangeTblEntry(jstate, expr);
			break;
#if 0
		case T_RTEPermissionInfo:
			_jumbleRTEPermissionInfo(jstate, expr);
			break;
#endif
		case T_RangeTblFunction:
			_jumbleRangeTblFunction(jstate, expr);
			break;
		case T_TableSampleClause:
			_jumbleTableSampleClause(jstate, expr);
			break;
		case T_WithCheckOption:
			_jumbleWithCheckOption(jstate, expr);
			break;
		case T_SortGroupClause:
			_jumbleSortGroupClause(jstate, expr);
			break;
		case T_GroupingSet:
			_jumbleGroupingSet(jstate, expr);
			break;
		case T_WindowClause:
			_jumbleWindowClause(jstate, expr);
			break;
		case T_RowMarkClause:
			_jumbleRowMarkClause(jstate, expr);
			break;
		case T_WithClause:
			_jumbleWithClause(jstate, expr);
			break;
		case T_InferClause:
			_jumbleInferClause(jstate, expr);
			break;
		case T_OnConflictClause:
			_jumbleOnConflictClause(jstate, expr);
			break;
		case T_CTESearchClause:
			_jumbleCTESearchClause(jstate, expr);
			break;
		case T_CTECycleClause:
			_jumbleCTECycleClause(jstate, expr);
			break;
		case T_CommonTableExpr:
			_jumbleCommonTableExpr(jstate, expr);
			break;
		case T_MergeWhenClause:
			_jumbleMergeWhenClause(jstate, expr);
			break;
#if 0
		case T_ReturningOption:
			_jumbleReturningOption(jstate, expr);
			break;
		case T_ReturningClause:
			_jumbleReturningClause(jstate, expr);
			break;
#endif
		case T_TriggerTransition:
			_jumbleTriggerTransition(jstate, expr);
			break;
#if 0
		case T_JsonOutput:
			_jumbleJsonOutput(jstate, expr);
			break;
		case T_JsonArgument:
			_jumbleJsonArgument(jstate, expr);
			break;
		case T_JsonFuncExpr:
			_jumbleJsonFuncExpr(jstate, expr);
			break;
		case T_JsonTablePathSpec:
			_jumbleJsonTablePathSpec(jstate, expr);
			break;
		case T_JsonTable:
			_jumbleJsonTable(jstate, expr);
			break;
		case T_JsonTableColumn:
			_jumbleJsonTableColumn(jstate, expr);
			break;
		case T_JsonKeyValue:
			_jumbleJsonKeyValue(jstate, expr);
			break;
		case T_JsonParseExpr:
			_jumbleJsonParseExpr(jstate, expr);
			break;
		case T_JsonScalarExpr:
			_jumbleJsonScalarExpr(jstate, expr);
			break;
		case T_JsonSerializeExpr:
			_jumbleJsonSerializeExpr(jstate, expr);
			break;
		case T_JsonObjectConstructor:
			_jumbleJsonObjectConstructor(jstate, expr);
			break;
		case T_JsonArrayConstructor:
			_jumbleJsonArrayConstructor(jstate, expr);
			break;
		case T_JsonArrayQueryConstructor:
			_jumbleJsonArrayQueryConstructor(jstate, expr);
			break;
		case T_JsonAggConstructor:
			_jumbleJsonAggConstructor(jstate, expr);
			break;
		case T_JsonObjectAgg:
			_jumbleJsonObjectAgg(jstate, expr);
			break;
		case T_JsonArrayAgg:
			_jumbleJsonArrayAgg(jstate, expr);
			break;
#endif
		case T_InsertStmt:
			_jumbleInsertStmt(jstate, expr);
			break;
		case T_DeleteStmt:
			_jumbleDeleteStmt(jstate, expr);
			break;
		case T_UpdateStmt:
			_jumbleUpdateStmt(jstate, expr);
			break;
		case T_MergeStmt:
			_jumbleMergeStmt(jstate, expr);
			break;
		case T_SelectStmt:
			_jumbleSelectStmt(jstate, expr);
			break;
		case T_SetOperationStmt:
			_jumbleSetOperationStmt(jstate, expr);
			break;
		case T_ReturnStmt:
			_jumbleReturnStmt(jstate, expr);
			break;
		case T_PLAssignStmt:
			_jumblePLAssignStmt(jstate, expr);
			break;
		case T_CreateSchemaStmt:
			_jumbleCreateSchemaStmt(jstate, expr);
			break;
		case T_AlterTableStmt:
			_jumbleAlterTableStmt(jstate, expr);
			break;
		case T_AlterTableCmd:
			_jumbleAlterTableCmd(jstate, expr);
			break;
#if 0
		case T_ATAlterConstraint:
			_jumbleATAlterConstraint(jstate, expr);
			break;
#endif
		case T_ReplicaIdentityStmt:
			_jumbleReplicaIdentityStmt(jstate, expr);
			break;
		case T_AlterCollationStmt:
			_jumbleAlterCollationStmt(jstate, expr);
			break;
		case T_AlterDomainStmt:
			_jumbleAlterDomainStmt(jstate, expr);
			break;
		case T_GrantStmt:
			_jumbleGrantStmt(jstate, expr);
			break;
		case T_ObjectWithArgs:
			_jumbleObjectWithArgs(jstate, expr);
			break;
		case T_AccessPriv:
			_jumbleAccessPriv(jstate, expr);
			break;
		case T_GrantRoleStmt:
			_jumbleGrantRoleStmt(jstate, expr);
			break;
		case T_AlterDefaultPrivilegesStmt:
			_jumbleAlterDefaultPrivilegesStmt(jstate, expr);
			break;
		case T_CopyStmt:
			_jumbleCopyStmt(jstate, expr);
			break;
		case T_VariableSetStmt:
			_jumbleVariableSetStmt(jstate, expr);
			break;
		case T_VariableShowStmt:
			_jumbleVariableShowStmt(jstate, expr);
			break;
		case T_CreateStmt:
			_jumbleCreateStmt(jstate, expr);
			break;
		case T_Constraint:
			_jumbleConstraint(jstate, expr);
			break;
		case T_CreateTableSpaceStmt:
			_jumbleCreateTableSpaceStmt(jstate, expr);
			break;
		case T_DropTableSpaceStmt:
			_jumbleDropTableSpaceStmt(jstate, expr);
			break;
		case T_AlterTableSpaceOptionsStmt:
			_jumbleAlterTableSpaceOptionsStmt(jstate, expr);
			break;
		case T_AlterTableMoveAllStmt:
			_jumbleAlterTableMoveAllStmt(jstate, expr);
			break;
		case T_CreateExtensionStmt:
			_jumbleCreateExtensionStmt(jstate, expr);
			break;
		case T_AlterExtensionStmt:
			_jumbleAlterExtensionStmt(jstate, expr);
			break;
		case T_AlterExtensionContentsStmt:
			_jumbleAlterExtensionContentsStmt(jstate, expr);
			break;
		case T_CreateFdwStmt:
			_jumbleCreateFdwStmt(jstate, expr);
			break;
		case T_AlterFdwStmt:
			_jumbleAlterFdwStmt(jstate, expr);
			break;
		case T_CreateForeignServerStmt:
			_jumbleCreateForeignServerStmt(jstate, expr);
			break;
		case T_AlterForeignServerStmt:
			_jumbleAlterForeignServerStmt(jstate, expr);
			break;
		case T_CreateForeignTableStmt:
			_jumbleCreateForeignTableStmt(jstate, expr);
			break;
		case T_CreateUserMappingStmt:
			_jumbleCreateUserMappingStmt(jstate, expr);
			break;
		case T_AlterUserMappingStmt:
			_jumbleAlterUserMappingStmt(jstate, expr);
			break;
		case T_DropUserMappingStmt:
			_jumbleDropUserMappingStmt(jstate, expr);
			break;
		case T_ImportForeignSchemaStmt:
			_jumbleImportForeignSchemaStmt(jstate, expr);
			break;
		case T_CreatePolicyStmt:
			_jumbleCreatePolicyStmt(jstate, expr);
			break;
		case T_AlterPolicyStmt:
			_jumbleAlterPolicyStmt(jstate, expr);
			break;
		case T_CreateAmStmt:
			_jumbleCreateAmStmt(jstate, expr);
			break;
		case T_CreateTrigStmt:
			_jumbleCreateTrigStmt(jstate, expr);
			break;
		case T_CreateEventTrigStmt:
			_jumbleCreateEventTrigStmt(jstate, expr);
			break;
		case T_AlterEventTrigStmt:
			_jumbleAlterEventTrigStmt(jstate, expr);
			break;
		case T_CreatePLangStmt:
			_jumbleCreatePLangStmt(jstate, expr);
			break;
		case T_CreateRoleStmt:
			_jumbleCreateRoleStmt(jstate, expr);
			break;
		case T_AlterRoleStmt:
			_jumbleAlterRoleStmt(jstate, expr);
			break;
		case T_AlterRoleSetStmt:
			_jumbleAlterRoleSetStmt(jstate, expr);
			break;
		case T_DropRoleStmt:
			_jumbleDropRoleStmt(jstate, expr);
			break;
		case T_CreateSeqStmt:
			_jumbleCreateSeqStmt(jstate, expr);
			break;
		case T_AlterSeqStmt:
			_jumbleAlterSeqStmt(jstate, expr);
			break;
		case T_DefineStmt:
			_jumbleDefineStmt(jstate, expr);
			break;
		case T_CreateDomainStmt:
			_jumbleCreateDomainStmt(jstate, expr);
			break;
		case T_CreateOpClassStmt:
			_jumbleCreateOpClassStmt(jstate, expr);
			break;
		case T_CreateOpClassItem:
			_jumbleCreateOpClassItem(jstate, expr);
			break;
		case T_CreateOpFamilyStmt:
			_jumbleCreateOpFamilyStmt(jstate, expr);
			break;
		case T_AlterOpFamilyStmt:
			_jumbleAlterOpFamilyStmt(jstate, expr);
			break;
		case T_DropStmt:
			_jumbleDropStmt(jstate, expr);
			break;
		case T_TruncateStmt:
			_jumbleTruncateStmt(jstate, expr);
			break;
		case T_CommentStmt:
			_jumbleCommentStmt(jstate, expr);
			break;
		case T_SecLabelStmt:
			_jumbleSecLabelStmt(jstate, expr);
			break;
		case T_DeclareCursorStmt:
			_jumbleDeclareCursorStmt(jstate, expr);
			break;
		case T_ClosePortalStmt:
			_jumbleClosePortalStmt(jstate, expr);
			break;
		case T_FetchStmt:
			_jumbleFetchStmt(jstate, expr);
			break;
		case T_IndexStmt:
			_jumbleIndexStmt(jstate, expr);
			break;
		case T_CreateStatsStmt:
			_jumbleCreateStatsStmt(jstate, expr);
			break;
		case T_StatsElem:
			_jumbleStatsElem(jstate, expr);
			break;
		case T_AlterStatsStmt:
			_jumbleAlterStatsStmt(jstate, expr);
			break;
		case T_CreateFunctionStmt:
			_jumbleCreateFunctionStmt(jstate, expr);
			break;
		case T_FunctionParameter:
			_jumbleFunctionParameter(jstate, expr);
			break;
		case T_AlterFunctionStmt:
			_jumbleAlterFunctionStmt(jstate, expr);
			break;
		case T_DoStmt:
			_jumbleDoStmt(jstate, expr);
			break;
		case T_CallStmt:
			_jumbleCallStmt(jstate, expr);
			break;
		case T_RenameStmt:
			_jumbleRenameStmt(jstate, expr);
			break;
		case T_AlterObjectDependsStmt:
			_jumbleAlterObjectDependsStmt(jstate, expr);
			break;
		case T_AlterObjectSchemaStmt:
			_jumbleAlterObjectSchemaStmt(jstate, expr);
			break;
		case T_AlterOwnerStmt:
			_jumbleAlterOwnerStmt(jstate, expr);
			break;
		case T_AlterOperatorStmt:
			_jumbleAlterOperatorStmt(jstate, expr);
			break;
		case T_AlterTypeStmt:
			_jumbleAlterTypeStmt(jstate, expr);
			break;
		case T_RuleStmt:
			_jumbleRuleStmt(jstate, expr);
			break;
		case T_NotifyStmt:
			_jumbleNotifyStmt(jstate, expr);
			break;
		case T_ListenStmt:
			_jumbleListenStmt(jstate, expr);
			break;
		case T_UnlistenStmt:
			_jumbleUnlistenStmt(jstate, expr);
			break;
		case T_TransactionStmt:
			_jumbleTransactionStmt(jstate, expr);
			break;
		case T_CompositeTypeStmt:
			_jumbleCompositeTypeStmt(jstate, expr);
			break;
		case T_CreateEnumStmt:
			_jumbleCreateEnumStmt(jstate, expr);
			break;
		case T_CreateRangeStmt:
			_jumbleCreateRangeStmt(jstate, expr);
			break;
		case T_AlterEnumStmt:
			_jumbleAlterEnumStmt(jstate, expr);
			break;
		case T_ViewStmt:
			_jumbleViewStmt(jstate, expr);
			break;
		case T_LoadStmt:
			_jumbleLoadStmt(jstate, expr);
			break;
		case T_CreatedbStmt:
			_jumbleCreatedbStmt(jstate, expr);
			break;
		case T_AlterDatabaseStmt:
			_jumbleAlterDatabaseStmt(jstate, expr);
			break;
		case T_AlterDatabaseRefreshCollStmt:
			_jumbleAlterDatabaseRefreshCollStmt(jstate, expr);
			break;
		case T_AlterDatabaseSetStmt:
			_jumbleAlterDatabaseSetStmt(jstate, expr);
			break;
		case T_DropdbStmt:
			_jumbleDropdbStmt(jstate, expr);
			break;
		case T_AlterSystemStmt:
			_jumbleAlterSystemStmt(jstate, expr);
			break;
		case T_ClusterStmt:
			_jumbleClusterStmt(jstate, expr);
			break;
		case T_VacuumStmt:
			_jumbleVacuumStmt(jstate, expr);
			break;
		case T_VacuumRelation:
			_jumbleVacuumRelation(jstate, expr);
			break;
		case T_ExplainStmt:
			_jumbleExplainStmt(jstate, expr);
			break;
		case T_CreateTableAsStmt:
			_jumbleCreateTableAsStmt(jstate, expr);
			break;
		case T_RefreshMatViewStmt:
			_jumbleRefreshMatViewStmt(jstate, expr);
			break;
		case T_CheckPointStmt:
			_jumbleCheckPointStmt(jstate, expr);
			break;
		case T_DiscardStmt:
			_jumbleDiscardStmt(jstate, expr);
			break;
		case T_LockStmt:
			_jumbleLockStmt(jstate, expr);
			break;
		case T_ConstraintsSetStmt:
			_jumbleConstraintsSetStmt(jstate, expr);
			break;
		case T_ReindexStmt:
			_jumbleReindexStmt(jstate, expr);
			break;
		case T_CreateConversionStmt:
			_jumbleCreateConversionStmt(jstate, expr);
			break;
		case T_CreateCastStmt:
			_jumbleCreateCastStmt(jstate, expr);
			break;
		case T_CreateTransformStmt:
			_jumbleCreateTransformStmt(jstate, expr);
			break;
		case T_PrepareStmt:
			_jumblePrepareStmt(jstate, expr);
			break;
		case T_ExecuteStmt:
			_jumbleExecuteStmt(jstate, expr);
			break;
#if 0
		case T_DeallocateStmt:
			_jumbleDeallocateStmt(jstate, expr);
			break;
#endif
		case T_DropOwnedStmt:
			_jumbleDropOwnedStmt(jstate, expr);
			break;
		case T_ReassignOwnedStmt:
			_jumbleReassignOwnedStmt(jstate, expr);
			break;
		case T_AlterTSDictionaryStmt:
			_jumbleAlterTSDictionaryStmt(jstate, expr);
			break;
		case T_AlterTSConfigurationStmt:
			_jumbleAlterTSConfigurationStmt(jstate, expr);
			break;
		case T_PublicationTable:
			_jumblePublicationTable(jstate, expr);
			break;
		case T_PublicationObjSpec:
			_jumblePublicationObjSpec(jstate, expr);
			break;
		case T_CreatePublicationStmt:
			_jumbleCreatePublicationStmt(jstate, expr);
			break;
		case T_AlterPublicationStmt:
			_jumbleAlterPublicationStmt(jstate, expr);
			break;
		case T_CreateSubscriptionStmt:
			_jumbleCreateSubscriptionStmt(jstate, expr);
			break;
		case T_AlterSubscriptionStmt:
			_jumbleAlterSubscriptionStmt(jstate, expr);
			break;
		case T_DropSubscriptionStmt:
			_jumbleDropSubscriptionStmt(jstate, expr);
			break;
#if 0
		case T_GroupByOrdering:
			_jumbleGroupByOrdering(jstate, expr);
			break;
#endif
		case T_Result:
			_jumbleResult(jstate, expr);
			break;
		case T_ProjectSet:
			_jumbleProjectSet(jstate, expr);
			break;
		case T_ModifyTable:
			_jumbleModifyTable(jstate, expr);
			break;
		case T_Append:
			_jumbleAppend(jstate, expr);
			break;
		case T_MergeAppend:
			_jumbleMergeAppend(jstate, expr);
			break;
		case T_RecursiveUnion:
			_jumbleRecursiveUnion(jstate, expr);
			break;
		case T_BitmapAnd:
			_jumbleBitmapAnd(jstate, expr);
			break;
		case T_BitmapOr:
			_jumbleBitmapOr(jstate, expr);
			break;
		case T_SeqScan:
			_jumbleSeqScan(jstate, expr);
			break;
		case T_SampleScan:
			_jumbleSampleScan(jstate, expr);
			break;
		case T_IndexScan:
			_jumbleIndexScan(jstate, expr);
			break;
		case T_IndexOnlyScan:
			_jumbleIndexOnlyScan(jstate, expr);
			break;
		case T_BitmapIndexScan:
			_jumbleBitmapIndexScan(jstate, expr);
			break;
		case T_BitmapHeapScan:
			_jumbleBitmapHeapScan(jstate, expr);
			break;
		case T_TidScan:
			_jumbleTidScan(jstate, expr);
			break;
		case T_TidRangeScan:
			_jumbleTidRangeScan(jstate, expr);
			break;
		case T_SubqueryScan:
			_jumbleSubqueryScan(jstate, expr);
			break;
		case T_FunctionScan:
			_jumbleFunctionScan(jstate, expr);
			break;
		case T_ValuesScan:
			_jumbleValuesScan(jstate, expr);
			break;
		case T_TableFuncScan:
			_jumbleTableFuncScan(jstate, expr);
			break;
		case T_CteScan:
			_jumbleCteScan(jstate, expr);
			break;
		case T_NamedTuplestoreScan:
			_jumbleNamedTuplestoreScan(jstate, expr);
			break;
		case T_WorkTableScan:
			_jumbleWorkTableScan(jstate, expr);
			break;
		case T_ForeignScan:
			_jumbleForeignScan(jstate, expr);
			break;
		case T_CustomScan:
			_jumbleCustomScan(jstate, expr);
			break;
		case T_NestLoop:
			_jumbleNestLoop(jstate, expr);
			break;
		case T_NestLoopParam:
			_jumbleNestLoopParam(jstate, expr);
			break;
		case T_MergeJoin:
			_jumbleMergeJoin(jstate, expr);
			break;
		case T_HashJoin:
			_jumbleHashJoin(jstate, expr);
			break;
		case T_Material:
			_jumbleMaterial(jstate, expr);
			break;
		case T_Memoize:
			_jumbleMemoize(jstate, expr);
			break;
		case T_Sort:
			_jumbleSort(jstate, expr);
			break;
		case T_IncrementalSort:
			_jumbleIncrementalSort(jstate, expr);
			break;
		case T_Group:
			_jumbleGroup(jstate, expr);
			break;
		case T_Agg:
			_jumbleAgg(jstate, expr);
			break;
		case T_WindowAgg:
			_jumbleWindowAgg(jstate, expr);
			break;
		case T_Unique:
			_jumbleUnique(jstate, expr);
			break;
		case T_Gather:
			_jumbleGather(jstate, expr);
			break;
		case T_GatherMerge:
			_jumbleGatherMerge(jstate, expr);
			break;
		case T_Hash:
			_jumbleHash(jstate, expr);
			break;
		case T_SetOp:
			_jumbleSetOp(jstate, expr);
			break;
		case T_LockRows:
			_jumbleLockRows(jstate, expr);
			break;
		case T_Limit:
			_jumbleLimit(jstate, expr);
			break;
		case T_ExtensibleNode:
			_jumbleExtensibleNode(jstate, expr);
			break;
		case T_Integer:
			_jumbleInteger(jstate, expr);
			break;
		case T_Float:
			_jumbleFloat(jstate, expr);
			break;
		case T_Boolean:
			_jumbleBoolean(jstate, expr);
			break;
		case T_String:
			_jumbleString(jstate, expr);
			break;
		case T_BitString:
			_jumbleBitString(jstate, expr);
			break;
		case T_YbBitmapTablePath:
			_jumbleYbBitmapTablePath(jstate, expr);
			break;
		case T_PlanRowMark:
			_jumblePlanRowMark(jstate, expr);
			break;
		case T_YbSeqScan:
			_jumbleYbSeqScan(jstate, expr);
			break;
		case T_YbBitmapIndexScan:
			_jumbleYbBitmapIndexScan(jstate, expr);
			break;
		case T_YbBitmapTableScan:
			_jumbleYbBitmapTableScan(jstate, expr);
			break;
		case T_YbBatchedNestLoop:
			_jumbleYbBatchedNestLoop(jstate, expr);
			break;
		case T_YbExprColrefDesc:
			_jumbleYbExprColrefDesc(jstate, expr);
			break;

		case T_List:
		case T_IntList:
		case T_OidList:
			/* case T_XidList: */
			_jumbleList(jstate, expr);
			break;

		default:
			/* Only a warning, since we can stumble along anyway */
			elog(ERROR, "unrecognized node type: %d",
				 (int) nodeTag(expr));
			break;
	}

	/* Special cases to handle outside the automated code */
	switch (nodeTag(expr))
	{
		case T_Param:
			{
				Param	   *p = (Param *) node;

				/*
				 * Update the highest Param id seen, in order to start
				 * normalization correctly.
				 */
				if (p->paramkind == PARAM_EXTERN &&
					p->paramid > jstate->highest_extern_param_id)
					jstate->highest_extern_param_id = p->paramid;
			}
			break;
		default:
			break;
	}

	/* Ensure we added something to the jumble buffer */
	Assert(jstate->total_jumble_len > prev_jumble_len);
}

static void
_jumbleList(YbJumbleState *jstate, Node *node)
{
	List	   *expr = (List *) node;
	ListCell   *l;

	switch (expr->type)
	{
		case T_List:
			foreach(l, expr)
				YbJumbleNode(jstate, lfirst(l));
			break;
		case T_IntList:
			foreach(l, expr)
				AppendJumble32(jstate, (const unsigned char *) &lfirst_int(l));
			break;
		case T_OidList:
			foreach(l, expr)
				AppendJumble32(jstate, (const unsigned char *) &lfirst_oid(l));
			break;
#if 0
		case T_XidList:
			foreach(l, expr)
				AppendJumble32(jstate, (const unsigned char *) &lfirst_xid(l));
			break;
#endif
		default:
			elog(ERROR, "unrecognized list node type: %d",
				 (int) expr->type);
			return;
	}
}

static void
_jumbleA_Const(YbJumbleState *jstate, Node *node)
{
	A_Const    *expr = (A_Const *) node;

	JUMBLE_FIELD(isnull);
	if (!expr->isnull)
	{
		JUMBLE_FIELD(val.node.type);
		switch (nodeTag(&expr->val))
		{
			case T_Integer:
				JUMBLE_FIELD(val.ival.ival);
				break;
			case T_Float:
				JUMBLE_STRING(val.fval.fval);
				break;
			case T_Boolean:
				JUMBLE_FIELD(val.boolval.boolval);
				break;
			case T_String:
				JUMBLE_STRING(val.sval.sval);
				break;
			case T_BitString:
				JUMBLE_STRING(val.bsval.bsval);
				break;
			default:
				elog(ERROR, "unrecognized node type: %d",
					 (int) nodeTag(&expr->val));
				break;
		}
	}
}

static void
_jumbleVariableSetStmt(YbJumbleState *jstate, Node *node)
{
	VariableSetStmt *expr = (VariableSetStmt *) node;

	JUMBLE_FIELD(kind);
	JUMBLE_STRING(name);

	/*
	 * Account for the list of arguments in query jumbling only if told by the
	 * parser.
	 */
#if 0
	if (expr->jumble_args)
		JUMBLE_NODE(args);
#endif
	JUMBLE_FIELD(is_local);
	/* JUMBLE_LOCATION(location); */
}

/*
 * Custom query jumble function for RangeTblEntry.eref.
 */
static void
_jumbleRangeTblEntry_eref(YbJumbleState *jstate,
						  RangeTblEntry *rte,
						  Alias *expr)
{
	JUMBLE_FIELD(type);

	/*
	 * This includes only the table name, the list of column names is ignored.
	 */
	JUMBLE_STRING(aliasname);
}

/*
 * Jumble the entries in the range table to map RT indexes to relations
 *
 * This ensures jumbled RT indexes (e.g. in a Scan or Modify node), are
 * distinguished by the target of the RT entry, even if the index is the same.
 */
void
YbJumbleRangeTableList(YbJumbleState *jstate, List *rtable)
{
	ListCell   *lc;

	int			rtIndex = 0;

	foreach(lc, rtable)
	{
		RangeTblEntry *rte = lfirst_node(RangeTblEntry, lc);

		++rtIndex;

		switch (rte->rtekind)
		{
			case RTE_RELATION:
				{
					AppendJumble(jstate, (const unsigned char *) &rtIndex, sizeof(int));

					if (!(jstate->ybUseNames))
						AppendJumble(jstate, (const unsigned char *) &(rte->relid), sizeof(rte->relid));
					else
					{
						char	   *relName = rte->ybScannedObjectName;

						if (relName == NULL)
						{
							relName = get_rel_name(rte->relid);
							rte->ybScannedObjectName = relName;
						}

						if (relName != NULL)
							AppendJumble(jstate, (const unsigned char *) relName, strlen(relName));

						AppendJumble(jstate, (const unsigned char *) &rtIndex, sizeof(int));

						char	   *schemaName = rte->ybSchemaName;

						if (schemaName == NULL)
						{
							Oid			schemaOid = get_rel_namespace(rte->relid);

							schemaName = get_namespace_name(schemaOid);
							rte->ybSchemaName = schemaName;
						}

						if (schemaName != NULL)
							AppendJumble(jstate, (const unsigned char *) schemaName, strlen(schemaName));
					}
				}
				break;
			case RTE_CTE:
				AppendJumble(jstate, (const unsigned char *) (rte->ctename), strlen(rte->ctename));
				break;
			case RTE_JOIN:
				break;
			default:
				{
					if (!(jstate->ybUseNames))
						AppendJumble(jstate, (const unsigned char *) &(rte->relid), sizeof(rte->relid));
					else
					{
						char	   *relName = rte->ybScannedObjectName;

						if (relName == NULL)
						{
							relName = get_rel_name(rte->relid);
							rte->ybScannedObjectName = relName;
						}

						if (relName != NULL)
							AppendJumble(jstate, (const unsigned char *) relName, strlen(relName));
					}
				}
				break;
		}
	}
}
