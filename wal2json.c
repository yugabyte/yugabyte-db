/*-------------------------------------------------------------------------
 *
 * wal2json.c
 * 		JSON output plugin for changeset extraction
 *
 * Copyright (c) 2013-2017, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *		contrib/wal2json/wal2json.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "catalog/pg_type.h"

#include "replication/logical.h"

#include "utils/builtins.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"
#include "utils/pg_lsn.h"
#include "utils/rel.h"
#include "utils/syscache.h"

PG_MODULE_MAGIC;

extern void		_PG_init(void);
extern void		_PG_output_plugin_init(OutputPluginCallbacks *cb);

typedef struct
{
	MemoryContext context;
	bool		include_xids;		/* include transaction ids */
	bool		include_timestamp;	/* include transaction timestamp */
	bool		include_schemas;	/* qualify tables */
	bool		include_types;		/* include data types */
	bool		include_type_oids;	/* include data type oids */
	bool		include_typmod;		/* include typmod in types */
	bool		include_not_null;	/* include not-null constraints */

	bool		pretty_print;		/* pretty-print JSON? */
	bool		write_in_chunks;	/* write in chunks? */

	/*
	 * LSN pointing to the end of commit record + 1 (txn->end_lsn)
	 * It is useful for tools that wants a position to restart from.
	 */
	bool		include_lsn;		/* include LSNs */

	uint64		nr_changes;			/* # of passes in pg_decode_change() */
									/* FIXME replace with txn->nentries */
} JsonDecodingData;

/* These must be available to pg_dlsym() */
static void pg_decode_startup(LogicalDecodingContext *ctx, OutputPluginOptions *opt, bool is_init);
static void pg_decode_shutdown(LogicalDecodingContext *ctx);
static void pg_decode_begin_txn(LogicalDecodingContext *ctx,
					ReorderBufferTXN *txn);
static void pg_decode_commit_txn(LogicalDecodingContext *ctx,
					 ReorderBufferTXN *txn, XLogRecPtr commit_lsn);
static void pg_decode_change(LogicalDecodingContext *ctx,
				 ReorderBufferTXN *txn, Relation rel,
				 ReorderBufferChange *change);
#if	PG_VERSION_NUM >= 90600
static void pg_decode_message(LogicalDecodingContext *ctx,
					ReorderBufferTXN *txn, XLogRecPtr lsn,
					bool transactional, const char *prefix,
					Size content_size, const char *content);
#endif

void
_PG_init(void)
{
}

/* Specify output plugin callbacks */
void
_PG_output_plugin_init(OutputPluginCallbacks *cb)
{
	AssertVariableIsOfType(&_PG_output_plugin_init, LogicalOutputPluginInit);

	cb->startup_cb = pg_decode_startup;
	cb->begin_cb = pg_decode_begin_txn;
	cb->change_cb = pg_decode_change;
	cb->commit_cb = pg_decode_commit_txn;
	cb->shutdown_cb = pg_decode_shutdown;
#if	PG_VERSION_NUM >= 90600
	cb->message_cb = pg_decode_message;
#endif
}

