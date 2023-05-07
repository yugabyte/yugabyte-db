/*-------------------------------------------------------------------------
 *
 * tupconvert.c
 *	  Tuple conversion support.
 *
 * These functions provide conversion between rowtypes that are logically
 * equivalent but might have columns in a different order or different sets of
 * dropped columns.
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/backend/access/common/tupconvert.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/tupconvert.h"
#include "executor/tuptable.h"

#include "pg_yb_utils.h"
#include "access/sysattr.h"

/*
 * The conversion setup routines have the following common API:
 *
 * The setup routine checks using attmap.c whether the given source and
 * destination tuple descriptors are logically compatible.  If not, it throws
 * an error.  If so, it returns NULL if they are physically compatible (ie, no
 * conversion is needed), else a TupleConversionMap that can be used by
 * execute_attr_map_tuple or execute_attr_map_slot to perform the conversion.
 *
 * The TupleConversionMap, if needed, is palloc'd in the caller's memory
 * context.  Also, the given tuple descriptors are referenced by the map,
 * so they must survive as long as the map is needed.
 *
 * The caller must supply a suitable primary error message to be used if
 * a compatibility error is thrown.  Recommended coding practice is to use
 * gettext_noop() on this string, so that it is translatable but won't
 * actually be translated unless the error gets thrown.
 *
 *
 * Implementation notes:
 *
 * The key component of a TupleConversionMap is an attrMap[] array with
 * one entry per output column.  This entry contains the 1-based index of
 * the corresponding input column, or zero to force a NULL value (for
 * a dropped output column).  The TupleConversionMap also contains workspace
 * arrays.
 */


/*
 * Set up for tuple conversion, matching input and output columns by
 * position.  (Dropped columns are ignored in both input and output.)
 */
TupleConversionMap *
convert_tuples_by_position(TupleDesc indesc,
						   TupleDesc outdesc,
						   const char *msg)
{
	TupleConversionMap *map;
	int			n;
	AttrMap    *attrMap;

	/* Verify compatibility and prepare attribute-number map */
	attrMap = build_attrmap_by_position(indesc, outdesc, msg);

	if (attrMap == NULL)
	{
		/* runtime conversion is not needed */
		return NULL;
	}

	/* Prepare the map structure */
	map = (TupleConversionMap *) palloc(sizeof(TupleConversionMap));
	map->indesc = indesc;
	map->outdesc = outdesc;
	map->attrMap = attrMap;
	/* preallocate workspace for Datum arrays */
	n = outdesc->natts + 1;		/* +1 for NULL */
	map->outvalues = (Datum *) palloc(n * sizeof(Datum));
	map->outisnull = (bool *) palloc(n * sizeof(bool));
	n = indesc->natts + 1;		/* +1 for NULL */
	map->invalues = (Datum *) palloc(n * sizeof(Datum));
	map->inisnull = (bool *) palloc(n * sizeof(bool));
	map->invalues[0] = (Datum) 0;	/* set up the NULL entry */
	map->inisnull[0] = true;

	return map;
}

/*
 * Set up for tuple conversion, matching input and output columns by name.
 * (Dropped columns are ignored in both input and output.)	This is intended
 * for use when the rowtypes are related by inheritance, so we expect an exact
 * match of both type and typmod.  The error messages will be a bit unhelpful
 * unless both rowtypes are named composite types.
 */
TupleConversionMap *
convert_tuples_by_name(TupleDesc indesc,
					   TupleDesc outdesc)
{
	TupleConversionMap *map;
	AttrMap    *attrMap;
	int			n = outdesc->natts;

	/* Verify compatibility and prepare attribute-number map */
	attrMap = build_attrmap_by_name_if_req(indesc, outdesc);

	if (attrMap == NULL)
	{
		/* runtime conversion is not needed */
		return NULL;
	}

	/* Prepare the map structure */
	map = (TupleConversionMap *) palloc(sizeof(TupleConversionMap));
	map->indesc = indesc;
	map->outdesc = outdesc;
	map->attrMap = attrMap;
	/* preallocate workspace for Datum arrays */
	map->outvalues = (Datum *) palloc(n * sizeof(Datum));
	map->outisnull = (bool *) palloc(n * sizeof(bool));
	n = indesc->natts + 1;		/* +1 for NULL */
	map->invalues = (Datum *) palloc(n * sizeof(Datum));
	map->inisnull = (bool *) palloc(n * sizeof(bool));
	map->invalues[0] = (Datum) 0;	/* set up the NULL entry */
	map->inisnull[0] = true;

	return map;
}

