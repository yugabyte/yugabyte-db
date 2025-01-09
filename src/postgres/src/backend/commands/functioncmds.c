/*-------------------------------------------------------------------------
 *
 * functioncmds.c
 *
 *	  Routines for CREATE and DROP FUNCTION commands and CREATE and DROP
 *	  CAST commands.
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/backend/commands/functioncmds.c
 *
 * DESCRIPTION
 *	  These routines take the parse tree and pick out the
 *	  appropriate arguments/flags, and pass the results to the
 *	  corresponding "FooDefine" routines (in src/catalog) that do
 *	  the actual catalog-munging.  These routines also verify permission
 *	  of the user to execute the command.
 *
 * NOTES
 *	  These things must be defined and committed in the following order:
 *		"create function":
 *				input/output, recv/send procedures
 *		"create type":
 *				type
 *		"create operator":
 *				operators
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/genam.h"
#include "access/htup_details.h"
#include "access/sysattr.h"
#include "access/table.h"
#include "catalog/catalog.h"
#include "catalog/dependency.h"
#include "catalog/indexing.h"
#include "catalog/objectaccess.h"
#include "catalog/pg_aggregate.h"
#include "catalog/pg_cast.h"
#include "catalog/pg_language.h"
#include "catalog/pg_namespace.h"
#include "catalog/pg_proc.h"
#include "catalog/pg_transform.h"
#include "catalog/pg_type.h"
#include "commands/alter.h"
#include "commands/defrem.h"
#include "commands/proclang.h"
#include "executor/execdesc.h"
#include "executor/executor.h"
#include "executor/functions.h"
#include "funcapi.h"
#include "miscadmin.h"
#include "optimizer/optimizer.h"
#include "parser/analyze.h"
#include "parser/parse_coerce.h"
#include "parser/parse_collate.h"
#include "parser/parse_expr.h"
#include "parser/parse_func.h"
#include "parser/parse_type.h"
#include "pgstat.h"
#include "tcop/pquery.h"
#include "tcop/utility.h"
#include "utils/acl.h"
#include "utils/builtins.h"
#include "utils/fmgroids.h"
#include "utils/guc.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"
#include "utils/rel.h"
#include "utils/snapmgr.h"
#include "utils/syscache.h"
#include "utils/typcache.h"

/* YB includes. */
#include "commands/extension.h"
#include "pg_yb_utils.h"

/*
 *	 Examine the RETURNS clause of the CREATE FUNCTION statement
 *	 and return information about it as *prorettype_p and *returnsSet.
 *
 * This is more complex than the average typename lookup because we want to
 * allow a shell type to be used, or even created if the specified return type
 * doesn't exist yet.  (Without this, there's no way to define the I/O procs
 * for a new type.)  But SQL function creation won't cope, so error out if
 * the target language is SQL.  (We do this here, not in the SQL-function
 * validator, so as not to produce a NOTICE and then an ERROR for the same
 * condition.)
 */
static void
compute_return_type(TypeName *returnType, Oid languageOid,
					Oid *prorettype_p, bool *returnsSet_p)
{
	Oid			rettype;
	Type		typtup;
	AclResult	aclresult;

	typtup = LookupTypeName(NULL, returnType, NULL, false);

	if (typtup)
	{
		if (!((Form_pg_type) GETSTRUCT(typtup))->typisdefined)
		{
			if (languageOid == SQLlanguageId)
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
						 errmsg("SQL function cannot return shell type %s",
								TypeNameToString(returnType))));
			else
				ereport(NOTICE,
						(errcode(ERRCODE_WRONG_OBJECT_TYPE),
						 errmsg("return type %s is only a shell",
								TypeNameToString(returnType))));
		}
		rettype = typeTypeId(typtup);
		ReleaseSysCache(typtup);
	}
	else
	{
		char	   *typnam = TypeNameToString(returnType);
		Oid			namespaceId;
		AclResult	aclresult;
		char	   *typname;
		ObjectAddress address;

		/*
		 * Only C-coded functions can be I/O functions.  We enforce this
		 * restriction here mainly to prevent littering the catalogs with
		 * shell types due to simple typos in user-defined function
		 * definitions.
		 */
		if (languageOid != INTERNALlanguageId &&
			languageOid != ClanguageId)
			ereport(ERROR,
					(errcode(ERRCODE_UNDEFINED_OBJECT),
					 errmsg("type \"%s\" does not exist", typnam)));

		/* Reject if there's typmod decoration, too */
		if (returnType->typmods != NIL)
			ereport(ERROR,
					(errcode(ERRCODE_SYNTAX_ERROR),
					 errmsg("type modifier cannot be specified for shell type \"%s\"",
							typnam)));

		/* Otherwise, go ahead and make a shell type */
		ereport(NOTICE,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				 errmsg("type \"%s\" is not yet defined", typnam),
				 errdetail("Creating a shell type definition.")));
		namespaceId = QualifiedNameGetCreationNamespace(returnType->names,
														&typname);
		aclresult = pg_namespace_aclcheck(namespaceId, GetUserId(),
										  ACL_CREATE);
		if (aclresult != ACLCHECK_OK)
			aclcheck_error(aclresult, OBJECT_SCHEMA,
						   get_namespace_name(namespaceId));
		address = TypeShellMake(typname, namespaceId, GetUserId());
		rettype = address.objectId;
		Assert(OidIsValid(rettype));
	}

	aclresult = pg_type_aclcheck(rettype, GetUserId(), ACL_USAGE);
	if (aclresult != ACLCHECK_OK)
		aclcheck_error_type(aclresult, rettype);

	*prorettype_p = rettype;
	*returnsSet_p = returnType->setof;
}

/*
 * Interpret the function parameter list of a CREATE FUNCTION,
 * CREATE PROCEDURE, or CREATE AGGREGATE statement.
 *
 * Input parameters:
 * parameters: list of FunctionParameter structs
 * languageOid: OID of function language (InvalidOid if it's CREATE AGGREGATE)
 * objtype: identifies type of object being created
 *
 * Results are stored into output parameters.  parameterTypes must always
 * be created, but the other arrays/lists can be NULL pointers if not needed.
 * variadicArgType is set to the variadic array type if there's a VARIADIC
 * parameter (there can be only one); or to InvalidOid if not.
 * requiredResultType is set to InvalidOid if there are no OUT parameters,
 * else it is set to the OID of the implied result type.
 */
