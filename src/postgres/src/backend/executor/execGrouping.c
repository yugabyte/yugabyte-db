/*-------------------------------------------------------------------------
 *
 * execGrouping.c
 *	  executor utility routines for grouping, hashing, and aggregation
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/backend/executor/execGrouping.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/parallel.h"
#include "catalog/pg_type.h"
#include "common/hashfn.h"
#include "nodes/nodeFuncs.h"
#include "optimizer/clauses.h"
#include "executor/executor.h"
#include "miscadmin.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"

/* Yugabyte includes */
#include "catalog/pg_collation.h"
#include "nodes/makefuncs.h"
#include "pg_yb_utils.h"

static int	TupleHashTableMatch(struct tuplehash_hash *tb, const MinimalTuple tuple1, const MinimalTuple tuple2);
static inline uint32 TupleHashTableHash_internal(struct tuplehash_hash *tb,
												 const MinimalTuple tuple);
static inline TupleHashEntry LookupTupleHashEntry_internal(TupleHashTable hashtable,
														   TupleTableSlot *slot,
														   bool *isnew, uint32 hash);

/*
 * Define parameters for tuple hash table code generation. The interface is
 * *also* declared in execnodes.h (to generate the types, which are externally
 * visible).
 */
#define SH_PREFIX tuplehash
#define SH_ELEMENT_TYPE TupleHashEntryData
#define SH_KEY_TYPE MinimalTuple
#define SH_KEY firstTuple
#define SH_HASH_KEY(tb, key) TupleHashTableHash_internal(tb, key)
#define SH_EQUAL(tb, a, b) TupleHashTableMatch(tb, a, b) == 0
#define SH_SCOPE extern
#define SH_STORE_HASH
#define SH_GET_HASH(tb, a) a->hash
#define SH_DEFINE
#include "lib/simplehash.h"


/*****************************************************************************
 *		Utility routines for grouping tuples together
 *****************************************************************************/

/*
 * execTuplesMatchPrepare
 *		Build expression that can be evaluated using ExecQual(), returning
 *		whether an ExprContext's inner/outer tuples are NOT DISTINCT
 */
ExprState *
execTuplesMatchPrepare(TupleDesc desc,
					   int numCols,
					   const AttrNumber *keyColIdx,
					   const Oid *eqOperators,
					   const Oid *collations,
					   PlanState *parent)
{
	Oid		   *eqFunctions = (Oid *) palloc(numCols * sizeof(Oid));
	int			i;
	ExprState  *expr;

	if (numCols == 0)
		return NULL;

	/* lookup equality functions */
	for (i = 0; i < numCols; i++)
		eqFunctions[i] = get_opcode(eqOperators[i]);

	/* build actual expression */
	expr = ExecBuildGroupingEqual(desc, desc, NULL, NULL,
								  numCols, keyColIdx, eqFunctions, collations,
								  parent);

	return expr;
}

/*
 * execTuplesHashPrepare
 *		Look up the equality and hashing functions needed for a TupleHashTable.
 *
 * This is similar to execTuplesMatchPrepare, but we also need to find the
 * hash functions associated with the equality operators.  *eqFunctions and
 * *hashFunctions receive the palloc'd result arrays.
 *
 */
void
execTuplesHashPrepare(int numCols,
					  const Oid *eqOperators,
					  Oid **eqFuncOids,
					  FmgrInfo **leftHashFunctions,
					  FmgrInfo **rightHashFunctions)
{
	int			i;

	*eqFuncOids = (Oid *) palloc(numCols * sizeof(Oid));
	*leftHashFunctions = (FmgrInfo *) palloc(numCols * sizeof(FmgrInfo));
	if (rightHashFunctions)
		*rightHashFunctions = (FmgrInfo *) palloc(numCols * sizeof(FmgrInfo));

	for (i = 0; i < numCols; i++)
	{
		Oid			eq_opr = eqOperators[i];
		Oid			eq_function;
		Oid			left_hash_function;
		Oid			right_hash_function;

		eq_function = get_opcode(eq_opr);
		if (!get_op_hash_functions(eq_opr,
								   &left_hash_function, &right_hash_function))
			elog(ERROR, "could not find hash function for hash operator %u",
				 eq_opr);
		/* We're not supporting cross-type cases here */
		/* YB: We do support cross-type cases if rightHashFunctions is valid. */
		Assert(rightHashFunctions != NULL ||
			   left_hash_function == right_hash_function);
		(*eqFuncOids)[i] = eq_function;
		if (rightHashFunctions != NULL)
			fmgr_info(right_hash_function, &(*rightHashFunctions)[i]);
		fmgr_info(left_hash_function, &(*leftHashFunctions)[i]);
	}
}

