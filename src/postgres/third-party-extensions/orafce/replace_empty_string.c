#include "postgres.h"

#include "access/htup_details.h"
#include "catalog/pg_type.h"
#include "commands/trigger.h"
#include "executor/spi.h"
#include "miscadmin.h"
#include "parser/parse_coerce.h"
#include "utils/builtins.h"
#include "utils/lsyscache.h"
#include "utils/rel.h"

#include "orafce.h"
#include "builtins.h"

PG_FUNCTION_INFO_V1(orafce_replace_empty_strings);
PG_FUNCTION_INFO_V1(orafce_replace_null_strings);

#if PG_VERSION_NUM < 100000

static HeapTuple
heap_modify_tuple_by_cols(HeapTuple tuple,
						  TupleDesc tupleDesc,
						  int nCols,
						  int *replCols,
						  Datum *replValues,
						  bool *replIsnull)
{
	int			numberOfAttributes = tupleDesc->natts;
	Datum	   *values;
	bool	   *isnull;
	HeapTuple	newTuple;
	int			i;

	/*
	 * allocate and fill values and isnull arrays from the tuple, then replace
	 * selected columns from the input arrays.
	 */
	values = (Datum *) palloc(numberOfAttributes * sizeof(Datum));
	isnull = (bool *) palloc(numberOfAttributes * sizeof(bool));

	heap_deform_tuple(tuple, tupleDesc, values, isnull);

	for (i = 0; i < nCols; i++)
	{
		int			attnum = replCols[i];

		if (attnum <= 0 || attnum > numberOfAttributes)
			elog(ERROR, "invalid column number %d", attnum);
		values[attnum - 1] = replValues[i];
		isnull[attnum - 1] = replIsnull[i];
	}

	/*
	 * create a new tuple from the values and isnull arrays
	 */
	newTuple = heap_form_tuple(tupleDesc, values, isnull);

	pfree(values);
	pfree(isnull);

	/*
	 * copy the identification info of the old tuple: t_ctid, t_self, and OID
	 * (if any)
	 */
	newTuple->t_data->t_ctid = tuple->t_data->t_ctid;
	newTuple->t_self = tuple->t_self;
	HEAPTUPLE_COPY_YBITEM(tuple, newTuple);
	newTuple->t_tableOid = tuple->t_tableOid;
#ifdef YB_TODO
	/* OID is now a regular column */
	if (tupleDesc->tdhasoid)
		HeapTupleSetOid(newTuple, HeapTupleGetOid(tuple));
#endif

	return newTuple;
}

#endif

static void
trigger_sanity_check(FunctionCallInfo fcinfo, const char *fname)
{
	TriggerData	   *trigdata = (TriggerData *) fcinfo->context;

	/* sanity checks from autoinc.c */
	if (!CALLED_AS_TRIGGER(fcinfo))
		elog(ERROR, "%s: not fired by trigger manager", fname);

	if (!TRIGGER_FIRED_FOR_ROW(trigdata->tg_event))
		elog(ERROR, "%s: must be fired for row", fname);

	if (!TRIGGER_FIRED_BEFORE(trigdata->tg_event))
		elog(ERROR, "%s: must be fired before event", fname);

	if (trigdata->tg_trigger->tgnargs > 1)
		elog(ERROR, "%s: only one trigger parameter is allowed", fname);
}

static HeapTuple
get_rettuple(FunctionCallInfo fcinfo)
{
	TriggerData	   *trigdata = (TriggerData *) fcinfo->context;
	HeapTuple		rettuple = NULL;

	if (TRIGGER_FIRED_BY_INSERT(trigdata->tg_event))
		rettuple = trigdata->tg_trigtuple;
	else if (TRIGGER_FIRED_BY_UPDATE(trigdata->tg_event))
		rettuple = trigdata->tg_newtuple;
	else
		/* internal error */
		elog(ERROR, "remove_empty_string: cannot process DELETE events");

	return rettuple;
}

/*
 * Trigger argument is used as parameter that can enforce warning about modified
 * columns. When first argument is "on" or "true", then warnings will be raised.
 */
static bool
should_raise_warnings(FunctionCallInfo fcinfo)
{
	TriggerData	   *trigdata = (TriggerData *) fcinfo->context;
	Trigger		   *trigger = trigdata->tg_trigger;

	if (trigger->tgnargs > 0)
	{
		char		  **args = trigger->tgargs;

		if (strcmp(args[0], "on") == 0 ||
				strcmp(args[0], "true") == 0)
			return true;
	}

	return false;
}

/*
 * Detects emty strings in type text based fields and replaces them by NULL.
 */