void
interpret_function_parameter_list(ParseState *pstate,
								  List *parameters,
								  Oid languageOid,
								  ObjectType objtype,
								  oidvector **parameterTypes,
								  List **parameterTypes_list,
								  ArrayType **allParameterTypes,
								  ArrayType **parameterModes,
								  ArrayType **parameterNames,
								  List **inParameterNames_list,
								  List **parameterDefaults,
								  Oid *variadicArgType,
								  Oid *requiredResultType)
{
	int			parameterCount = list_length(parameters);
	Oid		   *inTypes;
	int			inCount = 0;
	Datum	   *allTypes;
	Datum	   *paramModes;
	Datum	   *paramNames;
	int			outCount = 0;
	int			varCount = 0;
	bool		have_names = false;
	bool		have_defaults = false;
	ListCell   *x;
	int			i;

	*variadicArgType = InvalidOid;	/* default result */
	*requiredResultType = InvalidOid;	/* default result */

	inTypes = (Oid *) palloc(parameterCount * sizeof(Oid));
	allTypes = (Datum *) palloc(parameterCount * sizeof(Datum));
	paramModes = (Datum *) palloc(parameterCount * sizeof(Datum));
	paramNames = (Datum *) palloc0(parameterCount * sizeof(Datum));
	*parameterDefaults = NIL;

	/* Scan the list and extract data into work arrays */
	i = 0;
	foreach(x, parameters)
	{
		FunctionParameter *fp = (FunctionParameter *) lfirst(x);
		TypeName   *t = fp->argType;
		FunctionParameterMode fpmode = fp->mode;
		bool		isinput = false;
		Oid			toid;
		Type		typtup;
		AclResult	aclresult;

		/* For our purposes here, a defaulted mode spec is identical to IN */
		if (fpmode == FUNC_PARAM_DEFAULT)
			fpmode = FUNC_PARAM_IN;

		typtup = LookupTypeName(NULL, t, NULL, false);
		if (typtup)
		{
			if (!((Form_pg_type) GETSTRUCT(typtup))->typisdefined)
			{
				/* As above, hard error if language is SQL */
				if (languageOid == SQLlanguageId)
					ereport(ERROR,
							(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
							 errmsg("SQL function cannot accept shell type %s",
									TypeNameToString(t))));
				/* We don't allow creating aggregates on shell types either */
				else if (objtype == OBJECT_AGGREGATE)
					ereport(ERROR,
							(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
							 errmsg("aggregate cannot accept shell type %s",
									TypeNameToString(t))));
				else
					ereport(NOTICE,
							(errcode(ERRCODE_WRONG_OBJECT_TYPE),
							 errmsg("argument type %s is only a shell",
									TypeNameToString(t))));
			}
			toid = typeTypeId(typtup);
			ReleaseSysCache(typtup);
		}
		else
		{
			ereport(ERROR,
					(errcode(ERRCODE_UNDEFINED_OBJECT),
					 errmsg("type %s does not exist",
							TypeNameToString(t))));
			toid = InvalidOid;	/* keep compiler quiet */
		}

		aclresult = pg_type_aclcheck(toid, GetUserId(), ACL_USAGE);
		if (aclresult != ACLCHECK_OK)
			aclcheck_error_type(aclresult, toid);

		if (t->setof)
		{
			if (objtype == OBJECT_AGGREGATE)
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
						 errmsg("aggregates cannot accept set arguments")));
			else if (objtype == OBJECT_PROCEDURE)
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
						 errmsg("procedures cannot accept set arguments")));
			else
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
						 errmsg("functions cannot accept set arguments")));
		}

		/* handle input parameters */
		if (fpmode != FUNC_PARAM_OUT && fpmode != FUNC_PARAM_TABLE)
		{
			/* other input parameters can't follow a VARIADIC parameter */
			if (varCount > 0)
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
						 errmsg("VARIADIC parameter must be the last input parameter")));
			inTypes[inCount++] = toid;
			isinput = true;
			if (parameterTypes_list)
				*parameterTypes_list = lappend_oid(*parameterTypes_list, toid);
		}

		/* handle output parameters */
		if (fpmode != FUNC_PARAM_IN && fpmode != FUNC_PARAM_VARIADIC)
		{
			if (objtype == OBJECT_PROCEDURE)
			{
				/*
				 * We disallow OUT-after-VARIADIC only for procedures.  While
				 * such a case causes no confusion in ordinary function calls,
				 * it would cause confusion in a CALL statement.
				 */
				if (varCount > 0)
					ereport(ERROR,
							(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
							 errmsg("VARIADIC parameter must be the last parameter")));
				/* Procedures with output parameters always return RECORD */
				*requiredResultType = RECORDOID;
			}
			else if (outCount == 0) /* save first output param's type */
				*requiredResultType = toid;
			outCount++;
		}

		if (fpmode == FUNC_PARAM_VARIADIC)
		{
			*variadicArgType = toid;
			varCount++;
			/* validate variadic parameter type */
			switch (toid)
			{
				case ANYARRAYOID:
				case ANYCOMPATIBLEARRAYOID:
				case ANYOID:
					/* okay */
					break;
				default:
					if (!OidIsValid(get_element_type(toid)))
						ereport(ERROR,
								(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
								 errmsg("VARIADIC parameter must be an array")));
					break;
			}
		}

		allTypes[i] = ObjectIdGetDatum(toid);

		paramModes[i] = CharGetDatum(fpmode);

		if (fp->name && fp->name[0])
		{
			ListCell   *px;

			/*
			 * As of Postgres 9.0 we disallow using the same name for two
			 * input or two output function parameters.  Depending on the
			 * function's language, conflicting input and output names might
			 * be bad too, but we leave it to the PL to complain if so.
			 */
			foreach(px, parameters)
			{
				FunctionParameter *prevfp = (FunctionParameter *) lfirst(px);
				FunctionParameterMode prevfpmode;

				if (prevfp == fp)
					break;
				/* as above, default mode is IN */
				prevfpmode = prevfp->mode;
				if (prevfpmode == FUNC_PARAM_DEFAULT)
					prevfpmode = FUNC_PARAM_IN;
				/* pure in doesn't conflict with pure out */
				if ((fpmode == FUNC_PARAM_IN ||
					 fpmode == FUNC_PARAM_VARIADIC) &&
					(prevfpmode == FUNC_PARAM_OUT ||
					 prevfpmode == FUNC_PARAM_TABLE))
					continue;
				if ((prevfpmode == FUNC_PARAM_IN ||
					 prevfpmode == FUNC_PARAM_VARIADIC) &&
					(fpmode == FUNC_PARAM_OUT ||
					 fpmode == FUNC_PARAM_TABLE))
					continue;
				if (prevfp->name && prevfp->name[0] &&
					strcmp(prevfp->name, fp->name) == 0)
					ereport(ERROR,
							(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
							 errmsg("parameter name \"%s\" used more than once",
									fp->name)));
			}

			paramNames[i] = CStringGetTextDatum(fp->name);
			have_names = true;
		}

		if (inParameterNames_list)
			*inParameterNames_list = lappend(*inParameterNames_list, makeString(fp->name ? fp->name : pstrdup("")));

		if (fp->defexpr)
		{
			Node	   *def;

			if (!isinput)
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
						 errmsg("only input parameters can have default values")));

			def = transformExpr(pstate, fp->defexpr,
								EXPR_KIND_FUNCTION_DEFAULT);
			def = coerce_to_specific_type(pstate, def, toid, "DEFAULT");
			assign_expr_collations(pstate, def);

			/*
			 * Make sure no variables are referred to (this is probably dead
			 * code now that add_missing_from is history).
			 */
			if (list_length(pstate->p_rtable) != 0 ||
				contain_var_clause(def))
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_COLUMN_REFERENCE),
						 errmsg("cannot use table references in parameter default value")));

			/*
			 * transformExpr() should have already rejected subqueries,
			 * aggregates, and window functions, based on the EXPR_KIND_ for a
			 * default expression.
			 *
			 * It can't return a set either --- but coerce_to_specific_type
			 * already checked that for us.
			 *
			 * Note: the point of these restrictions is to ensure that an
			 * expression that, on its face, hasn't got subplans, aggregates,
			 * etc cannot suddenly have them after function default arguments
			 * are inserted.
			 */

			*parameterDefaults = lappend(*parameterDefaults, def);
			have_defaults = true;
		}
		else
		{
			if (isinput && have_defaults)
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
						 errmsg("input parameters after one with a default value must also have defaults")));

			/*
			 * For procedures, we also can't allow OUT parameters after one
			 * with a default, because the same sort of confusion arises in a
			 * CALL statement.
			 */
			if (objtype == OBJECT_PROCEDURE && have_defaults)
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
						 errmsg("procedure OUT parameters cannot appear after one with a default value")));
		}

		i++;
	}

	/* Now construct the proper outputs as needed */
	*parameterTypes = buildoidvector(inTypes, inCount);

	if (outCount > 0 || varCount > 0)
	{
		*allParameterTypes = construct_array(allTypes, parameterCount, OIDOID,
											 sizeof(Oid), true, TYPALIGN_INT);
		*parameterModes = construct_array(paramModes, parameterCount, CHAROID,
										  1, true, TYPALIGN_CHAR);
		if (outCount > 1)
			*requiredResultType = RECORDOID;
		/* otherwise we set requiredResultType correctly above */
	}
	else
	{
		*allParameterTypes = NULL;
		*parameterModes = NULL;
	}

	if (have_names)
	{
		for (i = 0; i < parameterCount; i++)
		{
			if (paramNames[i] == PointerGetDatum(NULL))
				paramNames[i] = CStringGetTextDatum("");
		}
		*parameterNames = construct_array(paramNames, parameterCount, TEXTOID,
										  -1, false, TYPALIGN_INT);
	}
	else
		*parameterNames = NULL;
}


/*
 * Recognize one of the options that can be passed to both CREATE
 * FUNCTION and ALTER FUNCTION and return it via one of the out
 * parameters. Returns true if the passed option was recognized. If
 * the out parameter we were going to assign to points to non-NULL,
 * raise a duplicate-clause error.  (We don't try to detect duplicate
 * SET parameters though --- if you're redundant, the last one wins.)
 */
static bool
compute_common_attribute(ParseState *pstate,
						 bool is_procedure,
						 DefElem *defel,
						 DefElem **volatility_item,
						 DefElem **strict_item,
						 DefElem **security_item,
						 DefElem **leakproof_item,
						 List **set_items,
						 DefElem **cost_item,
						 DefElem **rows_item,
						 DefElem **support_item,
						 DefElem **parallel_item)
{
	if (strcmp(defel->defname, "volatility") == 0)
	{
		if (is_procedure)
			goto procedure_error;
		if (*volatility_item)
			errorConflictingDefElem(defel, pstate);

		*volatility_item = defel;
	}
	else if (strcmp(defel->defname, "strict") == 0)
	{
		if (is_procedure)
			goto procedure_error;
		if (*strict_item)
			errorConflictingDefElem(defel, pstate);

		*strict_item = defel;
	}
	else if (strcmp(defel->defname, "security") == 0)
	{
		if (*security_item)
			errorConflictingDefElem(defel, pstate);

		*security_item = defel;
	}
	else if (strcmp(defel->defname, "leakproof") == 0)
	{
		if (is_procedure)
			goto procedure_error;
		if (*leakproof_item)
			errorConflictingDefElem(defel, pstate);

		*leakproof_item = defel;
	}
	else if (strcmp(defel->defname, "set") == 0)
	{
		*set_items = lappend(*set_items, defel->arg);
	}
	else if (strcmp(defel->defname, "cost") == 0)
	{
		if (is_procedure)
			goto procedure_error;
		if (*cost_item)
			errorConflictingDefElem(defel, pstate);

		*cost_item = defel;
	}
	else if (strcmp(defel->defname, "rows") == 0)
	{
		if (is_procedure)
			goto procedure_error;
		if (*rows_item)
			errorConflictingDefElem(defel, pstate);

		*rows_item = defel;
	}
	else if (strcmp(defel->defname, "support") == 0)
	{
		if (is_procedure)
			goto procedure_error;
		if (*support_item)
			errorConflictingDefElem(defel, pstate);

		*support_item = defel;
	}
	else if (strcmp(defel->defname, "parallel") == 0)
	{
		if (is_procedure)
			goto procedure_error;
		if (*parallel_item)
			errorConflictingDefElem(defel, pstate);

		*parallel_item = defel;
	}
	else
		return false;

	/* Recognized an option */
	return true;

procedure_error:
	ereport(ERROR,
			(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
			 errmsg("invalid attribute in procedure definition"),
			 parser_errposition(pstate, defel->location)));
	return false;
}

static char
interpret_func_volatility(DefElem *defel)
{
	char	   *str = strVal(defel->arg);

	if (strcmp(str, "immutable") == 0)
		return PROVOLATILE_IMMUTABLE;
	else if (strcmp(str, "stable") == 0)
		return PROVOLATILE_STABLE;
	else if (strcmp(str, "volatile") == 0)
		return PROVOLATILE_VOLATILE;
	else
	{
		elog(ERROR, "invalid volatility \"%s\"", str);
		return 0;				/* keep compiler quiet */
	}
}

static char
interpret_func_parallel(DefElem *defel)
{
	char	   *str = strVal(defel->arg);

	if (strcmp(str, "safe") == 0)
		return PROPARALLEL_SAFE;
	else if (strcmp(str, "unsafe") == 0)
		return PROPARALLEL_UNSAFE;
	else if (strcmp(str, "restricted") == 0)
		return PROPARALLEL_RESTRICTED;
	else
	{
		ereport(ERROR,
				(errcode(ERRCODE_SYNTAX_ERROR),
				 errmsg("parameter \"parallel\" must be SAFE, RESTRICTED, or UNSAFE")));
		return PROPARALLEL_UNSAFE;	/* keep compiler quiet */
	}
}