ExprState *
ybPrepareOuterExprsEqualFn(List *outer_exprs, Oid *eqOps, PlanState *parent)
{
	List	   *quals = NULL;
	ListCell   *lc;
	int			i = 0;

	foreach(lc, outer_exprs)
	{
		Expr	   *rhs = lfirst(lc);
		Expr	   *lhs = yb_copy_replace_varnos(rhs, OUTER_VAR, INNER_VAR);

		int			collid = exprCollation((Node *) rhs);
		Oid			eqop;

		/*
		 * We can't directly use eqOps[i] as that might be a cross-type
		 * comparison between the outer and inner sides. We attempt to get
		 * a hashable op from the same opfamily that equates the type of lhs
		 * with itself.
		 */
		(void) get_compatible_hash_operators(eqOps[i], NULL, &eqop);
		Expr	   *qual = make_opclause(eqop, BOOLOID, false, lhs, rhs,
										 collid, collid);

		set_opfuncid((OpExpr *) qual);
		quals = lappend(quals, qual);
		i++;
	}
	return ExecInitQual(quals, parent);
}


/*****************************************************************************
 *		Utility routines for all-in-memory hash tables
 *
 * These routines build hash tables for grouping tuples together (eg, for
 * hash aggregation).  There is one entry for each not-distinct set of tuples
 * presented.
 *****************************************************************************/

TupleHashTable
YbBuildTupleHashTableExt(PlanState *parent,
						 TupleDesc inputDesc,
						 int numCols, ExprState **keyColExprs,
						 ExprState *eqExpr,
						 Oid *eqfuncoids,
						 FmgrInfo *hashfunctions,
						 long nbuckets, Size additionalsize,
						 MemoryContext metacxt,
						 MemoryContext tablecxt,
						 MemoryContext tempcxt,
						 ExprContext *expr_cxt,
						 bool use_variable_hash_iv)
{
	TupleHashTable hashtable;
	Size		entrysize = sizeof(TupleHashEntryData) + additionalsize;
	MemoryContext oldcontext;

	Assert(nbuckets > 0);

	/* Limit initial table size request to not more than work_mem */
	nbuckets = Min(nbuckets, (long) ((work_mem * 1024L) / entrysize));

	oldcontext = MemoryContextSwitchTo(metacxt);

	hashtable = (TupleHashTable) palloc(sizeof(TupleHashTableData));

	hashtable->numCols = numCols;
	hashtable->yb_keyColExprs = keyColExprs;
	hashtable->keyColIdx = NULL;
	hashtable->tab_hash_funcs = hashfunctions;
	hashtable->tab_collations = NULL;
	hashtable->tablecxt = tablecxt;
	hashtable->tempcxt = tempcxt;
	hashtable->entrysize = entrysize;
	hashtable->tableslot = NULL;	/* will be made on first lookup */
	hashtable->inputslot = NULL;
	hashtable->in_hash_funcs = NULL;
	hashtable->in_keyColIdx = NULL;
	hashtable->yb_in_keycolExprs = NULL;
	hashtable->cur_eq_func = NULL;

	/*
	 * If parallelism is in use, even if the master backend is performing the
	 * scan itself, we don't want to create the hashtable exactly the same way
	 * in all workers. As hashtables are iterated over in keyspace-order,
	 * doing so in all processes in the same way is likely to lead to
	 * "unbalanced" hashtables when the table size initially is
	 * underestimated.
	 */
	if (use_variable_hash_iv)
		hashtable->hash_iv = murmurhash32(ParallelWorkerNumber);
	else
		hashtable->hash_iv = 0;

	hashtable->hashtab = tuplehash_create(metacxt, nbuckets, hashtable);

	/*
	 * We copy the input tuple descriptor just for safety --- we assume all
	 * input tuples will have equivalent descriptors.
	 */
	hashtable->tableslot = MakeSingleTupleTableSlot(CreateTupleDescCopy(inputDesc),
													&TTSOpsMinimalTuple);

	/* build comparator for all columns */
	hashtable->tab_eq_func = eqExpr;

	/*
	 * While not pretty, it's ok to not shut down this context, but instead
	 * rely on the containing memory context being reset, as
	 * ExecBuildGroupingEqual() only builds a very simple expression calling
	 * functions (i.e. nothing that'd employ RegisterExprContextCallback()).
	 */
	hashtable->exprcontext =
		expr_cxt ? expr_cxt : CreateStandaloneExprContext();

	MemoryContextSwitchTo(oldcontext);

	return hashtable;
}