Datum
orafce_replace_empty_strings(PG_FUNCTION_ARGS)
{
	TriggerData	   *trigdata = (TriggerData *) fcinfo->context;
	HeapTuple		rettuple = NULL;
	TupleDesc		tupdesc;
	int			   *resetcols = NULL;
	Datum		   *values = NULL;
	bool		   *nulls = NULL;
	Oid				prev_typid = InvalidOid;
	bool			is_string = false;
	int				nresetcols = 0;
	int				attnum;
	bool			raise_warning = false;
	char		   *relname = NULL;

	trigger_sanity_check(fcinfo, "replace_empty_strings");
	raise_warning = should_raise_warnings(fcinfo);

	rettuple = get_rettuple(fcinfo);
	tupdesc = trigdata->tg_relation->rd_att;

	/* iterate over record's fields */
	for (attnum = 1; attnum <= tupdesc->natts; attnum++)
	{
		Oid typid;

		/* simple cache - lot of time columns with same type is side by side */
		typid = SPI_gettypeid(tupdesc, attnum);
		if (typid != prev_typid)
		{
			TYPCATEGORY category;
			bool		ispreferred;
			Oid base_typid;

			base_typid = getBaseType(typid);
			get_type_category_preferred(base_typid, &category, &ispreferred);

			is_string = (category == TYPCATEGORY_STRING);
			prev_typid = typid;
		}

		if (is_string)
		{
			Datum		value;
			bool		isnull;

			value = SPI_getbinval(rettuple, tupdesc, attnum, &isnull);
			if (!isnull)
			{
				text *txt = DatumGetTextP(value);

				/* is it empty string (has zero length */
				if (VARSIZE_ANY_EXHDR(txt) == 0)
				{
					if (!resetcols)
					{
						/* lazy allocation of dynamic memory */
						resetcols = palloc0(tupdesc->natts * sizeof(int));
						nulls = palloc0(tupdesc->natts * sizeof(bool));
						values = palloc0(tupdesc->natts * sizeof(Datum));
					}

					resetcols[nresetcols] = attnum;
					values[nresetcols] = (Datum) 0;
					nulls[nresetcols++] = true;

					if (raise_warning)
					{
						if (!relname)
							relname = SPI_getrelname(trigdata->tg_relation);

						elog(WARNING,
				"Field \"%s\" of table \"%s\" is empty string (replaced by NULL).",
								SPI_fname(tupdesc, attnum), relname);
					}
				}
			}
		}
	}

	if (nresetcols > 0)
	{
		/* construct new tuple */
		rettuple = heap_modify_tuple_by_cols(rettuple, tupdesc,
											 nresetcols, resetcols,
											 values, nulls);
	}

	if (relname)
		pfree(relname);
	if (resetcols)
		pfree(resetcols);
	if (values)
		pfree(values);
	if (nulls)
		pfree(nulls);

	return PointerGetDatum(rettuple);
}

/*
 * Detects NULL in type text based fields and replaces them by empty string
 */
Datum
orafce_replace_null_strings(PG_FUNCTION_ARGS)
{
	TriggerData	   *trigdata = (TriggerData *) fcinfo->context;
	HeapTuple		rettuple = NULL;
	TupleDesc		tupdesc;
	int			   *resetcols = NULL;
	Datum		   *values = NULL;
	bool		   *nulls = NULL;
	Oid				prev_typid = InvalidOid;
	bool			is_string = false;
	int				nresetcols = 0;
	int				attnum;
	bool			raise_warning = false;
	char		   *relname = NULL;

	trigger_sanity_check(fcinfo, "replace_null_strings");
	raise_warning = should_raise_warnings(fcinfo);

	rettuple = get_rettuple(fcinfo);

	/* return fast when there are not any NULL */
	if (!HeapTupleHasNulls(rettuple))
		return PointerGetDatum(rettuple);

	tupdesc = trigdata->tg_relation->rd_att;

	/* iterate over record's fields */
	for (attnum = 1; attnum <= tupdesc->natts; attnum++)
	{
		Oid typid;

		/* simple cache - lot of time columns with same type is side by side */
		typid = SPI_gettypeid(tupdesc, attnum);
		if (typid != prev_typid)
		{
			TYPCATEGORY category;
			bool		ispreferred;
			Oid base_typid;

			base_typid = getBaseType(typid);
			get_type_category_preferred(base_typid, &category, &ispreferred);

			is_string = (category == TYPCATEGORY_STRING);
			prev_typid = typid;
		}

		if (is_string)
		{
			bool		isnull;

			(void) SPI_getbinval(rettuple, tupdesc, attnum, &isnull);
			if (isnull)
			{
				if (!resetcols)
				{
					/* lazy allocation of dynamic memory */
					resetcols = palloc0(tupdesc->natts * sizeof(int));
					nulls = palloc0(tupdesc->natts * sizeof(bool));
					values = palloc0(tupdesc->natts * sizeof(Datum));
				}

				resetcols[nresetcols] = attnum;
				values[nresetcols] = PointerGetDatum(cstring_to_text_with_len("", 0));
				nulls[nresetcols++] = false;

				if (raise_warning)
				{
					if (!relname)
						relname = SPI_getrelname(trigdata->tg_relation);

					elog(WARNING,
				"Field \"%s\" of table \"%s\" is NULL (replaced by '').",
								SPI_fname(tupdesc, attnum), relname);
				}
			}
		}
	}

	if (nresetcols > 0)
	{
		/* construct new tuple */
		rettuple = heap_modify_tuple_by_cols(rettuple, tupdesc,
											 nresetcols, resetcols,
											 values, nulls);
	}

	if (relname)
		pfree(relname);
	if (resetcols)
		pfree(resetcols);
	if (values)
		pfree(values);
	if (nulls)
		pfree(nulls);

	return PointerGetDatum(rettuple);
}