#ifdef YB_TODO
/* YB_TODO(neil) This function is no longer in Pg15 */
/*
 * Return a palloc'd bare attribute map for tuple conversion, matching input
 * and output columns by name.  (Dropped columns are ignored in both input and
 * output.)  This is normally a subroutine for convert_tuples_by_name, but can
 * be used standalone.
 */
AttrNumber *
convert_tuples_by_name_map(TupleDesc indesc, TupleDesc outdesc, const char *msg,
						   bool yb_ignore_type_mismatch)
{
	AttrNumber *attrMap;
	int			n;
	int			i;

	n = outdesc->natts;
	attrMap = (AttrNumber *) palloc0(n * sizeof(AttrNumber));
	for (i = 0; i < n; i++)
	{
		Form_pg_attribute outatt = TupleDescAttr(outdesc, i);
		char	   *attname;
		Oid			atttypid;
		int32		atttypmod;
		int			j;

		if (outatt->attisdropped)
			continue;			/* attrMap[i] is already 0 */
		attname = NameStr(outatt->attname);
		atttypid = outatt->atttypid;
		atttypmod = outatt->atttypmod;
		for (j = 0; j < indesc->natts; j++)
		{
			Form_pg_attribute inatt = TupleDescAttr(indesc, j);

			if (inatt->attisdropped)
				continue;
			if (strcmp(attname, NameStr(inatt->attname)) == 0)
			{
				/* Found it, check type */
				if (!(IsYugaByteEnabled() && yb_ignore_type_mismatch) &&
					(atttypid != inatt->atttypid ||
					 atttypmod != inatt->atttypmod))
					ereport(ERROR,
							(errcode(ERRCODE_DATATYPE_MISMATCH),
							 errmsg_internal("%s", _(msg)),
							 errdetail("Attribute \"%s\" of type %s does not match corresponding attribute of type %s.",
									   attname,
									   format_type_be(outdesc->tdtypeid),
									   format_type_be(indesc->tdtypeid))));
				attrMap[i] = (AttrNumber) (j + 1);
				break;
			}
		}
		if (attrMap[i] == 0)
			ereport(ERROR,
					(errcode(ERRCODE_DATATYPE_MISMATCH),
					 errmsg_internal("%s", _(msg)),
					 errdetail("Attribute \"%s\" of type %s does not exist in type %s.",
							   attname,
							   format_type_be(outdesc->tdtypeid),
							   format_type_be(indesc->tdtypeid))));
	}

	return attrMap;
}

/*
 * Returns mapping created by convert_tuples_by_name_map, or NULL if no
 * conversion not required. This is a convenience routine for
 * convert_tuples_by_name() and other functions.
 */
AttrNumber *
convert_tuples_by_name_map_if_req(TupleDesc indesc,
								  TupleDesc outdesc,
								  const char *msg)
{
	AttrNumber *attrMap;
	int			n = outdesc->natts;
	int			i;
	bool		same;

	/* Verify compatibility and prepare attribute-number map */
	attrMap = convert_tuples_by_name_map(indesc, outdesc, msg,
										 false /* yb_ignore_type_mismatch */);

	/*
	 * Check to see if the map is one-to-one, in which case we need not do a
	 * tuple conversion.  We must also insist that both tupdescs either
	 * specify or don't specify an OID column, else we need a conversion to
	 * add/remove space for that.  (For some callers, presence or absence of
	 * an OID column perhaps would not really matter, but let's be safe.)
	 */
	if (indesc->natts == outdesc->natts &&
		indesc->tdhasoid == outdesc->tdhasoid)
	{
		same = true;
		for (i = 0; i < n; i++)
		{
			Form_pg_attribute inatt;
			Form_pg_attribute outatt;

			if (attrMap[i] == (i + 1))
				continue;

			/*
			 * If it's a dropped column and the corresponding input column is
			 * also dropped, we needn't convert.  However, attlen and attalign
			 * must agree.
			 */
			inatt = TupleDescAttr(indesc, i);
			outatt = TupleDescAttr(outdesc, i);
			if (attrMap[i] == 0 &&
				inatt->attisdropped &&
				inatt->attlen == outatt->attlen &&
				inatt->attalign == outatt->attalign)
				continue;

			same = false;
			break;
		}
	}
	else
		same = false;

	if (same)
	{
		/* Runtime conversion is not needed */
		pfree(attrMap);
		return NULL;
	}
	else
		return attrMap;
}
#endif

/*
 * Perform conversion of a tuple according to the map.
 */
