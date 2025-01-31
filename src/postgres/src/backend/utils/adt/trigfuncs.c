/*-------------------------------------------------------------------------
 *
 * trigfuncs.c
 *	  Builtin functions for useful trigger support.
 *
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/backend/utils/adt/trigfuncs.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/htup_details.h"
#include "commands/trigger.h"
#include "utils/builtins.h"
#include "utils/rel.h"


/*
 * suppress_redundant_updates_trigger
 *
 * This trigger function will inhibit an update from being done
 * if the OLD and NEW records are identical.
 */
Datum
suppress_redundant_updates_trigger(PG_FUNCTION_ARGS)
{
	TriggerData *trigdata = (TriggerData *) fcinfo->context;
	HeapTuple	newtuple,
				oldtuple,
				rettuple;
	HeapTupleHeader newheader,
				oldheader;

	/* make sure it's called as a trigger */
	if (!CALLED_AS_TRIGGER(fcinfo))
		ereport(ERROR,
				(errcode(ERRCODE_E_R_I_E_TRIGGER_PROTOCOL_VIOLATED),
				 errmsg("suppress_redundant_updates_trigger: must be called as trigger")));

	/* and that it's called on update */
	if (!TRIGGER_FIRED_BY_UPDATE(trigdata->tg_event))
		ereport(ERROR,
				(errcode(ERRCODE_E_R_I_E_TRIGGER_PROTOCOL_VIOLATED),
				 errmsg("suppress_redundant_updates_trigger: must be called on update")));

	/* and that it's called before update */
	if (!TRIGGER_FIRED_BEFORE(trigdata->tg_event))
		ereport(ERROR,
				(errcode(ERRCODE_E_R_I_E_TRIGGER_PROTOCOL_VIOLATED),
				 errmsg("suppress_redundant_updates_trigger: must be called before update")));

	/* and that it's called for each row */
	if (!TRIGGER_FIRED_FOR_ROW(trigdata->tg_event))
		ereport(ERROR,
				(errcode(ERRCODE_E_R_I_E_TRIGGER_PROTOCOL_VIOLATED),
				 errmsg("suppress_redundant_updates_trigger: must be called for each row")));

	/* get tuple data, set default result */
	rettuple = newtuple = trigdata->tg_newtuple;
	oldtuple = trigdata->tg_trigtuple;

	newheader = newtuple->t_data;
	oldheader = oldtuple->t_data;

	/* if the tuple payload is the same ... */
	if (newtuple->t_len == oldtuple->t_len &&
		newheader->t_hoff == oldheader->t_hoff &&
		(HeapTupleHeaderGetNatts(newheader) ==
		 HeapTupleHeaderGetNatts(oldheader)) &&
		((newheader->t_infomask & ~HEAP_XACT_MASK) ==
		 (oldheader->t_infomask & ~HEAP_XACT_MASK)) &&
		memcmp(((char *) newheader) + SizeofHeapTupleHeader,
			   ((char *) oldheader) + SizeofHeapTupleHeader,
			   newtuple->t_len - SizeofHeapTupleHeader) == 0)
	{
		/* ... then suppress the update */
		rettuple = NULL;
	}

	return PointerGetDatum(rettuple);
}