/* Initialize this plugin */
static void
pg_decode_startup(LogicalDecodingContext *ctx, OutputPluginOptions *opt, bool is_init)
{
	ListCell	*option;
	JsonDecodingData *data;

	data = palloc0(sizeof(JsonDecodingData));
	data->context = AllocSetContextCreate(TopMemoryContext,
										"wal2json output context",
#if PG_VERSION_NUM >= 90600
										ALLOCSET_DEFAULT_SIZES
#else
										ALLOCSET_DEFAULT_MINSIZE,
										ALLOCSET_DEFAULT_INITSIZE,
										ALLOCSET_DEFAULT_MAXSIZE
#endif
                                        );
	data->include_xids = false;
	data->include_timestamp = false;
	data->include_schemas = true;
	data->include_types = true;
	data->include_type_oids = false;
	data->include_typmod = true;
	data->pretty_print = false;
	data->write_in_chunks = false;
	data->include_lsn = false;
	data->include_not_null = false;

	data->nr_changes = 0;

	ctx->output_plugin_private = data;

	opt->output_type = OUTPUT_PLUGIN_TEXTUAL_OUTPUT;

	foreach(option, ctx->output_plugin_options)
	{
		DefElem *elem = lfirst(option);

		Assert(elem->arg == NULL || IsA(elem->arg, String));

		if (strcmp(elem->defname, "include-xids") == 0)
		{
			/* If option does not provide a value, it means its value is true */
			if (elem->arg == NULL)
			{
				elog(LOG, "include-xids argument is null");
				data->include_xids = true;
			}
			else if (!parse_bool(strVal(elem->arg), &data->include_xids))
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("could not parse value \"%s\" for parameter \"%s\"",
							 strVal(elem->arg), elem->defname)));
		}
		else if (strcmp(elem->defname, "include-timestamp") == 0)
		{
			if (elem->arg == NULL)
			{
				elog(LOG, "include-timestamp argument is null");
				data->include_timestamp = true;
			}
			else if (!parse_bool(strVal(elem->arg), &data->include_timestamp))
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("could not parse value \"%s\" for parameter \"%s\"",
							 strVal(elem->arg), elem->defname)));
		}
		else if (strcmp(elem->defname, "include-schemas") == 0)
		{
			if (elem->arg == NULL)
			{
				elog(LOG, "include-schemas argument is null");
				data->include_schemas = true;
			}
			else if (!parse_bool(strVal(elem->arg), &data->include_schemas))
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("could not parse value \"%s\" for parameter \"%s\"",
							 strVal(elem->arg), elem->defname)));
		}
		else if (strcmp(elem->defname, "include-types") == 0)
		{
			if (elem->arg == NULL)
			{
				elog(LOG, "include-types argument is null");
				data->include_types = true;
			}
			else if (!parse_bool(strVal(elem->arg), &data->include_types))
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("could not parse value \"%s\" for parameter \"%s\"",
							 strVal(elem->arg), elem->defname)));
		}
		else if (strcmp(elem->defname, "include-type-oids") == 0)
		{
			if (elem->arg == NULL)
			{
				elog(LOG, "include-type-oids argument is null");
				data->include_type_oids = true;
			}
			else if (!parse_bool(strVal(elem->arg), &data->include_type_oids))
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("could not parse value \"%s\" for parameter \"%s\"",
							 strVal(elem->arg), elem->defname)));
		}
		else if (strcmp(elem->defname, "include-typmod") == 0)
		{
			if (elem->arg == NULL)
			{
				elog(LOG, "include-typmod argument is null");
				data->include_typmod = true;
			}
			else if (!parse_bool(strVal(elem->arg), &data->include_typmod))
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("could not parse value \"%s\" for parameter \"%s\"",
							 strVal(elem->arg), elem->defname)));
		}
		else if (strcmp(elem->defname, "include-not-null") == 0)
		{
			if (elem->arg == NULL)
			{
				elog(LOG, "include-not-null argument is null");
				data->include_not_null = true;
			}
			else if (!parse_bool(strVal(elem->arg), &data->include_not_null))
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("could not parse value \"%s\" for parameter \"%s\"",
							 strVal(elem->arg), elem->defname)));
		}
		else if (strcmp(elem->defname, "pretty-print") == 0)
		{
			if (elem->arg == NULL)
			{
				elog(LOG, "pretty-print argument is null");
				data->pretty_print = true;
			}
			else if (!parse_bool(strVal(elem->arg), &data->pretty_print))
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("could not parse value \"%s\" for parameter \"%s\"",
							 strVal(elem->arg), elem->defname)));
		}
		else if (strcmp(elem->defname, "write-in-chunks") == 0)
		{
			if (elem->arg == NULL)
			{
				elog(LOG, "write-in-chunks argument is null");
				data->write_in_chunks = true;
			}
			else if (!parse_bool(strVal(elem->arg), &data->write_in_chunks))
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("could not parse value \"%s\" for parameter \"%s\"",
							 strVal(elem->arg), elem->defname)));
		}
		else if (strcmp(elem->defname, "include-lsn") == 0)
		{
			if (elem->arg == NULL)
			{
				elog(LOG, "include-lsn argument is null");
				data->include_lsn = true;
			}
			else if (!parse_bool(strVal(elem->arg), &data->include_lsn))
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("could not parse value \"%s\" for parameter \"%s\"",
							 strVal(elem->arg), elem->defname)));
		}
		else
		{
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("option \"%s\" = \"%s\" is unknown",
						elem->defname,
						elem->arg ? strVal(elem->arg) : "(null)")));
		}
	}
}

/* cleanup this plugin's resources */
static void
pg_decode_shutdown(LogicalDecodingContext *ctx)
{
	JsonDecodingData *data = ctx->output_plugin_private;

	/* cleanup our own resources via memory context reset */
	MemoryContextDelete(data->context);
}