/*
 * Update a proconfig value according to a list of VariableSetStmt items.
 *
 * The input and result may be NULL to signify a null entry.
 */
static ArrayType *
update_proconfig_value(ArrayType *a, List *set_items)
{
	ListCell   *l;

	foreach(l, set_items)
	{
		VariableSetStmt *sstmt = lfirst_node(VariableSetStmt, l);

		if (sstmt->kind == VAR_RESET_ALL)
			a = NULL;
		else
		{
			char	   *valuestr = ExtractSetVariableArgs(sstmt);

			if (valuestr)
				a = GUCArrayAdd(a, sstmt->name, valuestr);
			else				/* RESET */
				a = GUCArrayDelete(a, sstmt->name);
		}
	}

	return a;
}

static Oid
interpret_func_support(DefElem *defel)
{
	List	   *procName = defGetQualifiedName(defel);
	Oid			procOid;
	Oid			argList[1];

	/*
	 * Support functions always take one INTERNAL argument and return
	 * INTERNAL.
	 */
	argList[0] = INTERNALOID;

	procOid = LookupFuncName(procName, 1, argList, true);
	if (!OidIsValid(procOid))
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_FUNCTION),
				 errmsg("function %s does not exist",
						func_signature_string(procName, 1, NIL, argList))));

	if (get_func_rettype(procOid) != INTERNALOID)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
				 errmsg("support function %s must return type %s",
						NameListToString(procName), "internal")));

	/*
	 * Someday we might want an ACL check here; but for now, we insist that
	 * you be superuser to specify a support function, so privilege on the
	 * support function is moot.
	 */
	if (!superuser() && !(IsYbExtensionUser(GetUserId()) && creating_extension))
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("must be superuser to specify a support function")));

	return procOid;
}


/*
 * Dissect the list of options assembled in gram.y into function
 * attributes.
 */
static void
compute_function_attributes(ParseState *pstate,
							bool is_procedure,
							List *options,
							List **as,
							char **language,
							Node **transform,
							bool *windowfunc_p,
							char *volatility_p,
							bool *strict_p,
							bool *security_definer,
							bool *leakproof_p,
							ArrayType **proconfig,
							float4 *procost,
							float4 *prorows,
							Oid *prosupport,
							char *parallel_p)
{
	ListCell   *option;
	DefElem    *as_item = NULL;
	DefElem    *language_item = NULL;
	DefElem    *transform_item = NULL;
	DefElem    *windowfunc_item = NULL;
	DefElem    *volatility_item = NULL;
	DefElem    *strict_item = NULL;
	DefElem    *security_item = NULL;
	DefElem    *leakproof_item = NULL;
	List	   *set_items = NIL;
	DefElem    *cost_item = NULL;
	DefElem    *rows_item = NULL;
	DefElem    *support_item = NULL;
	DefElem    *parallel_item = NULL;

	foreach(option, options)
	{
		DefElem    *defel = (DefElem *) lfirst(option);

		if (strcmp(defel->defname, "as") == 0)
		{
			if (as_item)
				errorConflictingDefElem(defel, pstate);
			as_item = defel;
		}
		else if (strcmp(defel->defname, "language") == 0)
		{
			if (language_item)
				errorConflictingDefElem(defel, pstate);
			language_item = defel;
		}
		else if (strcmp(defel->defname, "transform") == 0)
		{
			if (transform_item)
				errorConflictingDefElem(defel, pstate);
			transform_item = defel;
		}
		else if (strcmp(defel->defname, "window") == 0)
		{
			if (windowfunc_item)
				errorConflictingDefElem(defel, pstate);
			if (is_procedure)
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
						 errmsg("invalid attribute in procedure definition"),
						 parser_errposition(pstate, defel->location)));
			windowfunc_item = defel;
		}
		else if (compute_common_attribute(pstate,
										  is_procedure,
										  defel,
										  &volatility_item,
										  &strict_item,
										  &security_item,
										  &leakproof_item,
										  &set_items,
										  &cost_item,
										  &rows_item,
										  &support_item,
										  &parallel_item))
		{
			/* recognized common option */
			continue;
		}
		else
			elog(ERROR, "option \"%s\" not recognized",
				 defel->defname);
	}

	if (as_item)
		*as = (List *) as_item->arg;
	if (language_item)
		*language = strVal(language_item->arg);
	if (transform_item)
		*transform = transform_item->arg;
	if (windowfunc_item)
		*windowfunc_p = boolVal(windowfunc_item->arg);
	if (volatility_item)
		*volatility_p = interpret_func_volatility(volatility_item);
	if (strict_item)
		*strict_p = boolVal(strict_item->arg);
	if (security_item)
		*security_definer = boolVal(security_item->arg);
	if (leakproof_item)
		*leakproof_p = boolVal(leakproof_item->arg);
	if (set_items)
		*proconfig = update_proconfig_value(NULL, set_items);
	if (cost_item)
	{
		*procost = defGetNumeric(cost_item);
		if (*procost <= 0)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("COST must be positive")));
	}
	if (rows_item)
	{
		*prorows = defGetNumeric(rows_item);
		if (*prorows <= 0)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("ROWS must be positive")));
	}
	if (support_item)
		*prosupport = interpret_func_support(support_item);
	if (parallel_item)
		*parallel_p = interpret_func_parallel(parallel_item);
}


/*
 * For a dynamically linked C language object, the form of the clause is
 *
 *	   AS <object file name> [, <link symbol name> ]
 *
 * In all other cases
 *
 *	   AS <object reference, or sql code>
 */
static void
interpret_AS_clause(Oid languageOid, const char *languageName,
					char *funcname, List *as, Node *sql_body_in,
					List *parameterTypes, List *inParameterNames,
					char **prosrc_str_p, char **probin_str_p,
					Node **sql_body_out,
					const char *queryString)
{
	if (!sql_body_in && !as)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
				 errmsg("no function body specified")));

	if (sql_body_in && as)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
				 errmsg("duplicate function body specified")));

	if (sql_body_in && languageOid != SQLlanguageId)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
				 errmsg("inline SQL function body only valid for language SQL")));

	*sql_body_out = NULL;

	if (languageOid == ClanguageId)
	{
		/*
		 * For "C" language, store the file name in probin and, when given,
		 * the link symbol name in prosrc.  If link symbol is omitted,
		 * substitute procedure name.  We also allow link symbol to be
		 * specified as "-", since that was the habit in PG versions before
		 * 8.4, and there might be dump files out there that don't translate
		 * that back to "omitted".
		 */
		*probin_str_p = strVal(linitial(as));
		if (list_length(as) == 1)
			*prosrc_str_p = funcname;
		else
		{
			*prosrc_str_p = strVal(lsecond(as));
			if (strcmp(*prosrc_str_p, "-") == 0)
				*prosrc_str_p = funcname;
		}
	}
	else if (sql_body_in)
	{
		SQLFunctionParseInfoPtr pinfo;

		pinfo = (SQLFunctionParseInfoPtr) palloc0(sizeof(SQLFunctionParseInfo));

		pinfo->fname = funcname;
		pinfo->nargs = list_length(parameterTypes);
		pinfo->argtypes = (Oid *) palloc(pinfo->nargs * sizeof(Oid));
		pinfo->argnames = (char **) palloc(pinfo->nargs * sizeof(char *));
		for (int i = 0; i < list_length(parameterTypes); i++)
		{
			char	   *s = strVal(list_nth(inParameterNames, i));

			pinfo->argtypes[i] = list_nth_oid(parameterTypes, i);
			if (IsPolymorphicType(pinfo->argtypes[i]))
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
						 errmsg("SQL function with unquoted function body cannot have polymorphic arguments")));

			if (s[0] != '\0')
				pinfo->argnames[i] = s;
			else
				pinfo->argnames[i] = NULL;
		}

		if (IsA(sql_body_in, List))
		{
			List	   *stmts = linitial_node(List, castNode(List, sql_body_in));
			ListCell   *lc;
			List	   *transformed_stmts = NIL;

			foreach(lc, stmts)
			{
				Node	   *stmt = lfirst(lc);
				Query	   *q;
				ParseState *pstate = make_parsestate(NULL);

				pstate->p_sourcetext = queryString;
				sql_fn_parser_setup(pstate, pinfo);
				q = transformStmt(pstate, stmt);
				if (q->commandType == CMD_UTILITY)
					ereport(ERROR,
							errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
							errmsg("%s is not yet supported in unquoted SQL function body",
								   GetCommandTagName(CreateCommandTag(q->utilityStmt))));
				transformed_stmts = lappend(transformed_stmts, q);
				free_parsestate(pstate);
			}

			*sql_body_out = (Node *) list_make1(transformed_stmts);
		}
		else
		{
			Query	   *q;
			ParseState *pstate = make_parsestate(NULL);

			pstate->p_sourcetext = queryString;
			sql_fn_parser_setup(pstate, pinfo);
			q = transformStmt(pstate, sql_body_in);
			if (q->commandType == CMD_UTILITY)
				ereport(ERROR,
						errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
						errmsg("%s is not yet supported in unquoted SQL function body",
							   GetCommandTagName(CreateCommandTag(q->utilityStmt))));
			free_parsestate(pstate);

			*sql_body_out = (Node *) q;
		}

		/*
		 * We must put something in prosrc.  For the moment, just record an
		 * empty string.  It might be useful to store the original text of the
		 * CREATE FUNCTION statement --- but to make actual use of that in
		 * error reports, we'd also have to adjust readfuncs.c to not throw
		 * away node location fields when reading prosqlbody.
		 */
		*prosrc_str_p = pstrdup("");

		/* But we definitely don't need probin. */
		*probin_str_p = NULL;
	}
	else
	{
		/* Everything else wants the given string in prosrc. */
		*prosrc_str_p = strVal(linitial(as));
		*probin_str_p = NULL;

		if (list_length(as) != 1)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
					 errmsg("only one AS item needed for language \"%s\"",
							languageName)));

		if (languageOid == INTERNALlanguageId)
		{
			/*
			 * In PostgreSQL versions before 6.5, the SQL name of the created
			 * function could not be different from the internal name, and
			 * "prosrc" wasn't used.  So there is code out there that does
			 * CREATE FUNCTION xyz AS '' LANGUAGE internal. To preserve some
			 * modicum of backwards compatibility, accept an empty "prosrc"
			 * value as meaning the supplied SQL function name.
			 */
			if (strlen(*prosrc_str_p) == 0)
				*prosrc_str_p = funcname;
		}
	}
}


