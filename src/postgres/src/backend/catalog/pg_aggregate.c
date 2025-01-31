/*-------------------------------------------------------------------------
 *
 * pg_aggregate.c
 *	  routines to support manipulation of the pg_aggregate relation
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/backend/catalog/pg_aggregate.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/htup_details.h"
#include "access/table.h"
#include "catalog/dependency.h"
#include "catalog/indexing.h"
#include "catalog/pg_aggregate.h"
#include "catalog/pg_language.h"
#include "catalog/pg_operator.h"
#include "catalog/pg_proc.h"
#include "catalog/pg_type.h"
#include "miscadmin.h"
#include "parser/parse_coerce.h"
#include "parser/parse_func.h"
#include "parser/parse_oper.h"
#include "utils/acl.h"
#include "utils/builtins.h"
#include "utils/lsyscache.h"
#include "utils/rel.h"
#include "utils/syscache.h"


static Oid	lookup_agg_function(List *fnName, int nargs, Oid *input_types,
								Oid variadicArgType,
								Oid *rettype);


/*
 * AggregateCreate
 */
ObjectAddress
AggregateCreate(const char *aggName,
				Oid aggNamespace,
				bool replace,
				char aggKind,
				int numArgs,
				int numDirectArgs,
				oidvector *parameterTypes,
				Datum allParameterTypes,
				Datum parameterModes,
				Datum parameterNames,
				List *parameterDefaults,
				Oid variadicArgType,
				List *aggtransfnName,
				List *aggfinalfnName,
				List *aggcombinefnName,
				List *aggserialfnName,
				List *aggdeserialfnName,
				List *aggmtransfnName,
				List *aggminvtransfnName,
				List *aggmfinalfnName,
				bool finalfnExtraArgs,
				bool mfinalfnExtraArgs,
				char finalfnModify,
				char mfinalfnModify,
				List *aggsortopName,
				Oid aggTransType,
				int32 aggTransSpace,
				Oid aggmTransType,
				int32 aggmTransSpace,
				const char *agginitval,
				const char *aggminitval,
				char proparallel)
{
	Relation	aggdesc;
	HeapTuple	tup;
	HeapTuple	oldtup;
	bool		nulls[Natts_pg_aggregate];
	Datum		values[Natts_pg_aggregate];
	bool		replaces[Natts_pg_aggregate];
	Form_pg_proc proc;
	Oid			transfn;
	Oid			finalfn = InvalidOid;	/* can be omitted */
	Oid			combinefn = InvalidOid; /* can be omitted */
	Oid			serialfn = InvalidOid;	/* can be omitted */
	Oid			deserialfn = InvalidOid;	/* can be omitted */
	Oid			mtransfn = InvalidOid;	/* can be omitted */
	Oid			minvtransfn = InvalidOid;	/* can be omitted */
	Oid			mfinalfn = InvalidOid;	/* can be omitted */
	Oid			sortop = InvalidOid;	/* can be omitted */
	Oid		   *aggArgTypes = parameterTypes->values;
	bool		mtransIsStrict = false;
	Oid			rettype;
	Oid			finaltype;
	Oid			fnArgs[FUNC_MAX_ARGS];
	int			nargs_transfn;
	int			nargs_finalfn;
	Oid			procOid;
	TupleDesc	tupDesc;
	char	   *detailmsg;
	int			i;
	ObjectAddress myself,
				referenced;
	ObjectAddresses *addrs;
	AclResult	aclresult;

	/* sanity checks (caller should have caught these) */
	if (!aggName)
		elog(ERROR, "no aggregate name supplied");

	if (!aggtransfnName)
		elog(ERROR, "aggregate must have a transition function");

	if (numDirectArgs < 0 || numDirectArgs > numArgs)
		elog(ERROR, "incorrect number of direct arguments for aggregate");

	/*
	 * Aggregates can have at most FUNC_MAX_ARGS-1 args, else the transfn
	 * and/or finalfn will be unrepresentable in pg_proc.  We must check now
	 * to protect fixed-size arrays here and possibly in called functions.
	 */
	if (numArgs < 0 || numArgs > FUNC_MAX_ARGS - 1)
		ereport(ERROR,
				(errcode(ERRCODE_TOO_MANY_ARGUMENTS),
				 errmsg_plural("aggregates cannot have more than %d argument",
							   "aggregates cannot have more than %d arguments",
							   FUNC_MAX_ARGS - 1,
							   FUNC_MAX_ARGS - 1)));

	/*
	 * If transtype is polymorphic, must have polymorphic argument also; else
	 * we will have no way to deduce the actual transtype.
	 */
	detailmsg = check_valid_polymorphic_signature(aggTransType,
												  aggArgTypes,
												  numArgs);
	if (detailmsg)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
				 errmsg("cannot determine transition data type"),
				 errdetail_internal("%s", detailmsg)));

	/*
	 * Likewise for moving-aggregate transtype, if any
	 */
	if (OidIsValid(aggmTransType))
	{
		detailmsg = check_valid_polymorphic_signature(aggmTransType,
													  aggArgTypes,
													  numArgs);
		if (detailmsg)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
					 errmsg("cannot determine transition data type"),
					 errdetail_internal("%s", detailmsg)));
	}

	/*
	 * An ordered-set aggregate that is VARIADIC must be VARIADIC ANY.  In
	 * principle we could support regular variadic types, but it would make
	 * things much more complicated because we'd have to assemble the correct
	 * subsets of arguments into array values.  Since no standard aggregates
	 * have use for such a case, we aren't bothering for now.
	 */
	if (AGGKIND_IS_ORDERED_SET(aggKind) && OidIsValid(variadicArgType) &&
		variadicArgType != ANYOID)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("a variadic ordered-set aggregate must use VARIADIC type ANY")));

	/*
	 * If it's a hypothetical-set aggregate, there must be at least as many
	 * direct arguments as aggregated ones, and the last N direct arguments
	 * must match the aggregated ones in type.  (We have to check this again
	 * when the aggregate is called, in case ANY is involved, but it makes
	 * sense to reject the aggregate definition now if the declared arg types
	 * don't match up.)  It's unconditionally OK if numDirectArgs == numArgs,
	 * indicating that the grammar merged identical VARIADIC entries from both
	 * lists.  Otherwise, if the agg is VARIADIC, then we had VARIADIC only on
	 * the aggregated side, which is not OK.  Otherwise, insist on the last N
	 * parameter types on each side matching exactly.
	 */
	if (aggKind == AGGKIND_HYPOTHETICAL &&
		numDirectArgs < numArgs)
	{
		int			numAggregatedArgs = numArgs - numDirectArgs;

		if (OidIsValid(variadicArgType) ||
			numDirectArgs < numAggregatedArgs ||
			memcmp(aggArgTypes + (numDirectArgs - numAggregatedArgs),
				   aggArgTypes + numDirectArgs,
				   numAggregatedArgs * sizeof(Oid)) != 0)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
					 errmsg("a hypothetical-set aggregate must have direct arguments matching its aggregated arguments")));
	}

	/*
	 * Find the transfn.  For ordinary aggs, it takes the transtype plus all
	 * aggregate arguments.  For ordered-set aggs, it takes the transtype plus
	 * all aggregated args, but not direct args.  However, we have to treat
	 * specially the case where a trailing VARIADIC item is considered to
	 * cover both direct and aggregated args.
	 */
	if (AGGKIND_IS_ORDERED_SET(aggKind))
	{
		if (numDirectArgs < numArgs)
			nargs_transfn = numArgs - numDirectArgs + 1;
		else
		{
			/* special case with VARIADIC last arg */
			Assert(variadicArgType != InvalidOid);
			nargs_transfn = 2;
		}
		fnArgs[0] = aggTransType;
		memcpy(fnArgs + 1, aggArgTypes + (numArgs - (nargs_transfn - 1)),
			   (nargs_transfn - 1) * sizeof(Oid));
	}
	else
	{
		nargs_transfn = numArgs + 1;
		fnArgs[0] = aggTransType;
		memcpy(fnArgs + 1, aggArgTypes, numArgs * sizeof(Oid));
	}
	transfn = lookup_agg_function(aggtransfnName, nargs_transfn,
								  fnArgs, variadicArgType,
								  &rettype);

	/*
	 * Return type of transfn (possibly after refinement by
	 * enforce_generic_type_consistency, if transtype isn't polymorphic) must
	 * exactly match declared transtype.
	 *
	 * In the non-polymorphic-transtype case, it might be okay to allow a
	 * rettype that's binary-coercible to transtype, but I'm not quite
	 * convinced that it's either safe or useful.  When transtype is
	 * polymorphic we *must* demand exact equality.
	 */
	if (rettype != aggTransType)
		ereport(ERROR,
				(errcode(ERRCODE_DATATYPE_MISMATCH),
				 errmsg("return type of transition function %s is not %s",
						NameListToString(aggtransfnName),
						format_type_be(aggTransType))));

	tup = SearchSysCache1(PROCOID, ObjectIdGetDatum(transfn));
	if (!HeapTupleIsValid(tup))
		elog(ERROR, "cache lookup failed for function %u", transfn);
	proc = (Form_pg_proc) GETSTRUCT(tup);

	/*
	 * If the transfn is strict and the initval is NULL, make sure first input
	 * type and transtype are the same (or at least binary-compatible), so
	 * that it's OK to use the first input value as the initial transValue.
	 */
	if (proc->proisstrict && agginitval == NULL)
	{
		if (numArgs < 1 ||
			!IsBinaryCoercible(aggArgTypes[0], aggTransType))
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
					 errmsg("must not omit initial value when transition function is strict and transition type is not compatible with input type")));
	}

	ReleaseSysCache(tup);

	/* handle moving-aggregate transfn, if supplied */
	if (aggmtransfnName)
	{
		/*
		 * The arguments are the same as for the regular transfn, except that
		 * the transition data type might be different.  So re-use the fnArgs
		 * values set up above, except for that one.
		 */
		Assert(OidIsValid(aggmTransType));
		fnArgs[0] = aggmTransType;

		mtransfn = lookup_agg_function(aggmtransfnName, nargs_transfn,
									   fnArgs, variadicArgType,
									   &rettype);

		/* As above, return type must exactly match declared mtranstype. */
		if (rettype != aggmTransType)
			ereport(ERROR,
					(errcode(ERRCODE_DATATYPE_MISMATCH),
					 errmsg("return type of transition function %s is not %s",
							NameListToString(aggmtransfnName),
							format_type_be(aggmTransType))));

		tup = SearchSysCache1(PROCOID, ObjectIdGetDatum(mtransfn));
		if (!HeapTupleIsValid(tup))
			elog(ERROR, "cache lookup failed for function %u", mtransfn);
		proc = (Form_pg_proc) GETSTRUCT(tup);

		/*
		 * If the mtransfn is strict and the minitval is NULL, check first
		 * input type and mtranstype are binary-compatible.
		 */
		if (proc->proisstrict && aggminitval == NULL)
		{
			if (numArgs < 1 ||
				!IsBinaryCoercible(aggArgTypes[0], aggmTransType))
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
						 errmsg("must not omit initial value when transition function is strict and transition type is not compatible with input type")));
		}

		/* Remember if mtransfn is strict; we may need this below */
		mtransIsStrict = proc->proisstrict;

		ReleaseSysCache(tup);
	}

	/* handle minvtransfn, if supplied */
	if (aggminvtransfnName)
	{
		/*
		 * This must have the same number of arguments with the same types as
		 * the forward transition function, so just re-use the fnArgs data.
		 */
		Assert(aggmtransfnName);

		minvtransfn = lookup_agg_function(aggminvtransfnName, nargs_transfn,
										  fnArgs, variadicArgType,
										  &rettype);

		/* As above, return type must exactly match declared mtranstype. */
		if (rettype != aggmTransType)
			ereport(ERROR,
					(errcode(ERRCODE_DATATYPE_MISMATCH),
					 errmsg("return type of inverse transition function %s is not %s",
							NameListToString(aggminvtransfnName),
							format_type_be(aggmTransType))));

		tup = SearchSysCache1(PROCOID, ObjectIdGetDatum(minvtransfn));
		if (!HeapTupleIsValid(tup))
			elog(ERROR, "cache lookup failed for function %u", minvtransfn);
		proc = (Form_pg_proc) GETSTRUCT(tup);

		/*
		 * We require the strictness settings of the forward and inverse
		 * transition functions to agree.  This saves having to handle
		 * assorted special cases at execution time.
		 */
		if (proc->proisstrict != mtransIsStrict)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
					 errmsg("strictness of aggregate's forward and inverse transition functions must match")));

		ReleaseSysCache(tup);
	}

	/* handle finalfn, if supplied */
	if (aggfinalfnName)
	{
		/*
		 * If finalfnExtraArgs is specified, the transfn takes the transtype
		 * plus all args; otherwise, it just takes the transtype plus any
		 * direct args.  (Non-direct args are useless at runtime, and are
		 * actually passed as NULLs, but we may need them in the function
		 * signature to allow resolution of a polymorphic agg's result type.)
		 */
		Oid			ffnVariadicArgType = variadicArgType;

		fnArgs[0] = aggTransType;
		memcpy(fnArgs + 1, aggArgTypes, numArgs * sizeof(Oid));
		if (finalfnExtraArgs)
			nargs_finalfn = numArgs + 1;
		else
		{
			nargs_finalfn = numDirectArgs + 1;
			if (numDirectArgs < numArgs)
			{
				/* variadic argument doesn't affect finalfn */
				ffnVariadicArgType = InvalidOid;
			}
		}

		finalfn = lookup_agg_function(aggfinalfnName, nargs_finalfn,
									  fnArgs, ffnVariadicArgType,
									  &finaltype);

		/*
		 * When finalfnExtraArgs is specified, the finalfn will certainly be
		 * passed at least one null argument, so complain if it's strict.
		 * Nothing bad would happen at runtime (you'd just get a null result),
		 * but it's surely not what the user wants, so let's complain now.
		 */
		if (finalfnExtraArgs && func_strict(finalfn))
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
					 errmsg("final function with extra arguments must not be declared STRICT")));
	}
	else
	{
		/*
		 * If no finalfn, aggregate result type is type of the state value
		 */
		finaltype = aggTransType;
	}
	Assert(OidIsValid(finaltype));

	/* handle the combinefn, if supplied */
	if (aggcombinefnName)
	{
		Oid			combineType;

		/*
		 * Combine function must have 2 arguments, each of which is the trans
		 * type.  VARIADIC doesn't affect it.
		 */
		fnArgs[0] = aggTransType;
		fnArgs[1] = aggTransType;

		combinefn = lookup_agg_function(aggcombinefnName, 2,
										fnArgs, InvalidOid,
										&combineType);

		/* Ensure the return type matches the aggregate's trans type */
		if (combineType != aggTransType)
			ereport(ERROR,
					(errcode(ERRCODE_DATATYPE_MISMATCH),
					 errmsg("return type of combine function %s is not %s",
							NameListToString(aggcombinefnName),
							format_type_be(aggTransType))));

		/*
		 * A combine function to combine INTERNAL states must accept nulls and
		 * ensure that the returned state is in the correct memory context. We
		 * cannot directly check the latter, but we can check the former.
		 */
		if (aggTransType == INTERNALOID && func_strict(combinefn))
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
					 errmsg("combine function with transition type %s must not be declared STRICT",
							format_type_be(aggTransType))));
	}

	/*
	 * Validate the serialization function, if present.
	 */
	if (aggserialfnName)
	{
		/* signature is always serialize(internal) returns bytea */
		fnArgs[0] = INTERNALOID;

		serialfn = lookup_agg_function(aggserialfnName, 1,
									   fnArgs, InvalidOid,
									   &rettype);

		if (rettype != BYTEAOID)
			ereport(ERROR,
					(errcode(ERRCODE_DATATYPE_MISMATCH),
					 errmsg("return type of serialization function %s is not %s",
							NameListToString(aggserialfnName),
							format_type_be(BYTEAOID))));
	}

	/*
	 * Validate the deserialization function, if present.
	 */
	if (aggdeserialfnName)
	{
		/* signature is always deserialize(bytea, internal) returns internal */
		fnArgs[0] = BYTEAOID;
		fnArgs[1] = INTERNALOID;	/* dummy argument for type safety */

		deserialfn = lookup_agg_function(aggdeserialfnName, 2,
										 fnArgs, InvalidOid,
										 &rettype);

		if (rettype != INTERNALOID)
			ereport(ERROR,
					(errcode(ERRCODE_DATATYPE_MISMATCH),
					 errmsg("return type of deserialization function %s is not %s",
							NameListToString(aggdeserialfnName),
							format_type_be(INTERNALOID))));
	}

	/*
	 * If finaltype (i.e. aggregate return type) is polymorphic, inputs must
	 * be polymorphic also, else parser will fail to deduce result type.
	 * (Note: given the previous test on transtype and inputs, this cannot
	 * happen, unless someone has snuck a finalfn definition into the catalogs
	 * that itself violates the rule against polymorphic result with no
	 * polymorphic input.)
	 */
	detailmsg = check_valid_polymorphic_signature(finaltype,
												  aggArgTypes,
												  numArgs);
	if (detailmsg)
		ereport(ERROR,
				(errcode(ERRCODE_DATATYPE_MISMATCH),
				 errmsg("cannot determine result data type"),
				 errdetail_internal("%s", detailmsg)));

	/*
	 * Also, the return type can't be INTERNAL unless there's at least one
	 * INTERNAL argument.  This is the same type-safety restriction we enforce
	 * for regular functions, but at the level of aggregates.  We must test
	 * this explicitly because we allow INTERNAL as the transtype.
	 */
	detailmsg = check_valid_internal_signature(finaltype,
											   aggArgTypes,
											   numArgs);
	if (detailmsg)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
				 errmsg("unsafe use of pseudo-type \"internal\""),
				 errdetail_internal("%s", detailmsg)));

	/*
	 * If a moving-aggregate implementation is supplied, look up its finalfn
	 * if any, and check that the implied aggregate result type matches the
	 * plain implementation.
	 */
	if (OidIsValid(aggmTransType))
	{
		/* handle finalfn, if supplied */
		if (aggmfinalfnName)
		{
			/*
			 * The arguments are figured the same way as for the regular
			 * finalfn, but using aggmTransType and mfinalfnExtraArgs.
			 */
			Oid			ffnVariadicArgType = variadicArgType;

			fnArgs[0] = aggmTransType;
			memcpy(fnArgs + 1, aggArgTypes, numArgs * sizeof(Oid));
			if (mfinalfnExtraArgs)
				nargs_finalfn = numArgs + 1;
			else
			{
				nargs_finalfn = numDirectArgs + 1;
				if (numDirectArgs < numArgs)
				{
					/* variadic argument doesn't affect finalfn */
					ffnVariadicArgType = InvalidOid;
				}
			}

			mfinalfn = lookup_agg_function(aggmfinalfnName, nargs_finalfn,
										   fnArgs, ffnVariadicArgType,
										   &rettype);

			/* As above, check strictness if mfinalfnExtraArgs is given */
			if (mfinalfnExtraArgs && func_strict(mfinalfn))
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
						 errmsg("final function with extra arguments must not be declared STRICT")));
		}
		else
		{
			/*
			 * If no finalfn, aggregate result type is type of the state value
			 */
			rettype = aggmTransType;
		}
		Assert(OidIsValid(rettype));
		if (rettype != finaltype)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
					 errmsg("moving-aggregate implementation returns type %s, but plain implementation returns type %s",
							format_type_be(rettype),
							format_type_be(finaltype))));
	}

	/* handle sortop, if supplied */
	if (aggsortopName)
	{
		if (numArgs != 1)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
					 errmsg("sort operator can only be specified for single-argument aggregates")));
		sortop = LookupOperName(NULL, aggsortopName,
								aggArgTypes[0], aggArgTypes[0],
								false, -1);
	}

	/*
	 * permission checks on used types
	 */
	for (i = 0; i < numArgs; i++)
	{
		aclresult = pg_type_aclcheck(aggArgTypes[i], GetUserId(), ACL_USAGE);
		if (aclresult != ACLCHECK_OK)
			aclcheck_error_type(aclresult, aggArgTypes[i]);
	}

	aclresult = pg_type_aclcheck(aggTransType, GetUserId(), ACL_USAGE);
	if (aclresult != ACLCHECK_OK)
		aclcheck_error_type(aclresult, aggTransType);

	if (OidIsValid(aggmTransType))
	{
		aclresult = pg_type_aclcheck(aggmTransType, GetUserId(), ACL_USAGE);
		if (aclresult != ACLCHECK_OK)
			aclcheck_error_type(aclresult, aggmTransType);
	}

	aclresult = pg_type_aclcheck(finaltype, GetUserId(), ACL_USAGE);
	if (aclresult != ACLCHECK_OK)
		aclcheck_error_type(aclresult, finaltype);


	/*
	 * Everything looks okay.  Try to create the pg_proc entry for the
	 * aggregate.  (This could fail if there's already a conflicting entry.)
	 */

	myself = ProcedureCreate(aggName,
							 aggNamespace,
							 replace,	/* maybe replacement */
							 false, /* doesn't return a set */
							 finaltype, /* returnType */
							 GetUserId(),	/* proowner */
							 INTERNALlanguageId,	/* languageObjectId */
							 InvalidOid,	/* no validator */
							 "aggregate_dummy", /* placeholder (no such proc) */
							 NULL,	/* probin */
							 NULL,	/* prosqlbody */
							 PROKIND_AGGREGATE,
							 false, /* security invoker (currently not
									 * definable for agg) */
							 false, /* isLeakProof */
							 false, /* isStrict (not needed for agg) */
							 PROVOLATILE_IMMUTABLE, /* volatility (not needed
													 * for agg) */
							 proparallel,
							 parameterTypes,	/* paramTypes */
							 allParameterTypes, /* allParamTypes */
							 parameterModes,	/* parameterModes */
							 parameterNames,	/* parameterNames */
							 parameterDefaults, /* parameterDefaults */
							 PointerGetDatum(NULL), /* trftypes */
							 PointerGetDatum(NULL), /* proconfig */
							 InvalidOid,	/* no prosupport */
							 1, /* procost */
							 0);	/* prorows */
	procOid = myself.objectId;

	/*
	 * Okay to create the pg_aggregate entry.
	 */
	aggdesc = table_open(AggregateRelationId, RowExclusiveLock);
	tupDesc = aggdesc->rd_att;

	/* initialize nulls and values */
	for (i = 0; i < Natts_pg_aggregate; i++)
	{
		nulls[i] = false;
		values[i] = (Datum) NULL;
		replaces[i] = true;
	}
	values[Anum_pg_aggregate_aggfnoid - 1] = ObjectIdGetDatum(procOid);
	values[Anum_pg_aggregate_aggkind - 1] = CharGetDatum(aggKind);
	values[Anum_pg_aggregate_aggnumdirectargs - 1] = Int16GetDatum(numDirectArgs);
	values[Anum_pg_aggregate_aggtransfn - 1] = ObjectIdGetDatum(transfn);
	values[Anum_pg_aggregate_aggfinalfn - 1] = ObjectIdGetDatum(finalfn);
	values[Anum_pg_aggregate_aggcombinefn - 1] = ObjectIdGetDatum(combinefn);
	values[Anum_pg_aggregate_aggserialfn - 1] = ObjectIdGetDatum(serialfn);
	values[Anum_pg_aggregate_aggdeserialfn - 1] = ObjectIdGetDatum(deserialfn);
	values[Anum_pg_aggregate_aggmtransfn - 1] = ObjectIdGetDatum(mtransfn);
	values[Anum_pg_aggregate_aggminvtransfn - 1] = ObjectIdGetDatum(minvtransfn);
	values[Anum_pg_aggregate_aggmfinalfn - 1] = ObjectIdGetDatum(mfinalfn);
	values[Anum_pg_aggregate_aggfinalextra - 1] = BoolGetDatum(finalfnExtraArgs);
	values[Anum_pg_aggregate_aggmfinalextra - 1] = BoolGetDatum(mfinalfnExtraArgs);
	values[Anum_pg_aggregate_aggfinalmodify - 1] = CharGetDatum(finalfnModify);
	values[Anum_pg_aggregate_aggmfinalmodify - 1] = CharGetDatum(mfinalfnModify);
	values[Anum_pg_aggregate_aggsortop - 1] = ObjectIdGetDatum(sortop);
	values[Anum_pg_aggregate_aggtranstype - 1] = ObjectIdGetDatum(aggTransType);
	values[Anum_pg_aggregate_aggtransspace - 1] = Int32GetDatum(aggTransSpace);
	values[Anum_pg_aggregate_aggmtranstype - 1] = ObjectIdGetDatum(aggmTransType);
	values[Anum_pg_aggregate_aggmtransspace - 1] = Int32GetDatum(aggmTransSpace);
	if (agginitval)
		values[Anum_pg_aggregate_agginitval - 1] = CStringGetTextDatum(agginitval);
	else
		nulls[Anum_pg_aggregate_agginitval - 1] = true;
	if (aggminitval)
		values[Anum_pg_aggregate_aggminitval - 1] = CStringGetTextDatum(aggminitval);
	else
		nulls[Anum_pg_aggregate_aggminitval - 1] = true;

	if (replace)
		oldtup = SearchSysCache1(AGGFNOID, ObjectIdGetDatum(procOid));
	else
		oldtup = NULL;

	if (HeapTupleIsValid(oldtup))
	{
		Form_pg_aggregate oldagg = (Form_pg_aggregate) GETSTRUCT(oldtup);

		/*
		 * If we're replacing an existing entry, we need to validate that
		 * we're not changing anything that would break callers. Specifically
		 * we must not change aggkind or aggnumdirectargs, which affect how an
		 * aggregate call is treated in parse analysis.
		 */
		if (aggKind != oldagg->aggkind)
			ereport(ERROR,
					(errcode(ERRCODE_WRONG_OBJECT_TYPE),
					 errmsg("cannot change routine kind"),
					 (oldagg->aggkind == AGGKIND_NORMAL ?
					  errdetail("\"%s\" is an ordinary aggregate function.", aggName) :
					  oldagg->aggkind == AGGKIND_ORDERED_SET ?
					  errdetail("\"%s\" is an ordered-set aggregate.", aggName) :
					  oldagg->aggkind == AGGKIND_HYPOTHETICAL ?
					  errdetail("\"%s\" is a hypothetical-set aggregate.", aggName) :
					  0)));
		if (numDirectArgs != oldagg->aggnumdirectargs)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
					 errmsg("cannot change number of direct arguments of an aggregate function")));

		replaces[Anum_pg_aggregate_aggfnoid - 1] = false;
		replaces[Anum_pg_aggregate_aggkind - 1] = false;
		replaces[Anum_pg_aggregate_aggnumdirectargs - 1] = false;

		tup = heap_modify_tuple(oldtup, tupDesc, values, nulls, replaces);
		CatalogTupleUpdate(aggdesc, &tup->t_self, tup);
		ReleaseSysCache(oldtup);
	}
	else
	{
		tup = heap_form_tuple(tupDesc, values, nulls);
		CatalogTupleInsert(aggdesc, tup);
	}

	table_close(aggdesc, RowExclusiveLock);

	/*
	 * Create dependencies for the aggregate (above and beyond those already
	 * made by ProcedureCreate).  Note: we don't need an explicit dependency
	 * on aggTransType since we depend on it indirectly through transfn.
	 * Likewise for aggmTransType using the mtransfn, if it exists.
	 *
	 * If we're replacing an existing definition, ProcedureCreate deleted all
	 * our existing dependencies, so we have to do the same things here either
	 * way.
	 */

	addrs = new_object_addresses();

	/* Depends on transition function */
	ObjectAddressSet(referenced, ProcedureRelationId, transfn);
	add_exact_object_address(&referenced, addrs);

	/* Depends on final function, if any */
	if (OidIsValid(finalfn))
	{
		ObjectAddressSet(referenced, ProcedureRelationId, finalfn);
		add_exact_object_address(&referenced, addrs);
	}

	/* Depends on combine function, if any */
	if (OidIsValid(combinefn))
	{
		ObjectAddressSet(referenced, ProcedureRelationId, combinefn);
		add_exact_object_address(&referenced, addrs);
	}

	/* Depends on serialization function, if any */
	if (OidIsValid(serialfn))
	{
		ObjectAddressSet(referenced, ProcedureRelationId, serialfn);
		add_exact_object_address(&referenced, addrs);
	}

	/* Depends on deserialization function, if any */
	if (OidIsValid(deserialfn))
	{
		ObjectAddressSet(referenced, ProcedureRelationId, deserialfn);
		add_exact_object_address(&referenced, addrs);
	}

	/* Depends on forward transition function, if any */
	if (OidIsValid(mtransfn))
	{
		ObjectAddressSet(referenced, ProcedureRelationId, mtransfn);
		add_exact_object_address(&referenced, addrs);
	}

	/* Depends on inverse transition function, if any */
	if (OidIsValid(minvtransfn))
	{
		ObjectAddressSet(referenced, ProcedureRelationId, minvtransfn);
		add_exact_object_address(&referenced, addrs);
	}

	/* Depends on final function, if any */
	if (OidIsValid(mfinalfn))
	{
		ObjectAddressSet(referenced, ProcedureRelationId, mfinalfn);
		add_exact_object_address(&referenced, addrs);
	}

	/* Depends on sort operator, if any */
	if (OidIsValid(sortop))
	{
		ObjectAddressSet(referenced, OperatorRelationId, sortop);
		add_exact_object_address(&referenced, addrs);
	}

	record_object_address_dependencies(&myself, addrs, DEPENDENCY_NORMAL);
	free_object_addresses(addrs);
	return myself;
}