/*
 * Construct an empty TupleHashTable
 *
 *	numCols, keyColIdx: identify the tuple fields to use as lookup key
 *	eqfunctions: equality comparison functions to use
 *	hashfunctions: datatype-specific hashing functions to use
 *	nbuckets: initial estimate of hashtable size
 *	additionalsize: size of data stored in ->additional
 *	metacxt: memory context for long-lived allocation, but not per-entry data
 *	tablecxt: memory context in which to store table entries
 *	tempcxt: short-lived context for evaluation hash and comparison functions
 *
 * The function arrays may be made with execTuplesHashPrepare().  Note they
 * are not cross-type functions, but expect to see the table datatype(s)
 * on both sides.
 *
 * Note that keyColIdx, eqfunctions, and hashfunctions must be allocated in
 * storage that will live as long as the hashtable does.
 */
TupleHashTable
BuildTupleHashTableExt(PlanState *parent,
					   TupleDesc inputDesc,
					   int numCols, AttrNumber *keyColIdx,
					   const Oid *eqfuncoids,
					   FmgrInfo *hashfunctions,
					   Oid *collations,
					   long nbuckets, Size additionalsize,
					   MemoryContext metacxt,
					   MemoryContext tablecxt,
					   MemoryContext tempcxt,
					   bool use_variable_hash_iv)
{
	TupleHashTable hashtable;
	Size		entrysize = sizeof(TupleHashEntryData) + additionalsize;
	Size		hash_mem_limit;
	MemoryContext oldcontext;
	bool		allow_jit;

	Assert(nbuckets > 0);

	/* Limit initial table size request to not more than hash_mem */
	hash_mem_limit = get_hash_memory_limit() / entrysize;
	if (nbuckets > hash_mem_limit)
		nbuckets = hash_mem_limit;

	oldcontext = MemoryContextSwitchTo(metacxt);

	hashtable = (TupleHashTable) palloc(sizeof(TupleHashTableData));

	hashtable->numCols = numCols;
	hashtable->keyColIdx = keyColIdx;
	hashtable->tab_hash_funcs = hashfunctions;
	hashtable->tab_collations = collations;
	hashtable->yb_keyColExprs = NULL;
	hashtable->tablecxt = tablecxt;
	hashtable->tempcxt = tempcxt;
	hashtable->entrysize = entrysize;
	hashtable->tableslot = NULL;	/* will be made on first lookup */
	hashtable->inputslot = NULL;
	hashtable->in_hash_funcs = NULL;
	hashtable->in_keyColIdx = NULL;
	hashtable->yb_in_keycolExprs = NULL;
	hashtable->cur_eq_func = NULL;

	/*
	 * If parallelism is in use, even if the leader backend is performing the
	 * scan itself, we don't want to create the hashtable exactly the same way
	 * in all workers. As hashtables are iterated over in keyspace-order,
	 * doing so in all processes in the same way is likely to lead to
	 * "unbalanced" hashtables when the table size initially is
	 * underestimated.
	 */
	if (use_variable_hash_iv)
		hashtable->hash_iv = murmurhash32(ParallelWorkerNumber);
	else
		hashtable->hash_iv = 0;

	hashtable->hashtab = tuplehash_create(metacxt, nbuckets, hashtable);

	/*
	 * We copy the input tuple descriptor just for safety --- we assume all
	 * input tuples will have equivalent descriptors.
	 */
	hashtable->tableslot = MakeSingleTupleTableSlot(CreateTupleDescCopy(inputDesc),
													&TTSOpsMinimalTuple);

	/*
	 * If the old reset interface is used (i.e. BuildTupleHashTable, rather
	 * than BuildTupleHashTableExt), allowing JIT would lead to the generated
	 * functions to a) live longer than the query b) be re-generated each time
	 * the table is being reset. Therefore prevent JIT from being used in that
	 * case, by not providing a parent node (which prevents accessing the
	 * JitContext in the EState).
	 */
	allow_jit = metacxt != tablecxt;

	/* build comparator for all columns */
	/* XXX: should we support non-minimal tuples for the inputslot? */
	hashtable->tab_eq_func = ExecBuildGroupingEqual(inputDesc, inputDesc,
													&TTSOpsMinimalTuple, &TTSOpsMinimalTuple,
													numCols,
													keyColIdx, eqfuncoids, collations,
													allow_jit ? parent : NULL);

	/*
	 * While not pretty, it's ok to not shut down this context, but instead
	 * rely on the containing memory context being reset, as
	 * ExecBuildGroupingEqual() only builds a very simple expression calling
	 * functions (i.e. nothing that'd employ RegisterExprContextCallback()).
	 */
	hashtable->exprcontext = CreateStandaloneExprContext();

	MemoryContextSwitchTo(oldcontext);

	return hashtable;
}