/*
 * CreateFunction
 *	 Execute a CREATE FUNCTION (or CREATE PROCEDURE) utility statement.
 */
ObjectAddress
CreateFunction(ParseState *pstate, CreateFunctionStmt *stmt)
{
	char	   *probin_str;
	char	   *prosrc_str;
	Node	   *prosqlbody;
	Oid			prorettype;
	bool		returnsSet;
	char	   *language;
	Oid			languageOid;
	Oid			languageValidator;
	Node	   *transformDefElem = NULL;
	char	   *funcname;
	Oid			namespaceId;
	AclResult	aclresult;
	oidvector  *parameterTypes;
	List	   *parameterTypes_list = NIL;
	ArrayType  *allParameterTypes;
	ArrayType  *parameterModes;
	ArrayType  *parameterNames;
	List	   *inParameterNames_list = NIL;
	List	   *parameterDefaults;
	Oid			variadicArgType;
	List	   *trftypes_list = NIL;
	ArrayType  *trftypes;
	Oid			requiredResultType;
	bool		isWindowFunc,
				isStrict,
				security,
				isLeakProof;
	char		volatility;
	ArrayType  *proconfig;
	float4		procost;
	float4		prorows;
	Oid			prosupport;
	HeapTuple	languageTuple;
	Form_pg_language languageStruct;
	List	   *as_clause;
	char		parallel;

	/* Convert list of names to a name and namespace */
	namespaceId = QualifiedNameGetCreationNamespace(stmt->funcname,
													&funcname);

	/* Check we have creation rights in target namespace */
	aclresult = pg_namespace_aclcheck(namespaceId, GetUserId(), ACL_CREATE);
	if (aclresult != ACLCHECK_OK && !IsYbDbAdminUser(GetUserId()))
		aclcheck_error(aclresult, OBJECT_SCHEMA,
					   get_namespace_name(namespaceId));

	/* Set default attributes */
	as_clause = NIL;
	language = NULL;
	isWindowFunc = false;
	isStrict = false;
	security = false;
	isLeakProof = false;
	volatility = PROVOLATILE_VOLATILE;
	proconfig = NULL;
	procost = -1;				/* indicates not set */
	prorows = -1;				/* indicates not set */
	prosupport = InvalidOid;
	parallel = PROPARALLEL_UNSAFE;

	/* Extract non-default attributes from stmt->options list */
	compute_function_attributes(pstate,
								stmt->is_procedure,
								stmt->options,
								&as_clause, &language, &transformDefElem,
								&isWindowFunc, &volatility,
								&isStrict, &security, &isLeakProof,
								&proconfig, &procost, &prorows,
								&prosupport, &parallel);

	if (!language)
	{
		if (stmt->sql_body)
			language = "sql";
		else
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
					 errmsg("no language specified")));
	}

	/* Look up the language and validate permissions */
	languageTuple = SearchSysCache1(LANGNAME, PointerGetDatum(language));
	if (!HeapTupleIsValid(languageTuple))
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				 errmsg("language \"%s\" does not exist", language),
				 (extension_file_exists(language) ?
				  errhint("Use CREATE EXTENSION to load the language into the database.") : 0)));

	languageStruct = (Form_pg_language) GETSTRUCT(languageTuple);
	languageOid = languageStruct->oid;

	if (languageStruct->lanpltrusted)
	{
		/* if trusted language, need USAGE privilege */
		AclResult	aclresult;

		aclresult = pg_language_aclcheck(languageOid, GetUserId(), ACL_USAGE);
		if (aclresult != ACLCHECK_OK && !IsYbDbAdminUser(GetUserId()))
			aclcheck_error(aclresult, OBJECT_LANGUAGE,
						   NameStr(languageStruct->lanname));
	}
	else
	{
		/*
		 * If untrusted language, must be superuser, or someone with the
		 * yb_extension role in the midst of creating an extension.
		 */
		if (!(IsYbExtensionUser(GetUserId()) && creating_extension) &&
			!superuser())
			aclcheck_error(ACLCHECK_NO_PRIV, OBJECT_LANGUAGE,
						   NameStr(languageStruct->lanname));
	}

	languageValidator = languageStruct->lanvalidator;

	ReleaseSysCache(languageTuple);

	/*
	 * Only superuser is allowed to create leakproof functions because
	 * leakproof functions can see tuples which have not yet been filtered out
	 * by security barrier views or row-level security policies.
	 */
	if (isLeakProof && !superuser())
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("only superuser can define a leakproof function")));

	if (transformDefElem)
	{
		ListCell   *lc;

		foreach(lc, castNode(List, transformDefElem))
		{
			Oid			typeid = typenameTypeId(NULL,
												lfirst_node(TypeName, lc));
			Oid			elt = get_base_element_type(typeid);

			typeid = elt ? elt : typeid;

			get_transform_oid(typeid, languageOid, false);
			trftypes_list = lappend_oid(trftypes_list, typeid);
		}
	}

	/*
	 * Convert remaining parameters of CREATE to form wanted by
	 * ProcedureCreate.
	 */
	interpret_function_parameter_list(pstate,
									  stmt->parameters,
									  languageOid,
									  stmt->is_procedure ? OBJECT_PROCEDURE : OBJECT_FUNCTION,
									  &parameterTypes,
									  &parameterTypes_list,
									  &allParameterTypes,
									  &parameterModes,
									  &parameterNames,
									  &inParameterNames_list,
									  &parameterDefaults,
									  &variadicArgType,
									  &requiredResultType);

	if (stmt->is_procedure)
	{
		Assert(!stmt->returnType);
		prorettype = requiredResultType ? requiredResultType : VOIDOID;
		returnsSet = false;
	}
	else if (stmt->returnType)
	{
		/* explicit RETURNS clause */
		compute_return_type(stmt->returnType, languageOid,
							&prorettype, &returnsSet);
		if (OidIsValid(requiredResultType) && prorettype != requiredResultType)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
					 errmsg("function result type must be %s because of OUT parameters",
							format_type_be(requiredResultType))));
	}
	else if (OidIsValid(requiredResultType))
	{
		/* default RETURNS clause from OUT parameters */
		prorettype = requiredResultType;
		returnsSet = false;
	}
	else
	{
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
				 errmsg("function result type must be specified")));
		/* Alternative possibility: default to RETURNS VOID */
		prorettype = VOIDOID;
		returnsSet = false;
	}

	if (list_length(trftypes_list) > 0)
	{
		ListCell   *lc;
		Datum	   *arr;
		int			i;

		arr = palloc(list_length(trftypes_list) * sizeof(Datum));
		i = 0;
		foreach(lc, trftypes_list)
			arr[i++] = ObjectIdGetDatum(lfirst_oid(lc));
		trftypes = construct_array(arr, list_length(trftypes_list),
								   OIDOID, sizeof(Oid), true, TYPALIGN_INT);
	}
	else
	{
		/* store SQL NULL instead of empty array */
		trftypes = NULL;
	}

	interpret_AS_clause(languageOid, language, funcname, as_clause, stmt->sql_body,
						parameterTypes_list, inParameterNames_list,
						&prosrc_str, &probin_str, &prosqlbody,
						pstate->p_sourcetext);

	/*
	 * Set default values for COST and ROWS depending on other parameters;
	 * reject ROWS if it's not returnsSet.  NB: pg_dump knows these default
	 * values, keep it in sync if you change them.
	 */
	if (procost < 0)
	{
		/* SQL and PL-language functions are assumed more expensive */
		if (languageOid == INTERNALlanguageId ||
			languageOid == ClanguageId)
			procost = 1;
		else
			procost = 100;
	}
	if (prorows < 0)
	{
		if (returnsSet)
			prorows = 1000;
		else
			prorows = 0;		/* dummy value if not returnsSet */
	}
	else if (!returnsSet)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("ROWS is not applicable when function does not return a set")));

	/*
	 * And now that we have all the parameters, and know we're permitted to do
	 * so, go ahead and create the function.
	 */
	return ProcedureCreate(funcname,
						   namespaceId,
						   stmt->replace,
						   returnsSet,
						   prorettype,
						   GetUserId(),
						   languageOid,
						   languageValidator,
						   prosrc_str,	/* converted to text later */
						   probin_str,	/* converted to text later */
						   prosqlbody,
						   stmt->is_procedure ? PROKIND_PROCEDURE : (isWindowFunc ? PROKIND_WINDOW : PROKIND_FUNCTION),
						   security,
						   isLeakProof,
						   isStrict,
						   volatility,
						   parallel,
						   parameterTypes,
						   PointerGetDatum(allParameterTypes),
						   PointerGetDatum(parameterModes),
						   PointerGetDatum(parameterNames),
						   parameterDefaults,
						   PointerGetDatum(trftypes),
						   PointerGetDatum(proconfig),
						   prosupport,
						   procost,
						   prorows);
}

/*
 * Guts of function deletion.
 *
 * Note: this is also used for aggregate deletion, since the OIDs of
 * both functions and aggregates point to pg_proc.
 */
void
RemoveFunctionById(Oid funcOid)
{
	Relation	relation;
	HeapTuple	tup;
	char		prokind;

	/*
	 * Delete the pg_proc tuple.
	 */
	relation = table_open(ProcedureRelationId, RowExclusiveLock);

	tup = SearchSysCache1(PROCOID, ObjectIdGetDatum(funcOid));
	if (!HeapTupleIsValid(tup)) /* should not happen */
		elog(ERROR, "cache lookup failed for function %u", funcOid);

	prokind = ((Form_pg_proc) GETSTRUCT(tup))->prokind;

	CatalogTupleDelete(relation, tup);

	ReleaseSysCache(tup);

	table_close(relation, RowExclusiveLock);

	pgstat_drop_function(funcOid);

	/*
	 * If there's a pg_aggregate tuple, delete that too.
	 */
	if (prokind == PROKIND_AGGREGATE)
	{
		relation = table_open(AggregateRelationId, RowExclusiveLock);

		tup = SearchSysCache1(AGGFNOID, ObjectIdGetDatum(funcOid));
		if (!HeapTupleIsValid(tup)) /* should not happen */
			elog(ERROR, "cache lookup failed for pg_aggregate tuple for function %u", funcOid);

		CatalogTupleDelete(relation, tup);

		ReleaseSysCache(tup);

		table_close(relation, RowExclusiveLock);
	}
}