/* BEGIN callback */
static void
pg_decode_begin_txn(LogicalDecodingContext *ctx, ReorderBufferTXN *txn)
{
	JsonDecodingData *data = ctx->output_plugin_private;

	data->nr_changes = 0;

	/* Transaction starts */
	OutputPluginPrepareWrite(ctx, true);

	if (data->pretty_print)
		appendStringInfoString(ctx->out, "{\n");
	else
		appendStringInfoChar(ctx->out, '{');

	if (data->include_xids)
	{
		if (data->pretty_print)
			appendStringInfo(ctx->out, "\t\"xid\": %u,\n", txn->xid);
		else
			appendStringInfo(ctx->out, "\"xid\":%u,", txn->xid);
	}

	if (data->include_lsn)
	{
		char *lsn_str = DatumGetCString(DirectFunctionCall1(pg_lsn_out, txn->end_lsn));

		if (data->pretty_print)
			appendStringInfo(ctx->out, "\t\"nextlsn\": \"%s\",\n", lsn_str);
		else
			appendStringInfo(ctx->out, "\"nextlsn\":\"%s\",", lsn_str);

		pfree(lsn_str);
	}

	if (data->include_timestamp)
	{
		if (data->pretty_print)
			appendStringInfo(ctx->out, "\t\"timestamp\": \"%s\",\n", timestamptz_to_str(txn->commit_time));
		else
			appendStringInfo(ctx->out, "\"timestamp\":\"%s\",", timestamptz_to_str(txn->commit_time));
	}

	if (data->pretty_print)
		appendStringInfoString(ctx->out, "\t\"change\": [");
	else
		appendStringInfoString(ctx->out, "\"change\":[");

	if (data->write_in_chunks)
		OutputPluginWrite(ctx, true);
}

/* COMMIT callback */
static void
pg_decode_commit_txn(LogicalDecodingContext *ctx, ReorderBufferTXN *txn,
					 XLogRecPtr commit_lsn)
{
	JsonDecodingData *data = ctx->output_plugin_private;

	if (txn->has_catalog_changes)
		elog(DEBUG1, "txn has catalog changes: yes");
	else
		elog(DEBUG1, "txn has catalog changes: no");
	elog(DEBUG1, "my change counter: %lu ; # of changes: %lu ; # of changes in memory: %lu", data->nr_changes, txn->nentries, txn->nentries_mem);
	elog(DEBUG1, "# of subxacts: %d", txn->nsubtxns);

	/* Transaction ends */
	if (data->write_in_chunks)
		OutputPluginPrepareWrite(ctx, true);

	if (data->pretty_print)
	{
		/* if we don't write in chunks, we need a newline here */
		if (!data->write_in_chunks)
			appendStringInfoChar(ctx->out, '\n');

		appendStringInfoString(ctx->out, "\t]\n}");
	}
	else
	{
		appendStringInfoString(ctx->out, "]}");
	}

	OutputPluginWrite(ctx, true);
}

/*
 * Format a string as a JSON literal
 * XXX it doesn't do a sanity check for invalid input, does it?
 * FIXME it doesn't handle \uxxxx
 */
static void
quote_escape_json(StringInfo buf, const char *val)
{
	const char *valptr;

	appendStringInfoChar(buf, '"');
	for (valptr = val; *valptr; valptr++)
	{
		char		ch = *valptr;

		/* XXX suppress \x in bytea field? */
		if (ch == '\\' && *(valptr + 1) == 'x')
		{
			valptr++;
			continue;
		}

		switch (ch)
		{
			case '"':
			case '\\':
			case '/':
				appendStringInfo(buf, "\\%c", ch);
				break;
			case '\b':
				appendStringInfoString(buf, "\\b");
				break;
			case '\f':
				appendStringInfoString(buf, "\\f");
				break;
			case '\n':
				appendStringInfoString(buf, "\\n");
				break;
			case '\r':
				appendStringInfoString(buf, "\\r");
				break;
			case '\t':
				appendStringInfoString(buf, "\\t");
				break;
			default:
				appendStringInfoChar(buf, ch);
				break;
		}
	}
	appendStringInfoChar(buf, '"');
}

/*
 * Accumulate tuple information and stores it at the end
 *
 * replident: is this tuple a replica identity?
 * hasreplident: does this tuple has an associated replica identity?
 */