/*
 * BuildTupleHashTable is a backwards-compatibility wrapper for
 * BuildTupleHashTableExt(), that allocates the hashtable's metadata in
 * tablecxt. Note that hashtables created this way cannot be reset leak-free
 * with ResetTupleHashTable().
 */
TupleHashTable
BuildTupleHashTable(PlanState *parent,
					TupleDesc inputDesc,
					int numCols, AttrNumber *keyColIdx,
					const Oid *eqfuncoids,
					FmgrInfo *hashfunctions,
					Oid *collations,
					long nbuckets, Size additionalsize,
					MemoryContext tablecxt,
					MemoryContext tempcxt,
					bool use_variable_hash_iv)
{
	return BuildTupleHashTableExt(parent,
								  inputDesc,
								  numCols, keyColIdx,
								  eqfuncoids,
								  hashfunctions,
								  collations,
								  nbuckets, additionalsize,
								  tablecxt,
								  tablecxt,
								  tempcxt,
								  use_variable_hash_iv);
}

/*
 * Reset contents of the hashtable to be empty, preserving all the non-content
 * state. Note that the tablecxt passed to BuildTupleHashTableExt() should
 * also be reset, otherwise there will be leaks.
 */
void
ResetTupleHashTable(TupleHashTable hashtable)
{
	tuplehash_reset(hashtable->hashtab);
}

/*
 * Find or create a hashtable entry for the tuple group containing the
 * given tuple.  The tuple must be the same type as the hashtable entries.
 *
 * If isnew is NULL, we do not create new entries; we return NULL if no
 * match is found.
 *
 * If hash is not NULL, we set it to the calculated hash value. This allows
 * callers access to the hash value even if no entry is returned.
 *
 * If isnew isn't NULL, then a new entry is created if no existing entry
 * matches.  On return, *isnew is true if the entry is newly created,
 * false if it existed already.  ->additional_data in the new entry has
 * been zeroed.
 */
TupleHashEntry
LookupTupleHashEntry(TupleHashTable hashtable, TupleTableSlot *slot,
					 bool *isnew, uint32 *hash)
{
	TupleHashEntry entry;
	MemoryContext oldContext;
	uint32		local_hash;

	/* Need to run the hash functions in short-lived context */
	oldContext = MemoryContextSwitchTo(hashtable->tempcxt);

	/* set up data needed by hash and match functions */
	hashtable->inputslot = slot;
	hashtable->in_hash_funcs = hashtable->tab_hash_funcs;
	hashtable->in_keyColIdx = hashtable->keyColIdx;
	hashtable->yb_in_keycolExprs = hashtable->yb_keyColExprs;
	hashtable->cur_eq_func = hashtable->tab_eq_func;

	local_hash = TupleHashTableHash_internal(hashtable->hashtab, NULL);
	entry = LookupTupleHashEntry_internal(hashtable, slot, isnew, local_hash);

	if (hash != NULL)
		*hash = local_hash;

	Assert(entry == NULL || entry->hash == local_hash);

	MemoryContextSwitchTo(oldContext);

	return entry;
}