/*
 * Implements the ALTER FUNCTION utility command (except for the
 * RENAME and OWNER clauses, which are handled as part of the generic
 * ALTER framework).
 */
ObjectAddress
AlterFunction(ParseState *pstate, AlterFunctionStmt *stmt)
{
	HeapTuple	tup;
	Oid			funcOid;
	Form_pg_proc procForm;
	bool		is_procedure;
	Relation	rel;
	ListCell   *l;
	DefElem    *volatility_item = NULL;
	DefElem    *strict_item = NULL;
	DefElem    *security_def_item = NULL;
	DefElem    *leakproof_item = NULL;
	List	   *set_items = NIL;
	DefElem    *cost_item = NULL;
	DefElem    *rows_item = NULL;
	DefElem    *support_item = NULL;
	DefElem    *parallel_item = NULL;
	ObjectAddress address;

	rel = table_open(ProcedureRelationId, RowExclusiveLock);

	funcOid = LookupFuncWithArgs(stmt->objtype, stmt->func, false);

	ObjectAddressSet(address, ProcedureRelationId, funcOid);

	tup = SearchSysCacheCopy1(PROCOID, ObjectIdGetDatum(funcOid));
	if (!HeapTupleIsValid(tup)) /* should not happen */
		elog(ERROR, "cache lookup failed for function %u", funcOid);

	procForm = (Form_pg_proc) GETSTRUCT(tup);

	/* Permission check: must own function */
	if (!pg_proc_ownercheck(funcOid, GetUserId()) &&
		!IsYbDbAdminUser(GetUserId()))
		aclcheck_error(ACLCHECK_NOT_OWNER, stmt->objtype,
					   NameListToString(stmt->func->objname));

	if (procForm->prokind == PROKIND_AGGREGATE)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("\"%s\" is an aggregate function",
						NameListToString(stmt->func->objname))));

	is_procedure = (procForm->prokind == PROKIND_PROCEDURE);

	/* Examine requested actions. */
	foreach(l, stmt->actions)
	{
		DefElem    *defel = (DefElem *) lfirst(l);

		if (compute_common_attribute(pstate,
									 is_procedure,
									 defel,
									 &volatility_item,
									 &strict_item,
									 &security_def_item,
									 &leakproof_item,
									 &set_items,
									 &cost_item,
									 &rows_item,
									 &support_item,
									 &parallel_item) == false)
			elog(ERROR, "option \"%s\" not recognized", defel->defname);
	}

	if (volatility_item)
		procForm->provolatile = interpret_func_volatility(volatility_item);
	if (strict_item)
		procForm->proisstrict = boolVal(strict_item->arg);
	if (security_def_item)
		procForm->prosecdef = boolVal(security_def_item->arg);
	if (leakproof_item)
	{
		procForm->proleakproof = boolVal(leakproof_item->arg);
		if (procForm->proleakproof && !superuser())
			ereport(ERROR,
					(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
					 errmsg("only superuser can define a leakproof function")));
	}
	if (cost_item)
	{
		procForm->procost = defGetNumeric(cost_item);
		if (procForm->procost <= 0)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("COST must be positive")));
	}
	if (rows_item)
	{
		procForm->prorows = defGetNumeric(rows_item);
		if (procForm->prorows <= 0)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("ROWS must be positive")));
		if (!procForm->proretset)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("ROWS is not applicable when function does not return a set")));
	}
	if (support_item)
	{
		/* interpret_func_support handles the privilege check */
		Oid			newsupport = interpret_func_support(support_item);

		/* Add or replace dependency on support function */
		if (OidIsValid(procForm->prosupport))
			changeDependencyFor(ProcedureRelationId, funcOid,
								ProcedureRelationId, procForm->prosupport,
								newsupport);
		else
		{
			ObjectAddress referenced;

			referenced.classId = ProcedureRelationId;
			referenced.objectId = newsupport;
			referenced.objectSubId = 0;
			recordDependencyOn(&address, &referenced, DEPENDENCY_NORMAL);
		}

		procForm->prosupport = newsupport;
	}
	if (parallel_item)
		procForm->proparallel = interpret_func_parallel(parallel_item);
	if (set_items)
	{
		Datum		datum;
		bool		isnull;
		ArrayType  *a;
		Datum		repl_val[Natts_pg_proc];
		bool		repl_null[Natts_pg_proc];
		bool		repl_repl[Natts_pg_proc];

		/* extract existing proconfig setting */
		datum = SysCacheGetAttr(PROCOID, tup, Anum_pg_proc_proconfig, &isnull);
		a = isnull ? NULL : DatumGetArrayTypeP(datum);

		/* update according to each SET or RESET item, left to right */
		a = update_proconfig_value(a, set_items);

		/* update the tuple */
		memset(repl_repl, false, sizeof(repl_repl));
		repl_repl[Anum_pg_proc_proconfig - 1] = true;

		if (a == NULL)
		{
			repl_val[Anum_pg_proc_proconfig - 1] = (Datum) 0;
			repl_null[Anum_pg_proc_proconfig - 1] = true;
		}
		else
		{
			repl_val[Anum_pg_proc_proconfig - 1] = PointerGetDatum(a);
			repl_null[Anum_pg_proc_proconfig - 1] = false;
		}

		tup = heap_modify_tuple(tup, RelationGetDescr(rel),
								repl_val, repl_null, repl_repl);
	}
	/* DO NOT put more touches of procForm below here; it's now dangling. */

	/* Do the update */
	CatalogTupleUpdate(rel, &tup->t_self, tup);

	InvokeObjectPostAlterHook(ProcedureRelationId, funcOid, 0);

	table_close(rel, NoLock);
	heap_freetuple(tup);

	return address;
}


/*
 * CREATE CAST
 */
ObjectAddress
CreateCast(CreateCastStmt *stmt)
{
	Oid			sourcetypeid;
	Oid			targettypeid;
	char		sourcetyptype;
	char		targettyptype;
	Oid			funcid;
	int			nargs;
	char		castcontext;
	char		castmethod;
	HeapTuple	tuple;
	AclResult	aclresult;
	ObjectAddress myself;

	sourcetypeid = typenameTypeId(NULL, stmt->sourcetype);
	targettypeid = typenameTypeId(NULL, stmt->targettype);
	sourcetyptype = get_typtype(sourcetypeid);
	targettyptype = get_typtype(targettypeid);

	/* No pseudo-types allowed */
	if (sourcetyptype == TYPTYPE_PSEUDO)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("source data type %s is a pseudo-type",
						TypeNameToString(stmt->sourcetype))));

	if (targettyptype == TYPTYPE_PSEUDO)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("target data type %s is a pseudo-type",
						TypeNameToString(stmt->targettype))));

	/* Permission check */
	if (!pg_type_ownercheck(sourcetypeid, GetUserId())
		&& !pg_type_ownercheck(targettypeid, GetUserId()))
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("must be owner of type %s or type %s",
						format_type_be(sourcetypeid),
						format_type_be(targettypeid))));

	aclresult = pg_type_aclcheck(sourcetypeid, GetUserId(), ACL_USAGE);
	if (aclresult != ACLCHECK_OK)
		aclcheck_error_type(aclresult, sourcetypeid);

	aclresult = pg_type_aclcheck(targettypeid, GetUserId(), ACL_USAGE);
	if (aclresult != ACLCHECK_OK)
		aclcheck_error_type(aclresult, targettypeid);

	/* Domains are allowed for historical reasons, but we warn */
	if (sourcetyptype == TYPTYPE_DOMAIN)
		ereport(WARNING,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("cast will be ignored because the source data type is a domain")));

	else if (targettyptype == TYPTYPE_DOMAIN)
		ereport(WARNING,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("cast will be ignored because the target data type is a domain")));

	/* Determine the cast method */
	if (stmt->func != NULL)
		castmethod = COERCION_METHOD_FUNCTION;
	else if (stmt->inout)
		castmethod = COERCION_METHOD_INOUT;
	else
		castmethod = COERCION_METHOD_BINARY;

	if (castmethod == COERCION_METHOD_FUNCTION)
	{
		Form_pg_proc procstruct;

		funcid = LookupFuncWithArgs(OBJECT_FUNCTION, stmt->func, false);

		tuple = SearchSysCache1(PROCOID, ObjectIdGetDatum(funcid));
		if (!HeapTupleIsValid(tuple))
			elog(ERROR, "cache lookup failed for function %u", funcid);

		procstruct = (Form_pg_proc) GETSTRUCT(tuple);
		nargs = procstruct->pronargs;
		if (nargs < 1 || nargs > 3)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
					 errmsg("cast function must take one to three arguments")));
		if (!IsBinaryCoercible(sourcetypeid, procstruct->proargtypes.values[0]))
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
					 errmsg("argument of cast function must match or be binary-coercible from source data type")));
		if (nargs > 1 && procstruct->proargtypes.values[1] != INT4OID)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
					 errmsg("second argument of cast function must be type %s",
							"integer")));
		if (nargs > 2 && procstruct->proargtypes.values[2] != BOOLOID)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
					 errmsg("third argument of cast function must be type %s",
							"boolean")));
		if (!IsBinaryCoercible(procstruct->prorettype, targettypeid))
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
					 errmsg("return data type of cast function must match or be binary-coercible to target data type")));

		/*
		 * Restricting the volatility of a cast function may or may not be a
		 * good idea in the abstract, but it definitely breaks many old
		 * user-defined types.  Disable this check --- tgl 2/1/03
		 */