static void
tuple_to_stringinfo(LogicalDecodingContext *ctx, TupleDesc tupdesc, HeapTuple tuple, TupleDesc indexdesc, bool replident, bool hasreplident)
{
	JsonDecodingData	*data;
	int					natt;

	StringInfoData		colnames;
	StringInfoData		coltypes;
	StringInfoData		coltypeoids;
	StringInfoData		colnotnulls;
	StringInfoData		colvalues;
	char				*comma = "";

	data = ctx->output_plugin_private;

	initStringInfo(&colnames);
	initStringInfo(&coltypes);
	if (data->include_type_oids)
		initStringInfo(&coltypeoids);
	if (data->include_not_null)
		initStringInfo(&colnotnulls);
	initStringInfo(&colvalues);

	/*
	 * If replident is true, it will output info about replica identity. In this
	 * case, there are special JSON objects for it. Otherwise, it will print new
	 * tuple data.
	 */
	if (replident)
	{
		if (data->pretty_print)
		{
			appendStringInfoString(&colnames, "\t\t\t\"oldkeys\": {\n");
			appendStringInfoString(&colnames, "\t\t\t\t\"keynames\": [");
			appendStringInfoString(&coltypes, "\t\t\t\t\"keytypes\": [");
			if (data->include_type_oids)
				appendStringInfoString(&coltypeoids, "\t\t\t\"keytypeoids\": [");
			appendStringInfoString(&colvalues, "\t\t\t\t\"keyvalues\": [");
		}
		else
		{
			appendStringInfoString(&colnames, "\"oldkeys\":{");
			appendStringInfoString(&colnames, "\"keynames\":[");
			appendStringInfoString(&coltypes, "\"keytypes\":[");
			if (data->include_type_oids)
				appendStringInfoString(&coltypeoids, "\"keytypeoids\": [");
			appendStringInfoString(&colvalues, "\"keyvalues\":[");
		}
	}
	else
	{
		if (data->pretty_print)
		{
			appendStringInfoString(&colnames, "\t\t\t\"columnnames\": [");
			appendStringInfoString(&coltypes, "\t\t\t\"columntypes\": [");
			if (data->include_type_oids)
				appendStringInfoString(&coltypeoids, "\t\t\t\"columntypeoids\": [");
			if (data->include_not_null)
				appendStringInfoString(&colnotnulls, "\t\t\t\"columnoptionals\": [");
			appendStringInfoString(&colvalues, "\t\t\t\"columnvalues\": [");
		}
		else
		{
			appendStringInfoString(&colnames, "\"columnnames\":[");
			appendStringInfoString(&coltypes, "\"columntypes\":[");
			if (data->include_type_oids)
				appendStringInfoString(&coltypeoids, "\"columntypeoids\": [");
			if (data->include_not_null)
				appendStringInfoString(&colnotnulls, "\"columnoptionals\": [");
			appendStringInfoString(&colvalues, "\"columnvalues\":[");
		}
	}

	/* Print column information (name, type, value) */
	for (natt = 0; natt < tupdesc->natts; natt++)
	{
		Form_pg_attribute	attr;		/* the attribute itself */
		Oid					typid;		/* type of current attribute */
		HeapTuple			type_tuple;	/* information about a type */
		Oid					typoutput;	/* output function */
		bool				typisvarlena;
		Datum				origval;	/* possibly toasted Datum */
		Datum				val;		/* definitely detoasted Datum */
		char				*outputstr = NULL;
		bool				isnull;		/* column is null? */

		/*
		 * Commit d34a74dd064af959acd9040446925d9d53dff15b introduced
		 * TupleDescAttr() in back branches. If the version supports
		 * this macro, use it. Version 10 and later already support it.
		 */
#if (PG_VERSION_NUM >= 90600 && PG_VERSION_NUM < 90605) || (PG_VERSION_NUM >= 90500 && PG_VERSION_NUM < 90509) || (PG_VERSION_NUM >= 90400 && PG_VERSION_NUM < 90414)
		attr = tupdesc->attrs[natt];
#else
		attr = TupleDescAttr(tupdesc, natt);
#endif

		elog(DEBUG1, "attribute \"%s\" (%d/%d)", NameStr(attr->attname), natt, tupdesc->natts);

		/* Do not print dropped or system columns */
		if (attr->attisdropped || attr->attnum < 0)
			continue;

		/* Search indexed columns in whole heap tuple */
		if (indexdesc != NULL)
		{
			int		j;
			bool	found_col = false;

			for (j = 0; j < indexdesc->natts; j++)
			{
				Form_pg_attribute	iattr;

				/* See explanation a few lines above. */
#if (PG_VERSION_NUM >= 90600 && PG_VERSION_NUM < 90605) || (PG_VERSION_NUM >= 90500 && PG_VERSION_NUM < 90509) || (PG_VERSION_NUM >= 90400 && PG_VERSION_NUM < 90414)
				iattr = indexdesc->attrs[j];
#else
				iattr = TupleDescAttr(indexdesc, j);
#endif

				if (strcmp(NameStr(attr->attname), NameStr(iattr->attname)) == 0)
					found_col = true;

			}

			/* Print only indexed columns */
			if (!found_col)
				continue;
		}

		typid = attr->atttypid;

		/* Figure out type name */
		type_tuple = SearchSysCache1(TYPEOID, ObjectIdGetDatum(typid));
		if (!HeapTupleIsValid(type_tuple))
			elog(ERROR, "cache lookup failed for type %u", typid);

		/* Get information needed for printing values of a type */
		getTypeOutputInfo(typid, &typoutput, &typisvarlena);

		/* Get Datum from tuple */
		origval = heap_getattr(tuple, natt + 1, tupdesc, &isnull);

		/* Skip nulls iif printing key/identity */
		if (isnull && replident)
			continue;

		/* XXX Unchanged TOAST Datum does not need to be output */
		if (!isnull && typisvarlena && VARATT_IS_EXTERNAL_ONDISK(origval))
		{
			elog(WARNING, "column \"%s\" has an unchanged TOAST", NameStr(attr->attname));
			continue;
		}

		/* Accumulate each column info */
		appendStringInfo(&colnames, "%s\"%s\"", comma, NameStr(attr->attname));

		if (data->include_types)
		{
			if (data->include_typmod)
			{
				char	*type_str;

				type_str = TextDatumGetCString(DirectFunctionCall2(format_type, attr->atttypid, attr->atttypmod));
				appendStringInfo(&coltypes, "%s\"%s\"", comma, type_str);
				pfree(type_str);
			}
			else
			{
				Form_pg_type type_form = (Form_pg_type) GETSTRUCT(type_tuple);
				appendStringInfo(&coltypes, "%s\"%s\"", comma, NameStr(type_form->typname));
			}

			/* oldkeys doesn't print not-null constraints */
			if (!replident && data->include_not_null)
			{
				if (attr->attnotnull)
					appendStringInfo(&colnotnulls, "%sfalse", comma);
				else
					appendStringInfo(&colnotnulls, "%strue", comma);
			}
		}

		if (data->include_type_oids)
			appendStringInfo(&coltypeoids, "%s%u", comma, typid);

		ReleaseSysCache(type_tuple);

		if (isnull)
		{
			appendStringInfo(&colvalues, "%snull", comma);
		}
		else
		{
			if (typisvarlena)
				val = PointerGetDatum(PG_DETOAST_DATUM(origval));
			else
				val = origval;

			/* Finally got the value */
			outputstr = OidOutputFunctionCall(typoutput, val);

			/*
			 * Data types are printed with quotes unless they are number, true,
			 * false, null, an array or an object.
			 *
			 * The NaN and Infinity are not valid JSON symbols. Hence,
			 * regardless of sign they are represented as the string null.
			 */
			switch (typid)
			{
				case INT2OID:
				case INT4OID:
				case INT8OID:
				case OIDOID:
				case FLOAT4OID:
				case FLOAT8OID:
				case NUMERICOID:
					if (pg_strncasecmp(outputstr, "NaN", 3) == 0 ||
							pg_strncasecmp(outputstr, "Infinity", 8) == 0 ||
							pg_strncasecmp(outputstr, "-Infinity", 9) == 0)
					{
						appendStringInfo(&colvalues, "%snull", comma);
						elog(DEBUG1, "attribute \"%s\" is special: %s", NameStr(attr->attname), outputstr);
					}
					else if (strspn(outputstr, "0123456789+-eE.") == strlen(outputstr))
						appendStringInfo(&colvalues, "%s%s", comma, outputstr);
					else
						elog(ERROR, "%s is not a number", outputstr);
					break;
				case BOOLOID:
					if (strcmp(outputstr, "t") == 0)
						appendStringInfo(&colvalues, "%strue", comma);
					else
						appendStringInfo(&colvalues, "%sfalse", comma);
					break;
				default:
					appendStringInfoString(&colvalues, comma);
					quote_escape_json(&colvalues, outputstr);
					break;
			}
		}

		/* The first column does not have comma */
		if (strcmp(comma, "") == 0)
		{
			if (data->pretty_print)
				comma = ", ";
			else
				comma = ",";
		}
	}

	/* Column info ends */
	if (replident)
	{
		if (data->pretty_print)
		{
			appendStringInfoString(&colnames, "],\n");
			if (data->include_types)
				appendStringInfoString(&coltypes, "],\n");
			if (data->include_type_oids)
				appendStringInfoString(&coltypeoids, "],\n");
			appendStringInfoString(&colvalues, "]\n");
			appendStringInfoString(&colvalues, "\t\t\t}\n");
		}
		else
		{
			appendStringInfoString(&colnames, "],");
			if (data->include_types)
				appendStringInfoString(&coltypes, "],");
			if (data->include_type_oids)
				appendStringInfoString(&coltypeoids, "],");
			appendStringInfoChar(&colvalues, ']');
			appendStringInfoChar(&colvalues, '}');
		}
	}
	else
	{
		if (data->pretty_print)
		{
			appendStringInfoString(&colnames, "],\n");
			if (data->include_types)
				appendStringInfoString(&coltypes, "],\n");
			if (data->include_type_oids)
				appendStringInfoString(&coltypeoids, "],\n");
			if (data->include_not_null)
				appendStringInfoString(&colnotnulls, "],\n");
			if (hasreplident)
				appendStringInfoString(&colvalues, "],\n");
			else
				appendStringInfoString(&colvalues, "]\n");
		}
		else
		{
			appendStringInfoString(&colnames, "],");
			if (data->include_types)
				appendStringInfoString(&coltypes, "],");
			if (data->include_type_oids)
				appendStringInfoString(&coltypeoids, "],");
			if (data->include_not_null)
				appendStringInfoString(&colnotnulls, "],");
			if (hasreplident)
				appendStringInfoString(&colvalues, "],");
			else
				appendStringInfoChar(&colvalues, ']');
		}
	}

	/* Print data */
	appendStringInfoString(ctx->out, colnames.data);
	if (data->include_types)
		appendStringInfoString(ctx->out, coltypes.data);
	if (data->include_type_oids)
		appendStringInfoString(ctx->out, coltypeoids.data);
	if (data->include_not_null)
		appendStringInfoString(ctx->out, colnotnulls.data);
	appendStringInfoString(ctx->out, colvalues.data);

	pfree(colnames.data);
	pfree(coltypes.data);
	if (data->include_type_oids)
		pfree(coltypeoids.data);
	if (data->include_not_null)
		pfree(colnotnulls.data);
	pfree(colvalues.data);
}