/*
 * Compute the hash value for a tuple
 */
uint32
TupleHashTableHash(TupleHashTable hashtable, TupleTableSlot *slot)
{
	MemoryContext oldContext;
	uint32		hash;

	hashtable->inputslot = slot;
	hashtable->in_hash_funcs = hashtable->tab_hash_funcs;

	/* Need to run the hash functions in short-lived context */
	oldContext = MemoryContextSwitchTo(hashtable->tempcxt);

	hash = TupleHashTableHash_internal(hashtable->hashtab, NULL);

	MemoryContextSwitchTo(oldContext);

	return hash;
}

/*
 * A variant of LookupTupleHashEntry for callers that have already computed
 * the hash value.
 */
TupleHashEntry
LookupTupleHashEntryHash(TupleHashTable hashtable, TupleTableSlot *slot,
						 bool *isnew, uint32 hash)
{
	TupleHashEntry entry;
	MemoryContext oldContext;

	/* Need to run the hash functions in short-lived context */
	oldContext = MemoryContextSwitchTo(hashtable->tempcxt);

	/* set up data needed by hash and match functions */
	hashtable->inputslot = slot;
	hashtable->in_hash_funcs = hashtable->tab_hash_funcs;
	hashtable->cur_eq_func = hashtable->tab_eq_func;

	entry = LookupTupleHashEntry_internal(hashtable, slot, isnew, hash);
	Assert(entry == NULL || entry->hash == hash);

	MemoryContextSwitchTo(oldContext);

	return entry;
}

/*
 * Search for a hashtable entry matching the given tuple.  No entry is
 * created if there's not a match.  This is similar to the non-creating
 * case of LookupTupleHashEntry, except that it supports cross-type
 * comparisons, in which the given tuple is not of the same type as the
 * table entries.  The caller must provide the hash functions to use for
 * the input tuple, as well as the equality functions, and key attributes
 * to use for looking up with the given tuple since these may be
 * different from the table's internal functions.
 */
TupleHashEntry
FindTupleHashEntry(TupleHashTable hashtable, TupleTableSlot *slot,
				   ExprState *eqcomp,
				   FmgrInfo *hashfunctions,
				   AttrNumber *keyColIdx)
{
	TupleHashEntry entry;
	MemoryContext oldContext;
	MinimalTuple key;

	/* Need to run the hash functions in short-lived context */
	oldContext = MemoryContextSwitchTo(hashtable->tempcxt);

	/* Set up data needed by hash and match functions */
	hashtable->inputslot = slot;
	hashtable->in_hash_funcs = hashfunctions;
	hashtable->in_keyColIdx = keyColIdx;
	hashtable->yb_in_keycolExprs = NULL;
	hashtable->cur_eq_func = eqcomp;

	/* Search the hash table */
	key = NULL;					/* flag to reference inputslot */
	entry = tuplehash_lookup(hashtable->hashtab, key);
	MemoryContextSwitchTo(oldContext);

	return entry;
}

/*
 * If tuple is NULL, use the input slot instead. This convention avoids the
 * need to materialize virtual input tuples unless they actually need to get
 * copied into the table.
 *
 * Also, the caller must select an appropriate memory context for running
 * the hash functions. (dynahash.c doesn't change GetCurrentMemoryContext().)
 */