#ifdef NOT_USED
		if (procstruct->provolatile == PROVOLATILE_VOLATILE)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
					 errmsg("cast function must not be volatile")));
#endif
		if (procstruct->prokind != PROKIND_FUNCTION)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
					 errmsg("cast function must be a normal function")));
		if (procstruct->proretset)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
					 errmsg("cast function must not return a set")));

		ReleaseSysCache(tuple);
	}
	else
	{
		funcid = InvalidOid;
		nargs = 0;
	}

	if (castmethod == COERCION_METHOD_BINARY)
	{
		int16		typ1len;
		int16		typ2len;
		bool		typ1byval;
		bool		typ2byval;
		char		typ1align;
		char		typ2align;

		/*
		 * Must be superuser to create binary-compatible casts, since
		 * erroneous casts can easily crash the backend.
		 */
		if (!superuser() && !(IsYbExtensionUser(GetUserId()) && creating_extension))
			ereport(ERROR,
					(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
					 errmsg("must be superuser to create a cast WITHOUT FUNCTION")));

		/*
		 * Also, insist that the types match as to size, alignment, and
		 * pass-by-value attributes; this provides at least a crude check that
		 * they have similar representations.  A pair of types that fail this
		 * test should certainly not be equated.
		 */
		get_typlenbyvalalign(sourcetypeid, &typ1len, &typ1byval, &typ1align);
		get_typlenbyvalalign(targettypeid, &typ2len, &typ2byval, &typ2align);
		if (typ1len != typ2len ||
			typ1byval != typ2byval ||
			typ1align != typ2align)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
					 errmsg("source and target data types are not physically compatible")));

		/*
		 * We know that composite, enum and array types are never binary-
		 * compatible with each other.  They all have OIDs embedded in them.
		 *
		 * Theoretically you could build a user-defined base type that is
		 * binary-compatible with a composite, enum, or array type.  But we
		 * disallow that too, as in practice such a cast is surely a mistake.
		 * You can always work around that by writing a cast function.
		 */
		if (sourcetyptype == TYPTYPE_COMPOSITE ||
			targettyptype == TYPTYPE_COMPOSITE)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
					 errmsg("composite data types are not binary-compatible")));

		if (sourcetyptype == TYPTYPE_ENUM ||
			targettyptype == TYPTYPE_ENUM)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
					 errmsg("enum data types are not binary-compatible")));

		if (OidIsValid(get_element_type(sourcetypeid)) ||
			OidIsValid(get_element_type(targettypeid)))
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
					 errmsg("array data types are not binary-compatible")));

		/*
		 * We also disallow creating binary-compatibility casts involving
		 * domains.  Casting from a domain to its base type is already
		 * allowed, and casting the other way ought to go through domain
		 * coercion to permit constraint checking.  Again, if you're intent on
		 * having your own semantics for that, create a no-op cast function.
		 *
		 * NOTE: if we were to relax this, the above checks for composites
		 * etc. would have to be modified to look through domains to their
		 * base types.
		 */
		if (sourcetyptype == TYPTYPE_DOMAIN ||
			targettyptype == TYPTYPE_DOMAIN)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
					 errmsg("domain data types must not be marked binary-compatible")));
	}

	/*
	 * Allow source and target types to be same only for length coercion
	 * functions.  We assume a multi-arg function does length coercion.
	 */
	if (sourcetypeid == targettypeid && nargs < 2)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
				 errmsg("source data type and target data type are the same")));

	/* convert CoercionContext enum to char value for castcontext */
	switch (stmt->context)
	{
		case COERCION_IMPLICIT:
			castcontext = COERCION_CODE_IMPLICIT;
			break;
		case COERCION_ASSIGNMENT:
			castcontext = COERCION_CODE_ASSIGNMENT;
			break;
			/* COERCION_PLPGSQL is intentionally not covered here */
		case COERCION_EXPLICIT:
			castcontext = COERCION_CODE_EXPLICIT;
			break;
		default:
			elog(ERROR, "unrecognized CoercionContext: %d", stmt->context);
			castcontext = 0;	/* keep compiler quiet */
			break;
	}

	myself = CastCreate(sourcetypeid, targettypeid, funcid, castcontext,
						castmethod, DEPENDENCY_NORMAL);
	return myself;
}

#ifdef NEIL
Oid
get_cast_oid(Oid sourcetypeid, Oid targettypeid, bool missing_ok)

void
DropCastById(Oid castOid)
#endif

static void
check_transform_function(Form_pg_proc procstruct)
{
	if (procstruct->provolatile == PROVOLATILE_VOLATILE)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
				 errmsg("transform function must not be volatile")));
	if (procstruct->prokind != PROKIND_FUNCTION)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
				 errmsg("transform function must be a normal function")));
	if (procstruct->proretset)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
				 errmsg("transform function must not return a set")));
	if (procstruct->pronargs != 1)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
				 errmsg("transform function must take one argument")));
	if (procstruct->proargtypes.values[0] != INTERNALOID)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
				 errmsg("first argument of transform function must be type %s",
						"internal")));
}


/*
 * CREATE TRANSFORM
 */
ObjectAddress
CreateTransform(CreateTransformStmt *stmt)
{
	Oid			typeid;
	char		typtype;
	Oid			langid;
	Oid			fromsqlfuncid;
	Oid			tosqlfuncid;
	AclResult	aclresult;
	Form_pg_proc procstruct;
	Datum		values[Natts_pg_transform];
	bool		nulls[Natts_pg_transform];
	bool		replaces[Natts_pg_transform];
	Oid			transformid;
	HeapTuple	tuple;
	HeapTuple	newtuple;
	Relation	relation;
	ObjectAddress myself,
				referenced;
	ObjectAddresses *addrs;
	bool		is_replace;

	/*
	 * Get the type
	 */
	typeid = typenameTypeId(NULL, stmt->type_name);
	typtype = get_typtype(typeid);

	if (typtype == TYPTYPE_PSEUDO)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("data type %s is a pseudo-type",
						TypeNameToString(stmt->type_name))));

	if (typtype == TYPTYPE_DOMAIN)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("data type %s is a domain",
						TypeNameToString(stmt->type_name))));

	if (!pg_type_ownercheck(typeid, GetUserId()))
		aclcheck_error_type(ACLCHECK_NOT_OWNER, typeid);

	aclresult = pg_type_aclcheck(typeid, GetUserId(), ACL_USAGE);
	if (aclresult != ACLCHECK_OK)
		aclcheck_error_type(aclresult, typeid);

	/*
	 * Get the language
	 */
	langid = get_language_oid(stmt->lang, false);

	aclresult = pg_language_aclcheck(langid, GetUserId(), ACL_USAGE);
	if (aclresult != ACLCHECK_OK)
		aclcheck_error(aclresult, OBJECT_LANGUAGE, stmt->lang);

	/*
	 * Get the functions
	 */
	if (stmt->fromsql)
	{
		fromsqlfuncid = LookupFuncWithArgs(OBJECT_FUNCTION, stmt->fromsql, false);

		if (!pg_proc_ownercheck(fromsqlfuncid, GetUserId()))
			aclcheck_error(ACLCHECK_NOT_OWNER, OBJECT_FUNCTION, NameListToString(stmt->fromsql->objname));

		aclresult = pg_proc_aclcheck(fromsqlfuncid, GetUserId(), ACL_EXECUTE);
		if (aclresult != ACLCHECK_OK)
			aclcheck_error(aclresult, OBJECT_FUNCTION, NameListToString(stmt->fromsql->objname));

		tuple = SearchSysCache1(PROCOID, ObjectIdGetDatum(fromsqlfuncid));
		if (!HeapTupleIsValid(tuple))
			elog(ERROR, "cache lookup failed for function %u", fromsqlfuncid);
		procstruct = (Form_pg_proc) GETSTRUCT(tuple);
		if (procstruct->prorettype != INTERNALOID)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
					 errmsg("return data type of FROM SQL function must be %s",
							"internal")));
		check_transform_function(procstruct);
		ReleaseSysCache(tuple);
	}
	else
		fromsqlfuncid = InvalidOid;

	if (stmt->tosql)
	{
		tosqlfuncid = LookupFuncWithArgs(OBJECT_FUNCTION, stmt->tosql, false);

		if (!pg_proc_ownercheck(tosqlfuncid, GetUserId()))
			aclcheck_error(ACLCHECK_NOT_OWNER, OBJECT_FUNCTION, NameListToString(stmt->tosql->objname));

		aclresult = pg_proc_aclcheck(tosqlfuncid, GetUserId(), ACL_EXECUTE);
		if (aclresult != ACLCHECK_OK)
			aclcheck_error(aclresult, OBJECT_FUNCTION, NameListToString(stmt->tosql->objname));

		tuple = SearchSysCache1(PROCOID, ObjectIdGetDatum(tosqlfuncid));
		if (!HeapTupleIsValid(tuple))
			elog(ERROR, "cache lookup failed for function %u", tosqlfuncid);
		procstruct = (Form_pg_proc) GETSTRUCT(tuple);
		if (procstruct->prorettype != typeid)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
					 errmsg("return data type of TO SQL function must be the transform data type")));
		check_transform_function(procstruct);
		ReleaseSysCache(tuple);
	}
	else
		tosqlfuncid = InvalidOid;

	/*
	 * Ready to go
	 */
	values[Anum_pg_transform_trftype - 1] = ObjectIdGetDatum(typeid);
	values[Anum_pg_transform_trflang - 1] = ObjectIdGetDatum(langid);
	values[Anum_pg_transform_trffromsql - 1] = ObjectIdGetDatum(fromsqlfuncid);
	values[Anum_pg_transform_trftosql - 1] = ObjectIdGetDatum(tosqlfuncid);

	MemSet(nulls, false, sizeof(nulls));

	relation = table_open(TransformRelationId, RowExclusiveLock);

	tuple = SearchSysCache2(TRFTYPELANG,
							ObjectIdGetDatum(typeid),
							ObjectIdGetDatum(langid));
	if (HeapTupleIsValid(tuple))
	{
		Form_pg_transform form = (Form_pg_transform) GETSTRUCT(tuple);

		if (!stmt->replace)
			ereport(ERROR,
					(errcode(ERRCODE_DUPLICATE_OBJECT),
					 errmsg("transform for type %s language \"%s\" already exists",
							format_type_be(typeid),
							stmt->lang)));

		MemSet(replaces, false, sizeof(replaces));
		replaces[Anum_pg_transform_trffromsql - 1] = true;
		replaces[Anum_pg_transform_trftosql - 1] = true;

		newtuple = heap_modify_tuple(tuple, RelationGetDescr(relation), values, nulls, replaces);
		CatalogTupleUpdate(relation, &newtuple->t_self, newtuple);

		transformid = form->oid;
		ReleaseSysCache(tuple);
		is_replace = true;
	}
	else
	{
		transformid = GetNewOidWithIndex(relation, TransformOidIndexId,
										 Anum_pg_transform_oid);
		values[Anum_pg_transform_oid - 1] = ObjectIdGetDatum(transformid);
		newtuple = heap_form_tuple(RelationGetDescr(relation), values, nulls);
		CatalogTupleInsert(relation, newtuple);
		is_replace = false;
	}

	if (is_replace)
		deleteDependencyRecordsFor(TransformRelationId, transformid, true);

	addrs = new_object_addresses();

	/* make dependency entries */
	ObjectAddressSet(myself, TransformRelationId, transformid);

	/* dependency on language */
	ObjectAddressSet(referenced, LanguageRelationId, langid);
	add_exact_object_address(&referenced, addrs);

	/* dependency on type */
	ObjectAddressSet(referenced, TypeRelationId, typeid);
	add_exact_object_address(&referenced, addrs);

	/* dependencies on functions */
	if (OidIsValid(fromsqlfuncid))
	{
		ObjectAddressSet(referenced, ProcedureRelationId, fromsqlfuncid);
		add_exact_object_address(&referenced, addrs);
	}
	if (OidIsValid(tosqlfuncid))
	{
		ObjectAddressSet(referenced, ProcedureRelationId, tosqlfuncid);
		add_exact_object_address(&referenced, addrs);
	}

	record_object_address_dependencies(&myself, addrs, DEPENDENCY_NORMAL);
	free_object_addresses(addrs);

	/* dependency on extension */
	recordDependencyOnCurrentExtension(&myself, is_replace);

	/* Post creation hook for new transform */
	InvokeObjectPostCreateHook(TransformRelationId, transformid, 0);

	heap_freetuple(newtuple);

	table_close(relation, RowExclusiveLock);

	return myself;
}