/*
 * lookup_agg_function
 * common code for finding aggregate support functions
 *
 * fnName: possibly-schema-qualified function name
 * nargs, input_types: expected function argument types
 * variadicArgType: type of variadic argument if any, else InvalidOid
 *
 * Returns OID of function, and stores its return type into *rettype
 *
 * NB: must not scribble on input_types[], as we may re-use those
 */
static Oid
lookup_agg_function(List *fnName,
					int nargs,
					Oid *input_types,
					Oid variadicArgType,
					Oid *rettype)
{
	Oid			fnOid;
	bool		retset;
	int			nvargs;
	Oid			vatype;
	Oid		   *true_oid_array;
	FuncDetailCode fdresult;
	AclResult	aclresult;
	int			i;

	/*
	 * func_get_detail looks up the function in the catalogs, does
	 * disambiguation for polymorphic functions, handles inheritance, and
	 * returns the funcid and type and set or singleton status of the
	 * function's return value.  it also returns the true argument types to
	 * the function.
	 */
	fdresult = func_get_detail(fnName, NIL, NIL,
							   nargs, input_types, false, false, false,
							   &fnOid, rettype, &retset,
							   &nvargs, &vatype,
							   &true_oid_array, NULL);

	/* only valid case is a normal function not returning a set */
	if (fdresult != FUNCDETAIL_NORMAL || !OidIsValid(fnOid))
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_FUNCTION),
				 errmsg("function %s does not exist",
						func_signature_string(fnName, nargs,
											  NIL, input_types))));
	if (retset)
		ereport(ERROR,
				(errcode(ERRCODE_DATATYPE_MISMATCH),
				 errmsg("function %s returns a set",
						func_signature_string(fnName, nargs,
											  NIL, input_types))));

	/*
	 * If the agg is declared to take VARIADIC ANY, the underlying functions
	 * had better be declared that way too, else they may receive too many
	 * parameters; but func_get_detail would have been happy with plain ANY.
	 * (Probably nothing very bad would happen, but it wouldn't work as the
	 * user expects.)  Other combinations should work without any special
	 * pushups, given that we told func_get_detail not to expand VARIADIC.
	 */
	if (variadicArgType == ANYOID && vatype != ANYOID)
		ereport(ERROR,
				(errcode(ERRCODE_DATATYPE_MISMATCH),
				 errmsg("function %s must accept VARIADIC ANY to be used in this aggregate",
						func_signature_string(fnName, nargs,
											  NIL, input_types))));

	/*
	 * If there are any polymorphic types involved, enforce consistency, and
	 * possibly refine the result type.  It's OK if the result is still
	 * polymorphic at this point, though.
	 */
	*rettype = enforce_generic_type_consistency(input_types,
												true_oid_array,
												nargs,
												*rettype,
												true);

	/*
	 * func_get_detail will find functions requiring run-time argument type
	 * coercion, but nodeAgg.c isn't prepared to deal with that
	 */
	for (i = 0; i < nargs; i++)
	{
		if (!IsBinaryCoercible(input_types[i], true_oid_array[i]))
			ereport(ERROR,
					(errcode(ERRCODE_DATATYPE_MISMATCH),
					 errmsg("function %s requires run-time type coercion",
							func_signature_string(fnName, nargs,
												  NIL, true_oid_array))));
	}

	/* Check aggregate creator has permission to call the function */
	aclresult = pg_proc_aclcheck(fnOid, GetUserId(), ACL_EXECUTE);
	if (aclresult != ACLCHECK_OK)
		aclcheck_error(aclresult, OBJECT_FUNCTION, get_func_name(fnOid));

	return fnOid;
}