/* Print columns information */
static void
columns_to_stringinfo(LogicalDecodingContext *ctx, TupleDesc tupdesc, HeapTuple tuple, bool hasreplident)
{
	tuple_to_stringinfo(ctx, tupdesc, tuple, NULL, false, hasreplident);
}

/* Print replica identity information */
static void
identity_to_stringinfo(LogicalDecodingContext *ctx, TupleDesc tupdesc, HeapTuple tuple, TupleDesc indexdesc)
{
	/* Last parameter does not matter */
	tuple_to_stringinfo(ctx, tupdesc, tuple, indexdesc, true, false);
}

/* Callback for individual changed tuples */
static void
pg_decode_change(LogicalDecodingContext *ctx, ReorderBufferTXN *txn,
				 Relation relation, ReorderBufferChange *change)
{
	JsonDecodingData *data;
	Form_pg_class class_form;
	TupleDesc	tupdesc;
	MemoryContext old;

	Relation	indexrel;
	TupleDesc	indexdesc;

	AssertVariableIsOfType(&pg_decode_change, LogicalDecodeChangeCB);

	data = ctx->output_plugin_private;
	class_form = RelationGetForm(relation);
	tupdesc = RelationGetDescr(relation);

	/* Avoid leaking memory by using and resetting our own context */
	old = MemoryContextSwitchTo(data->context);

	if (data->write_in_chunks)
		OutputPluginPrepareWrite(ctx, true);

	/* Make sure rd_replidindex is set */
	RelationGetIndexList(relation);

	/* Sanity checks */
	switch (change->action)
	{
		case REORDER_BUFFER_CHANGE_INSERT:
			if (change->data.tp.newtuple == NULL)
			{
				elog(WARNING, "no tuple data for INSERT in table \"%s\"", NameStr(class_form->relname));
				MemoryContextSwitchTo(old);
				MemoryContextReset(data->context);
				return;
			}
			break;
		case REORDER_BUFFER_CHANGE_UPDATE:
			/*
			 * Bail out iif:
			 * (i) doesn't have a pk and replica identity is not full;
			 * (ii) replica identity is nothing.
			 */
			if (!OidIsValid(relation->rd_replidindex) && relation->rd_rel->relreplident != REPLICA_IDENTITY_FULL)
			{
				/* FIXME this sentence is imprecise */
				elog(WARNING, "table \"%s\" without primary key or replica identity is nothing", NameStr(class_form->relname));
				MemoryContextSwitchTo(old);
				MemoryContextReset(data->context);
				return;
			}

			if (change->data.tp.newtuple == NULL)
			{
				elog(WARNING, "no tuple data for UPDATE in table \"%s\"", NameStr(class_form->relname));
				MemoryContextSwitchTo(old);
				MemoryContextReset(data->context);
				return;
			}
			break;
		case REORDER_BUFFER_CHANGE_DELETE:
			/*
			 * Bail out iif:
			 * (i) doesn't have a pk and replica identity is not full;
			 * (ii) replica identity is nothing.
			 */
			if (!OidIsValid(relation->rd_replidindex) && relation->rd_rel->relreplident != REPLICA_IDENTITY_FULL)
			{
				/* FIXME this sentence is imprecise */
				elog(WARNING, "table \"%s\" without primary key or replica identity is nothing", NameStr(class_form->relname));
				MemoryContextSwitchTo(old);
				MemoryContextReset(data->context);
				return;
			}

			if (change->data.tp.oldtuple == NULL)
			{
				elog(WARNING, "no tuple data for DELETE in table \"%s\"", NameStr(class_form->relname));
				MemoryContextSwitchTo(old);
				MemoryContextReset(data->context);
				return;
			}
			break;
		default:
			Assert(false);
	}

	/* Change counter */
	data->nr_changes++;

	/* Change starts */
	if (data->pretty_print)
	{
		/* if we don't write in chunks, we need a newline here */
		if (!data->write_in_chunks)
			appendStringInfoChar(ctx->out, '\n');

		appendStringInfoString(ctx->out, "\t\t");

		if (data->nr_changes > 1)
			appendStringInfoChar(ctx->out, ',');

		appendStringInfoString(ctx->out, "{\n");
	}
	else
	{
		if (data->nr_changes > 1)
			appendStringInfoString(ctx->out, ",{");
		else
			appendStringInfoChar(ctx->out, '{');
	}

	/* Print change kind */
	switch (change->action)
	{
		case REORDER_BUFFER_CHANGE_INSERT:
			if (data->pretty_print)
				appendStringInfoString(ctx->out, "\t\t\t\"kind\": \"insert\",\n");
			else
				appendStringInfoString(ctx->out, "\"kind\":\"insert\",");
			break;
		case REORDER_BUFFER_CHANGE_UPDATE:
			if(data->pretty_print)
				appendStringInfoString(ctx->out, "\t\t\t\"kind\": \"update\",\n");
			else
				appendStringInfoString(ctx->out, "\"kind\":\"update\",");
			break;
		case REORDER_BUFFER_CHANGE_DELETE:
			if (data->pretty_print)
				appendStringInfoString(ctx->out, "\t\t\t\"kind\": \"delete\",\n");
			else
				appendStringInfoString(ctx->out, "\"kind\":\"delete\",");
			break;
		default:
			Assert(false);
	}

	/* Print table name (possibly) qualified */
	if (data->pretty_print)
	{
		if (data->include_schemas)
			appendStringInfo(ctx->out, "\t\t\t\"schema\": \"%s\",\n", get_namespace_name(class_form->relnamespace));
		appendStringInfo(ctx->out, "\t\t\t\"table\": \"%s\",\n", NameStr(class_form->relname));
	}
	else
	{
		if (data->include_schemas)
			appendStringInfo(ctx->out, "\"schema\":\"%s\",", get_namespace_name(class_form->relnamespace));
		appendStringInfo(ctx->out, "\"table\":\"%s\",", NameStr(class_form->relname));
	}

	switch (change->action)
	{
		case REORDER_BUFFER_CHANGE_INSERT:
			/* Print the new tuple */
			columns_to_stringinfo(ctx, tupdesc, &change->data.tp.newtuple->tuple, false);
			break;
		case REORDER_BUFFER_CHANGE_UPDATE:
			/* Print the new tuple */
			columns_to_stringinfo(ctx, tupdesc, &change->data.tp.newtuple->tuple, true);

			/*
			 * The old tuple is available when:
			 * (i) pk changes;
			 * (ii) replica identity is full;
			 * (iii) replica identity is index and indexed column changes.
			 *
			 * FIXME if old tuple is not available we must get only the indexed
			 * columns (the whole tuple is printed).
			 */
			if (change->data.tp.oldtuple == NULL)
			{
				elog(DEBUG1, "old tuple is null");

				indexrel = RelationIdGetRelation(relation->rd_replidindex);
				if (indexrel != NULL)
				{
					indexdesc = RelationGetDescr(indexrel);
					identity_to_stringinfo(ctx, tupdesc, &change->data.tp.newtuple->tuple, indexdesc);
					RelationClose(indexrel);
				}
				else
				{
					identity_to_stringinfo(ctx, tupdesc, &change->data.tp.newtuple->tuple, NULL);
				}
			}
			else
			{
				elog(DEBUG1, "old tuple is not null");
				identity_to_stringinfo(ctx, tupdesc, &change->data.tp.oldtuple->tuple, NULL);
			}
			break;
		case REORDER_BUFFER_CHANGE_DELETE:
			/* Print the replica identity */
			indexrel = RelationIdGetRelation(relation->rd_replidindex);
			if (indexrel != NULL)
			{
				indexdesc = RelationGetDescr(indexrel);
				identity_to_stringinfo(ctx, tupdesc, &change->data.tp.oldtuple->tuple, indexdesc);
				RelationClose(indexrel);
			}
			else
			{
				identity_to_stringinfo(ctx, tupdesc, &change->data.tp.oldtuple->tuple, NULL);
			}

			if (change->data.tp.oldtuple == NULL)
				elog(DEBUG1, "old tuple is null");
			else
				elog(DEBUG1, "old tuple is not null");
			break;
		default:
			Assert(false);
	}

	if (data->pretty_print)
		appendStringInfoString(ctx->out, "\t\t}");
	else
		appendStringInfoChar(ctx->out, '}');

	MemoryContextSwitchTo(old);
	MemoryContextReset(data->context);

	if (data->write_in_chunks)
		OutputPluginWrite(ctx, true);
}