/*
 * get_transform_oid - given type OID and language OID, look up a transform OID
 *
 * If missing_ok is false, throw an error if the transform is not found.  If
 * true, just return InvalidOid.
 */
Oid
get_transform_oid(Oid type_id, Oid lang_id, bool missing_ok)
{
	Oid			oid;

	oid = GetSysCacheOid2(TRFTYPELANG, Anum_pg_transform_oid,
						  ObjectIdGetDatum(type_id),
						  ObjectIdGetDatum(lang_id));
	if (!OidIsValid(oid) && !missing_ok)
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				 errmsg("transform for type %s language \"%s\" does not exist",
						format_type_be(type_id),
						get_language_name(lang_id, false))));
	return oid;
}


#ifdef NEIL
void
DropTransformById(Oid transformOid)
#endif


/*
 * Subroutine for ALTER FUNCTION/AGGREGATE SET SCHEMA/RENAME
 *
 * Is there a function with the given name and signature already in the given
 * namespace?  If so, raise an appropriate error message.
 */
void
IsThereFunctionInNamespace(const char *proname, int pronargs,
						   oidvector *proargtypes, Oid nspOid)
{
	/* check for duplicate name (more friendly than unique-index failure) */
	if (SearchSysCacheExists3(PROCNAMEARGSNSP,
							  CStringGetDatum(proname),
							  PointerGetDatum(proargtypes),
							  ObjectIdGetDatum(nspOid)))
		ereport(ERROR,
				(errcode(ERRCODE_DUPLICATE_FUNCTION),
				 errmsg("function %s already exists in schema \"%s\"",
						funcname_signature_string(proname, pronargs,
												  NIL, proargtypes->values),
						get_namespace_name(nspOid))));
}

/*
 * ExecuteDoStmt
 *		Execute inline procedural-language code
 *
 * See at ExecuteCallStmt() about the atomic argument.
 */
void
ExecuteDoStmt(ParseState *pstate, DoStmt *stmt, bool atomic)
{
	InlineCodeBlock *codeblock = makeNode(InlineCodeBlock);
	ListCell   *arg;
	DefElem    *as_item = NULL;
	DefElem    *language_item = NULL;
	char	   *language;
	Oid			laninline;
	HeapTuple	languageTuple;
	Form_pg_language languageStruct;

	/* Process options we got from gram.y */
	foreach(arg, stmt->args)
	{
		DefElem    *defel = (DefElem *) lfirst(arg);

		if (strcmp(defel->defname, "as") == 0)
		{
			if (as_item)
				errorConflictingDefElem(defel, pstate);
			as_item = defel;
		}
		else if (strcmp(defel->defname, "language") == 0)
		{
			if (language_item)
				errorConflictingDefElem(defel, pstate);
			language_item = defel;
		}
		else
			elog(ERROR, "option \"%s\" not recognized",
				 defel->defname);
	}

	if (as_item)
		codeblock->source_text = strVal(as_item->arg);
	else
		ereport(ERROR,
				(errcode(ERRCODE_SYNTAX_ERROR),
				 errmsg("no inline code specified")));

	/* if LANGUAGE option wasn't specified, use the default */
	if (language_item)
		language = strVal(language_item->arg);
	else
		language = "plpgsql";

	/* Look up the language and validate permissions */
	languageTuple = SearchSysCache1(LANGNAME, PointerGetDatum(language));
	if (!HeapTupleIsValid(languageTuple))
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				 errmsg("language \"%s\" does not exist", language),
				 (extension_file_exists(language) ?
				  errhint("Use CREATE EXTENSION to load the language into the database.") : 0)));

	languageStruct = (Form_pg_language) GETSTRUCT(languageTuple);
	codeblock->langOid = languageStruct->oid;
	codeblock->langIsTrusted = languageStruct->lanpltrusted;
	codeblock->atomic = atomic;

	if (languageStruct->lanpltrusted)
	{
		/* if trusted language, need USAGE privilege */
		AclResult	aclresult;

		aclresult = pg_language_aclcheck(codeblock->langOid, GetUserId(),
										 ACL_USAGE);
		if (aclresult != ACLCHECK_OK)
			aclcheck_error(aclresult, OBJECT_LANGUAGE,
						   NameStr(languageStruct->lanname));
	}
	else
	{
		/* if untrusted language, must be superuser */
		if (!superuser())
			aclcheck_error(ACLCHECK_NO_PRIV, OBJECT_LANGUAGE,
						   NameStr(languageStruct->lanname));
	}

	/* get the handler function's OID */
	laninline = languageStruct->laninline;
	if (!OidIsValid(laninline))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("language \"%s\" does not support inline code execution",
						NameStr(languageStruct->lanname))));

	ReleaseSysCache(languageTuple);

	/* execute the inline handler */
	OidFunctionCall1(laninline, PointerGetDatum(codeblock));
}

/*
 * Execute CALL statement
 *
 * Inside a top-level CALL statement, transaction-terminating commands such as
 * COMMIT or a PL-specific equivalent are allowed.  The terminology in the SQL
 * standard is that CALL establishes a non-atomic execution context.  Most
 * other commands establish an atomic execution context, in which transaction
 * control actions are not allowed.  If there are nested executions of CALL,
 * we want to track the execution context recursively, so that the nested
 * CALLs can also do transaction control.  Note, however, that for example in
 * CALL -> SELECT -> CALL, the second call cannot do transaction control,
 * because the SELECT in between establishes an atomic execution context.
 *
 * So when ExecuteCallStmt() is called from the top level, we pass in atomic =
 * false (recall that that means transactions = yes).  We then create a
 * CallContext node with content atomic = false, which is passed in the
 * fcinfo->context field to the procedure invocation.  The language
 * implementation should then take appropriate measures to allow or prevent
 * transaction commands based on that information, e.g., call
 * SPI_connect_ext(SPI_OPT_NONATOMIC).  The language should also pass on the
 * atomic flag to any nested invocations to CALL.
 *
 * The expression data structures and execution context that we create
 * within this function are children of the portalContext of the Portal
 * that the CALL utility statement runs in.  Therefore, any pass-by-ref
 * values that we're passing to the procedure will survive transaction
 * commits that might occur inside the procedure.
 */