static uint32
TupleHashTableHash_internal(struct tuplehash_hash *tb,
							const MinimalTuple tuple)
{
	TupleHashTable hashtable = (TupleHashTable) tb->private_data;
	int			numCols = hashtable->numCols;
	AttrNumber *keyColIdx = hashtable->keyColIdx;
	uint32		hashkey = hashtable->hash_iv;
	TupleTableSlot *slot;
	FmgrInfo   *hashfunctions;
	int			i;
	ExprState **eval_exprs = NULL;

	if (tuple == NULL)
	{
		/* Process the current input tuple for the table */
		slot = hashtable->inputslot;
		hashfunctions = hashtable->in_hash_funcs;
		keyColIdx = hashtable->in_keyColIdx;
		eval_exprs = hashtable->yb_in_keycolExprs;
	}
	else
	{
		/*
		 * Process a tuple already stored in the table.
		 *
		 * (this case never actually occurs due to the way simplehash.h is
		 * used, as the hash-value is stored in the entries)
		 */
		slot = hashtable->tableslot;
		ExecStoreMinimalTuple(tuple, slot, false);
		hashfunctions = hashtable->tab_hash_funcs;
	}

	for (i = 0; i < numCols; i++)
	{
		AttrNumber	att;
		Datum		attr;
		bool		isNull;

		/* combine successive hashkeys by rotating */
		hashkey = pg_rotate_left32(hashkey, 1);

		if (eval_exprs != NULL && eval_exprs[i] != NULL)
		{
			hashtable->exprcontext->ecxt_outertuple = slot;
			attr = ExecEvalExprSwitchContext(eval_exprs[i],
											 hashtable->exprcontext,
											 &isNull);
		}
		else
		{
			att = keyColIdx[i];
			attr = slot_getattr(slot, att, &isNull);
		}

		if (!isNull)			/* treat nulls as having hash key 0 */
		{
			uint32		hkey;

			/* YB doesn't support collations with hash functions. */
			Oid			yb_collation = (IsYugaByteEnabled() ?
										DEFAULT_COLLATION_OID :
										hashtable->tab_collations[i]);

			hkey = DatumGetUInt32(FunctionCall1Coll(&hashfunctions[i],
													yb_collation,
													attr));
			hashkey ^= hkey;
		}
	}

	/*
	 * The way hashes are combined above, among each other and with the IV,
	 * doesn't lead to good bit perturbation. As the IV's goal is to lead to
	 * achieve that, perform a round of hashing of the combined hash -
	 * resulting in near perfect perturbation.
	 */
	return murmurhash32(hashkey);
}

/*
 * Does the work of LookupTupleHashEntry and LookupTupleHashEntryHash. Useful
 * so that we can avoid switching the memory context multiple times for
 * LookupTupleHashEntry.
 *
 * NB: This function may or may not change the memory context. Caller is
 * expected to change it back.
 */
static inline TupleHashEntry
LookupTupleHashEntry_internal(TupleHashTable hashtable, TupleTableSlot *slot,
							  bool *isnew, uint32 hash)
{
	TupleHashEntryData *entry;
	bool		found;
	MinimalTuple key;

	key = NULL;					/* flag to reference inputslot */

	if (isnew)
	{
		entry = tuplehash_insert_hash(hashtable->hashtab, key, hash, &found);

		if (found)
		{
			/* found pre-existing entry */
			*isnew = false;
		}
		else
		{
			/* created new entry */
			*isnew = true;
			/* zero caller data */
			entry->additional = NULL;
			MemoryContextSwitchTo(hashtable->tablecxt);
			/* Copy the first tuple into the table context */
			entry->firstTuple = ExecCopySlotMinimalTuple(slot);
		}
	}
	else
	{
		entry = tuplehash_lookup_hash(hashtable->hashtab, key, hash);
	}

	return entry;
}

/*
 * See whether two tuples (presumably of the same hash value) match
 */
static int
TupleHashTableMatch(struct tuplehash_hash *tb, const MinimalTuple tuple1, const MinimalTuple tuple2)
{
	TupleTableSlot *slot1;
	TupleTableSlot *slot2;
	TupleHashTable hashtable = (TupleHashTable) tb->private_data;
	ExprContext *econtext = hashtable->exprcontext;

	/*
	 * We assume that simplehash.h will only ever call us with the first
	 * argument being an actual table entry, and the second argument being
	 * LookupTupleHashEntry's dummy TupleHashEntryData.  The other direction
	 * could be supported too, but is not currently required.
	 */
	Assert(tuple1 != NULL);
	slot1 = hashtable->tableslot;
	ExecStoreMinimalTuple(tuple1, slot1, false);
	Assert(tuple2 == NULL);
	slot2 = hashtable->inputslot;

	/* For crosstype comparisons, the inputslot must be first */
	econtext->ecxt_innertuple = slot2;
	econtext->ecxt_outertuple = slot1;
	return !ExecQualAndReset(hashtable->cur_eq_func, econtext);
}