HeapTuple
execute_attr_map_tuple(HeapTuple tuple, TupleConversionMap *map)
{
	AttrMap    *attrMap = map->attrMap;
	Datum	   *invalues = map->invalues;
	bool	   *inisnull = map->inisnull;
	Datum	   *outvalues = map->outvalues;
	bool	   *outisnull = map->outisnull;
	int			i;

	/*
	 * Extract all the values of the old tuple, offsetting the arrays so that
	 * invalues[0] is left NULL and invalues[1] is the first source attribute;
	 * this exactly matches the numbering convention in attrMap.
	 */
	heap_deform_tuple(tuple, map->indesc, invalues + 1, inisnull + 1);

	/*
	 * Transpose into proper fields of the new tuple.
	 */
	Assert(attrMap->maplen == map->outdesc->natts);
	for (i = 0; i < attrMap->maplen; i++)
	{
		int			j = attrMap->attnums[i];

		outvalues[i] = invalues[j];
		outisnull[i] = inisnull[j];
	}

	/*
	 * Now form the new tuple.
	 */
	return heap_form_tuple(map->outdesc, outvalues, outisnull);
}

/*
 * Perform conversion of a tuple slot according to the map.
 */
TupleTableSlot *
execute_attr_map_slot(AttrMap *attrMap,
					  TupleTableSlot *in_slot,
					  TupleTableSlot *out_slot)
{
	Datum	   *invalues;
	bool	   *inisnull;
	Datum	   *outvalues;
	bool	   *outisnull;
	int			outnatts;
	int			i;

	/* Sanity checks */
	Assert(in_slot->tts_tupleDescriptor != NULL &&
		   out_slot->tts_tupleDescriptor != NULL);
	Assert(in_slot->tts_values != NULL && out_slot->tts_values != NULL);

	outnatts = out_slot->tts_tupleDescriptor->natts;

	/* Extract all the values of the in slot. */
	slot_getallattrs(in_slot);

	/* Before doing the mapping, clear any old contents from the out slot */
	ExecClearTuple(out_slot);

	invalues = in_slot->tts_values;
	inisnull = in_slot->tts_isnull;
	outvalues = out_slot->tts_values;
	outisnull = out_slot->tts_isnull;

	/* Transpose into proper fields of the out slot. */
	for (i = 0; i < outnatts; i++)
	{
		int			j = attrMap->attnums[i] - 1;

		/* attrMap->attnums[i] == 0 means it's a NULL datum. */
		if (j == -1)
		{
			outvalues[i] = (Datum) 0;
			outisnull[i] = true;
		}
		else
		{
			outvalues[i] = invalues[j];
			outisnull[i] = inisnull[j];
		}
	}

	ExecStoreVirtualTuple(out_slot);

	return out_slot;
}

/*
 * Perform conversion of bitmap of columns according to the map.
 *
 * The input and output bitmaps are offset by
 * YBGetFirstLowInvalidAttributeNumber to accommodate system cols, like the
 * column-bitmaps in RangeTblEntry.
 *
 * Note: Postgres uses FirstLowInvalidHeapAttributeNumber.
 */
/* YB_TODO(neil) Need to choose between attrMap and map when fixing compiling errors */
Bitmapset *
execute_attr_map_cols(AttrMap *attrMap, Bitmapset *in_cols, Relation rel)
{
	Bitmapset  *out_cols;
	int			out_attnum;

	/* fast path for the common trivial case */
	if (in_cols == NULL)
		return NULL;

	/*
	 * For each output column, check which input column it corresponds to.
	 */
	out_cols = NULL;

	for (out_attnum = YBGetFirstLowInvalidAttributeNumber(rel) + 1;
		 out_attnum <= attrMap->maplen;
		 out_attnum++)
	{
		int			in_attnum;

		if (out_attnum < 0)
		{
			/* System column. No mapping. */
			in_attnum = out_attnum;
		}
		else if (out_attnum == 0)
			continue;
		else
		{
			/* normal user column */
			in_attnum = attrMap->attnums[out_attnum - 1];

			if (in_attnum == 0)
				continue;
		}

		if (bms_is_member(in_attnum - YBGetFirstLowInvalidAttributeNumber(rel), in_cols))
			out_cols = bms_add_member(out_cols,
									  out_attnum - YBGetFirstLowInvalidAttributeNumber(rel));
	}

	return out_cols;
}

/*
 * Free a TupleConversionMap structure.
 */
void
free_conversion_map(TupleConversionMap *map)
{
	/* indesc and outdesc are not ours to free */
	free_attrmap(map->attrMap);
	pfree(map->invalues);
	pfree(map->inisnull);
	pfree(map->outvalues);
	pfree(map->outisnull);
	pfree(map);
}