void
ExecuteCallStmt(CallStmt *stmt, ParamListInfo params, bool atomic, DestReceiver *dest)
{
	LOCAL_FCINFO(fcinfo, FUNC_MAX_ARGS);
	ListCell   *lc;
	FuncExpr   *fexpr;
	int			nargs;
	int			i;
	AclResult	aclresult;
	FmgrInfo	flinfo;
	CallContext *callcontext;
	EState	   *estate;
	ExprContext *econtext;
	HeapTuple	tp;
	PgStat_FunctionCallUsage fcusage;
	Datum		retval;

	fexpr = stmt->funcexpr;
	Assert(fexpr);
	Assert(IsA(fexpr, FuncExpr));

	aclresult = pg_proc_aclcheck(fexpr->funcid, GetUserId(), ACL_EXECUTE);
	if (aclresult != ACLCHECK_OK)
		aclcheck_error(aclresult, OBJECT_PROCEDURE, get_func_name(fexpr->funcid));

	/* Prep the context object we'll pass to the procedure */
	callcontext = makeNode(CallContext);
	callcontext->atomic = atomic;

	tp = SearchSysCache1(PROCOID, ObjectIdGetDatum(fexpr->funcid));
	if (!HeapTupleIsValid(tp))
		elog(ERROR, "cache lookup failed for function %u", fexpr->funcid);

	/*
	 * If proconfig is set we can't allow transaction commands because of the
	 * way the GUC stacking works: The transaction boundary would have to pop
	 * the proconfig setting off the stack.  That restriction could be lifted
	 * by redesigning the GUC nesting mechanism a bit.
	 */
	if (!heap_attisnull(tp, Anum_pg_proc_proconfig, NULL))
		callcontext->atomic = true;

	/*
	 * In security definer procedures, we can't allow transaction commands.
	 * StartTransaction() insists that the security context stack is empty,
	 * and AbortTransaction() resets the security context.  This could be
	 * reorganized, but right now it doesn't work.
	 */
	if (((Form_pg_proc) GETSTRUCT(tp))->prosecdef)
		callcontext->atomic = true;

	ReleaseSysCache(tp);

	/* safety check; see ExecInitFunc() */
	nargs = list_length(fexpr->args);
	if (nargs > FUNC_MAX_ARGS)
		ereport(ERROR,
				(errcode(ERRCODE_TOO_MANY_ARGUMENTS),
				 errmsg_plural("cannot pass more than %d argument to a procedure",
							   "cannot pass more than %d arguments to a procedure",
							   FUNC_MAX_ARGS,
							   FUNC_MAX_ARGS)));

	/* Initialize function call structure */
	InvokeFunctionExecuteHook(fexpr->funcid);
	fmgr_info(fexpr->funcid, &flinfo);
	fmgr_info_set_expr((Node *) fexpr, &flinfo);
	InitFunctionCallInfoData(*fcinfo, &flinfo, nargs, fexpr->inputcollid,
							 (Node *) callcontext, NULL);

	/*
	 * Evaluate procedure arguments inside a suitable execution context.  Note
	 * we can't free this context till the procedure returns.
	 */
	estate = CreateExecutorState();
	estate->es_param_list_info = params;
	econtext = CreateExprContext(estate);

	/*
	 * If we're called in non-atomic context, we also have to ensure that the
	 * argument expressions run with an up-to-date snapshot.  Our caller will
	 * have provided a current snapshot in atomic contexts, but not in
	 * non-atomic contexts, because the possibility of a COMMIT/ROLLBACK
	 * destroying the snapshot makes higher-level management too complicated.
	 */
	if (!atomic)
		PushActiveSnapshot(GetTransactionSnapshot());

	i = 0;
	foreach(lc, fexpr->args)
	{
		ExprState  *exprstate;
		Datum		val;
		bool		isnull;

		exprstate = ExecPrepareExpr(lfirst(lc), estate);

		val = ExecEvalExprSwitchContext(exprstate, econtext, &isnull);

		fcinfo->args[i].value = val;
		fcinfo->args[i].isnull = isnull;

		i++;
	}

	/* Get rid of temporary snapshot for arguments, if we made one */
	if (!atomic)
		PopActiveSnapshot();

	/* Here we actually call the procedure */
	pgstat_init_function_usage(fcinfo, &fcusage);
	retval = FunctionCallInvoke(fcinfo);
	pgstat_end_function_usage(&fcusage, true);

	/* Handle the procedure's outputs */
	if (fexpr->funcresulttype == VOIDOID)
	{
		/* do nothing */
	}
	else if (fexpr->funcresulttype == RECORDOID)
	{
		/* send tuple to client */
		HeapTupleHeader td;
		Oid			tupType;
		int32		tupTypmod;
		TupleDesc	retdesc;
		HeapTupleData rettupdata;
		TupOutputState *tstate;
		TupleTableSlot *slot;

		if (fcinfo->isnull)
			elog(ERROR, "procedure returned null record");

		/*
		 * Ensure there's an active snapshot whilst we execute whatever's
		 * involved here.  Note that this is *not* sufficient to make the
		 * world safe for TOAST pointers to be included in the returned data:
		 * the referenced data could have gone away while we didn't hold a
		 * snapshot.  Hence, it's incumbent on PLs that can do COMMIT/ROLLBACK
		 * to not return TOAST pointers, unless those pointers were fetched
		 * after the last COMMIT/ROLLBACK in the procedure.
		 *
		 * XXX that is a really nasty, hard-to-test requirement.  Is there a
		 * way to remove it?
		 */
		EnsurePortalSnapshotExists();

		td = DatumGetHeapTupleHeader(retval);
		tupType = HeapTupleHeaderGetTypeId(td);
		tupTypmod = HeapTupleHeaderGetTypMod(td);
		retdesc = lookup_rowtype_tupdesc(tupType, tupTypmod);

		tstate = begin_tup_output_tupdesc(dest, retdesc,
										  &TTSOpsHeapTuple);

		rettupdata.t_len = HeapTupleHeaderGetDatumLength(td);
		ItemPointerSetInvalid(&(rettupdata.t_self));
		HEAPTUPLE_YBCTID(&rettupdata) = 0;
		rettupdata.t_tableOid = InvalidOid;
		rettupdata.t_data = td;

		slot = ExecStoreHeapTuple(&rettupdata, tstate->slot, false);
		tstate->dest->receiveSlot(slot, tstate->dest);

		end_tup_output(tstate);

		ReleaseTupleDesc(retdesc);
	}
	else
		elog(ERROR, "unexpected result type for procedure: %u",
			 fexpr->funcresulttype);

	FreeExecutorState(estate);
}

/*
 * Construct the tuple descriptor for a CALL statement return
 */
TupleDesc
CallStmtResultDesc(CallStmt *stmt)
{
	FuncExpr   *fexpr;
	HeapTuple	tuple;
	TupleDesc	tupdesc;

	fexpr = stmt->funcexpr;

	tuple = SearchSysCache1(PROCOID, ObjectIdGetDatum(fexpr->funcid));
	if (!HeapTupleIsValid(tuple))
		elog(ERROR, "cache lookup failed for procedure %u", fexpr->funcid);

	tupdesc = build_function_result_tupdesc_t(tuple);

	ReleaseSysCache(tuple);

	return tupdesc;
}

/*
 * Change function owner
 */
ObjectAddress
AlterFunctionOwner(AlterOwnerStmt *stmt, Oid newOwnerId)
{
	Relation	relation;
	ObjectAddress address;
	HeapTuple	tup;

	/* Find the function's OID. */
	address = get_object_address(stmt->objectType,
								 stmt->object,
								 &relation,
								 AccessExclusiveLock,
								 false);
	relation = table_open(ProcedureRelationId, RowExclusiveLock);

	tup = SearchSysCacheCopy1(PROCOID, ObjectIdGetDatum(address.objectId));

	if (!HeapTupleIsValid(tup))
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				 errmsg("function with OID %u does not exist", address.objectId)));

	AlterFunctionOwner_internal(relation, tup, newOwnerId);

	/* YB_TEST(neil) address should already have procid (address.objectid == tup->oid?)
	 *   ObjectAddressSet(address, ProcedureRelationId, procId);
	 */

	heap_freetuple(tup);

	table_close(relation, RowExclusiveLock);

	return address;
}

/*
 * Internal workhorse for changing a function's owner
 */
void
AlterFunctionOwner_internal(Relation rel, HeapTuple tup, Oid newOwnerId)
{
	Oid procId;
	Form_pg_proc proc;
	Oid namespaceId;

	proc = (Form_pg_proc) GETSTRUCT(tup);
	procId = proc->oid;

	/* Assigning a function to the same owner is a no-op */
	if (proc->proowner == newOwnerId)
		return;

	/* Superusers and yb_db_admin role can bypass permission checks */
	if (!superuser() && !IsYbDbAdminUser(GetUserId()))
	{
		/* Must be owner */
		if (!has_privs_of_role(GetUserId(), proc->proowner))
		{
			aclcheck_error(ACLCHECK_NOT_OWNER, OBJECT_FUNCTION, NameStr(proc->proname));
		}

		/* Must be able to become new owner */
		check_is_member_of_role(GetUserId(), newOwnerId);

		/* New owner must have CREATE privilege on function's schema */
		namespaceId = proc->pronamespace;
		if (OidIsValid(namespaceId))
		{
			AclResult	aclresult;

			aclresult = pg_namespace_aclcheck(namespaceId, newOwnerId,
												  ACL_CREATE);
			if (aclresult != ACLCHECK_OK)
				aclcheck_error(aclresult, OBJECT_SCHEMA,
							   get_namespace_name(namespaceId));
		}
	}

	proc->proowner = newOwnerId;
	CatalogTupleUpdate(rel, &tup->t_self, tup);

	/* Update owner dependency reference */
	changeDependencyOnOwner(ProcedureRelationId,
							procId,
							newOwnerId);

	InvokeObjectPostAlterHook(ProcedureRelationId,
							  procId,
							  0);
}

/*
 * Change function name
 */
ObjectAddress
RenameFunction(RenameStmt *stmt, const char *newname)
{
	Relation	relation;
	ObjectAddress address;
	HeapTuple	tup;

	/* Find the function's OID */
	address = get_object_address(stmt->renameType,
								 stmt->object,
								 &relation,
								 AccessExclusiveLock,
								 false);
	relation = table_open(ProcedureRelationId, RowExclusiveLock);

	tup = SearchSysCacheCopy1(PROCOID, ObjectIdGetDatum(address.objectId));

	if (!HeapTupleIsValid(tup))
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				 errmsg("function with OID %u does not exist", address.objectId)));

	Form_pg_proc proc;

	proc = (Form_pg_proc) GETSTRUCT(tup);

	/* Superusers and yb_db_admin role can bypass permission checks */
	if (!superuser() && !IsYbDbAdminUser(GetUserId()))
	{
		/* Must be owner of the existing object */
		if (!has_privs_of_role(GetUserId(),proc->proowner))
			aclcheck_error(ACLCHECK_NOT_OWNER, OBJECT_FUNCTION,  NameStr(proc->proname));
	}

	/* Make sure function with new name doesn't exist */
	IsThereFunctionInNamespace(newname, proc->pronargs,
							   &proc->proargtypes, proc->pronamespace);

	/* Rename */
	namestrcpy(&(((Form_pg_proc) GETSTRUCT(tup))->proname), newname);
	CatalogTupleUpdate(relation, &tup->t_self, tup);

	InvokeObjectPostAlterHook(ProcedureRelationId,
							  proc->oid,
							  0);

	heap_freetuple(tup);

	table_close(relation, RowExclusiveLock);
	return address;
}