#if	PG_VERSION_NUM >= 90600
/* Callback for generic logical decoding messages */
static void
pg_decode_message(LogicalDecodingContext *ctx, ReorderBufferTXN *txn,
		XLogRecPtr lsn, bool transactional, const char *prefix, Size
		content_size, const char *content)
{
	JsonDecodingData *data;
	MemoryContext old;

	data = ctx->output_plugin_private;

	/* Avoid leaking memory by using and resetting our own context */
	old = MemoryContextSwitchTo(data->context);

	/*
	 * write immediately iif (i) write-in-chunks=1 or (ii) non-transactional
	 * messages.
	 */
	if (data->write_in_chunks || !transactional)
		OutputPluginPrepareWrite(ctx, true);

	/*
	 * increment counter only for transactional messages because
	 * non-transactional message has only one object.
	 */
	if (transactional)
		data->nr_changes++;

	if (data->pretty_print)
	{
		/* if we don't write in chunks, we need a newline here */
		if (!data->write_in_chunks && transactional)
			appendStringInfoChar(ctx->out, '\n');

		/* build a complete JSON object for non-transactional message */
		if (!transactional)
		{
			appendStringInfoString(ctx->out, "{\n");
			appendStringInfoString(ctx->out, "\t\"change\": [\n");
		}

		appendStringInfoString(ctx->out, "\t\t");

		if (data->nr_changes > 1)
			appendStringInfoChar(ctx->out, ',');

		appendStringInfoString(ctx->out, "{\n");

		appendStringInfoString(ctx->out, "\t\t\t\"kind\": \"message\",\n");

		if (transactional)
			appendStringInfoString(ctx->out, "\t\t\t\"transactional\": true,\n");
		else
			appendStringInfoString(ctx->out, "\t\t\t\"transactional\": false,\n");

		appendStringInfo(ctx->out, "\t\t\t\"prefix\": \"%s\",\n", prefix);
		appendStringInfoString(ctx->out, "\t\t\t\"content\": \"");
		appendBinaryStringInfo(ctx->out, content, content_size);
		appendStringInfoString(ctx->out, "\"\n");
		appendStringInfoString(ctx->out, "\t\t}");

		/* build a complete JSON object for non-transactional message */
		if (!transactional)
		{
			appendStringInfoString(ctx->out, "\n\t]");
			appendStringInfoString(ctx->out, "\n}");
		}
	}
	else
	{
		/* build a complete JSON object for non-transactional message */
		if (!transactional)
		{
			appendStringInfoString(ctx->out, "{");
			appendStringInfoString(ctx->out, "\"change\":[");
		}

		if (data->nr_changes > 1)
			appendStringInfoString(ctx->out, ",{");
		else
			appendStringInfoChar(ctx->out, '{');

		appendStringInfoString(ctx->out, "\"kind\":\"message\",");

		if (transactional)
			appendStringInfoString(ctx->out, "\"transactional\":true,");
		else
			appendStringInfoString(ctx->out, "\"transactional\":false,");

		appendStringInfo(ctx->out, "\"prefix\":\"%s\"", prefix);
		appendStringInfoChar(ctx->out, ',');
		appendStringInfoString(ctx->out, "\"content\":\"");
		appendBinaryStringInfo(ctx->out, content, content_size);
		appendStringInfoChar(ctx->out, '"');
		appendStringInfoChar(ctx->out, '}');

		/* build a complete JSON object for non-transactional message */
		if (!transactional)
		{
			appendStringInfoChar(ctx->out, ']');
			appendStringInfoChar(ctx->out, '}');
		}
	}

	MemoryContextSwitchTo(old);
	MemoryContextReset(data->context);

	if (data->write_in_chunks || !transactional)
		OutputPluginWrite(ctx, true);
}
#endif
