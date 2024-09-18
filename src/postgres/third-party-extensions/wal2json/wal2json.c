/*-------------------------------------------------------------------------
 *
 * wal2json.c
 * 		JSON output plugin for changeset extraction
 *
 * Copyright (c) 2013-2024, Euler Taveira de Oliveira
 *
 * IDENTIFICATION
 *		contrib/wal2json/wal2json.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/genam.h"
#include "access/heapam.h"
#include "access/sysattr.h"
#include "catalog/indexing.h"
#include "catalog/pg_attrdef.h"
#include "catalog/pg_type.h"

#include "replication/logical.h"
#if PG_VERSION_NUM >= 90500
#include "replication/origin.h"
#endif

#include "utils/builtins.h"
#include "utils/fmgroids.h"
#include "utils/guc.h"
#include "utils/json.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"
#include "utils/pg_lsn.h"
#include "utils/rel.h"
#include "utils/syscache.h"

/* YB includes. */
#include "pg_yb_utils.h"

#define WAL2JSON_VERSION				"2.6"
#define WAL2JSON_VERSION_NUM			206

#define	WAL2JSON_FORMAT_VERSION			2
#define	WAL2JSON_FORMAT_MIN_VERSION		1

PG_MODULE_MAGIC;

extern void		_PG_init(void);
extern void	PGDLLEXPORT	_PG_output_plugin_init(OutputPluginCallbacks *cb);

typedef struct
{
	bool	insert;
	bool	update;
	bool	delete;
	bool	truncate;
} JsonAction;

typedef struct
{
	MemoryContext context;
	bool		include_transaction;	/* BEGIN and COMMIT objects (v2) */
	bool		include_xids;		/* include transaction ids */
	bool		include_timestamp;	/* include transaction timestamp */
	bool		include_origin;		/* replication origin */
	bool		include_schemas;	/* qualify tables */
	bool		include_types;		/* include data types */
	bool		include_type_oids;	/* include data type oids */
	bool		include_typmod;		/* include typmod in types */
	bool		include_domain_data_type;	/* include underlying data type of the domain */
	bool		include_column_positions;	/* include column numbers */
	bool		include_not_null;	/* include not-null constraints */
	bool		include_default;	/* include default expressions */
	bool		include_pk;			/* include primary key */

	bool		pretty_print;		/* pretty-print JSON? */
	bool		write_in_chunks;	/* write in chunks? (v1) */
	bool		numeric_data_types_as_string;	/* use strings for numeric data types */

	JsonAction	actions;			/* output only these actions */

	List		*filter_origins;	/* filter out origins */
	List		*filter_tables;		/* filter out tables */
	List		*add_tables;		/* add only these tables */
	List		*filter_msg_prefixes;	/* filter by message prefixes */
	List		*add_msg_prefixes;	/* add only messages with these prefixes */

	int			format_version;		/* support different formats */

	/*
	 * LSN pointing to the end of commit record + 1 (txn->end_lsn)
	 * It is useful for tools that wants a position to restart from.
	 */
	bool		include_lsn;		/* include LSNs */

	uint64		nr_changes;			/* # of passes in pg_decode_change() */
									/* FIXME replace with txn->nentries */

	/* pretty print */
	char		ht[2];				/* horizontal tab, if pretty print */
	char		nl[2];				/* new line, if pretty print */
	char		sp[2];				/* space, if pretty print */
} JsonDecodingData;

typedef enum
{
	PGOUTPUTJSON_CHANGE,
	PGOUTPUTJSON_IDENTITY,
	PGOUTPUTJSON_PK
} PGOutputJsonKind;

typedef struct SelectTable
{
	char	*schemaname;
	char	*tablename;
	bool	allschemas;				/* true means any schema */
	bool	alltables;				/* true means any table */
} SelectTable;

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
#if PG_VERSION_NUM >= 90500
static bool pg_filter_by_origin(LogicalDecodingContext *ctx, RepOriginId origin_id);
#endif
#if PG_VERSION_NUM >= 90600
static void pg_decode_message(LogicalDecodingContext *ctx,
					ReorderBufferTXN *txn, XLogRecPtr lsn,
					bool transactional, const char *prefix,
					Size content_size, const char *content);
#endif
#if PG_VERSION_NUM >= 110000
static void pg_decode_truncate(LogicalDecodingContext *ctx,
					ReorderBufferTXN *txn, int n, Relation relations[],
					ReorderBufferChange *change);
#endif

static void columns_to_stringinfo(LogicalDecodingContext *ctx, TupleDesc tupdesc, HeapTuple tuple, bool addcomma, Relation relation, bool *yb_is_omitted);
static void tuple_to_stringinfo(LogicalDecodingContext *ctx, TupleDesc tupdesc, HeapTuple tuple, Bitmapset *bs, bool replident, bool addcomma, Relation relation, bool *yb_is_omitted);
static void pk_to_stringinfo(LogicalDecodingContext *ctx, TupleDesc tupdesc, HeapTuple tuple, Bitmapset *bs, bool addcomma, Relation relation);
static void identity_to_stringinfo(LogicalDecodingContext *ctx, TupleDesc tupdesc, HeapTuple tuple, Bitmapset *bs, bool *yb_is_omitted, Relation relation);
static bool parse_table_identifier(List *qualified_tables, char separator, List **select_tables);
static bool string_to_SelectTable(char *rawstring, char separator, List **select_tables);
static bool split_string_to_list(char *rawstring, char separator, List **sl);
static bool split_string_to_oid_list(char *rawstring, char separator, List **sl);

static bool pg_filter_by_action(int change_type, JsonAction actions);
static bool pg_filter_by_table(List *filter_tables, char *schemaname, char *tablename);
static bool pg_add_by_table(List *add_tables, char *schemaname, char *tablename);

/* version 1 */
static void pg_decode_begin_txn_v1(LogicalDecodingContext *ctx,
					ReorderBufferTXN *txn);
static void pg_decode_commit_txn_v1(LogicalDecodingContext *ctx,
					 ReorderBufferTXN *txn, XLogRecPtr commit_lsn);
static void pg_decode_change_v1(LogicalDecodingContext *ctx,
				 ReorderBufferTXN *txn, Relation rel,
				 ReorderBufferChange *change);
#if PG_VERSION_NUM >= 90600
static void pg_decode_message_v1(LogicalDecodingContext *ctx,
					ReorderBufferTXN *txn, XLogRecPtr lsn,
					bool transactional, const char *prefix,
					Size content_size, const char *content);
#endif
#if PG_VERSION_NUM >= 110000
static void pg_decode_truncate_v1(LogicalDecodingContext *ctx,
					ReorderBufferTXN *txn, int n, Relation relations[],
					ReorderBufferChange *change);
#endif

/* version 2 */
static void pg_decode_begin_txn_v2(LogicalDecodingContext *ctx,
					ReorderBufferTXN *txn);
static void pg_decode_commit_txn_v2(LogicalDecodingContext *ctx,
					 ReorderBufferTXN *txn, XLogRecPtr commit_lsn);
static void pg_decode_write_value(LogicalDecodingContext *ctx, Datum value, bool isnull, Oid typid, bool yb_unchanged_toasted);
static void pg_decode_write_tuple(LogicalDecodingContext *ctx, Relation relation, HeapTuple tuple, PGOutputJsonKind kind, bool *yb_is_omitted);
static void pg_decode_write_change(LogicalDecodingContext *ctx, ReorderBufferTXN *txn, Relation relation, ReorderBufferChange *change);
static void pg_decode_change_v2(LogicalDecodingContext *ctx,
				 ReorderBufferTXN *txn, Relation rel,
				 ReorderBufferChange *change);
#if PG_VERSION_NUM >= 90600
static void pg_decode_message_v2(LogicalDecodingContext *ctx,
					ReorderBufferTXN *txn, XLogRecPtr lsn,
					bool transactional, const char *prefix,
					Size content_size, const char *content);
#endif
#if PG_VERSION_NUM >= 110000
static void pg_decode_truncate_v2(LogicalDecodingContext *ctx,
					ReorderBufferTXN *txn, int n, Relation relations[],
					ReorderBufferChange *change);
#endif

/*
 * Backward compatibility.
 *
 * This macro is only available in 9.6+.
 */
#if PG_VERSION_NUM < 90600
#ifdef USE_FLOAT8_BYVAL
#define UInt64GetDatum(X) ((Datum) (X))
#else
#define UInt64GetDatum(X) Int64GetDatum((int64) (X))
#endif
#endif

#if PG_VERSION_NUM >= 150000 && PG_VERSION_NUM < 160000
static void update_replication_progress(LogicalDecodingContext *ctx, bool skipped_xact);
#elif PG_VERSION_NUM >= 100000 && PG_VERSION_NUM < 150000
static void update_replication_progress(LogicalDecodingContext *ctx);
#endif

static void
yb_pgoutput_schema_change(LogicalDecodingContext *ctx, Oid relid);

static void
yb_support_yb_specific_replica_identity(bool support_yb_specific_replica_identity);

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
#if PG_VERSION_NUM >= 90500
	cb->filter_by_origin_cb = pg_filter_by_origin;
#endif
#if PG_VERSION_NUM >= 90600
	cb->message_cb = pg_decode_message;
#endif
#if PG_VERSION_NUM >= 110000
	cb->truncate_cb = pg_decode_truncate;
#endif

	if (IsYugaByteEnabled())
	{
		cb->yb_schema_change_cb = yb_pgoutput_schema_change;
		cb->yb_support_yb_specifc_replica_identity_cb = yb_support_yb_specific_replica_identity;
	}
}

/* Initialize this plugin */
static void
pg_decode_startup(LogicalDecodingContext *ctx, OutputPluginOptions *opt, bool is_init)
{
	ListCell	*option;
	JsonDecodingData *data;
	SelectTable	*t;

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
	data->include_transaction = true;
	data->include_xids = false;
	data->include_timestamp = false;
	data->include_pk = false;
	data->include_origin = false;
	data->include_schemas = true;
	data->include_types = true;
	data->include_type_oids = false;
	data->include_typmod = true;
	data->include_domain_data_type = false;
	data->include_column_positions = false;
	data->numeric_data_types_as_string = false;
	data->pretty_print = false;
	data->write_in_chunks = false;
	data->include_lsn = false;
	data->include_not_null = false;
	data->include_default = false;
	data->filter_origins = NIL;
	data->filter_tables = NIL;
	data->filter_msg_prefixes = NIL;
	data->add_msg_prefixes = NIL;

	data->format_version = 1;

	/* default actions */
	if (WAL2JSON_FORMAT_VERSION == 1)
	{
		data->actions.insert = true;
		data->actions.update = true;
		data->actions.delete = true;
		data->actions.truncate = false;		/* backward compatibility */
	}
	else
	{
		data->actions.insert = true;
		data->actions.update = true;
		data->actions.delete = true;
		data->actions.truncate = true;
	}

	/* pretty print */
	data->ht[0] = '\0';
	data->nl[0] = '\0';
	data->sp[0] = '\0';

	/* add all tables in all schemas by default */
	t = palloc0(sizeof(SelectTable));
	t->allschemas = true;
	t->alltables = true;
	data->add_tables = lappend(data->add_tables, t);

	data->nr_changes = 0;

	ctx->output_plugin_private = data;

	opt->output_type = OUTPUT_PLUGIN_TEXTUAL_OUTPUT;

	foreach(option, ctx->output_plugin_options)
	{
		DefElem *elem = lfirst(option);

		Assert(elem->arg == NULL || IsA(elem->arg, String));

		if (strcmp(elem->defname, "include-transaction") == 0)
		{
			/* if option value is NULL then assume that value is true */
			if (elem->arg == NULL)
				data->include_transaction = true;
			else if (!parse_bool(strVal(elem->arg), &data->include_transaction))
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("could not parse value \"%s\" for parameter \"%s\"",
							 strVal(elem->arg), elem->defname)));
		}
		else if (strcmp(elem->defname, "include-xids") == 0)
		{
			/* If option does not provide a value, it means its value is true */
			if (elem->arg == NULL)
			{
				elog(DEBUG1, "include-xids argument is null");
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
				elog(DEBUG1, "include-timestamp argument is null");
				data->include_timestamp = true;
			}
			else if (!parse_bool(strVal(elem->arg), &data->include_timestamp))
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("could not parse value \"%s\" for parameter \"%s\"",
							 strVal(elem->arg), elem->defname)));
		}
		else if (strcmp(elem->defname, "include-pk") == 0)
		{
			if (elem->arg == NULL)
			{
				elog(DEBUG1, "include-pk argument is null");
				data->include_pk = true;
			}
			else if (!parse_bool(strVal(elem->arg), &data->include_pk))
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("could not parse value \"%s\" for parameter \"%s\"",
							 strVal(elem->arg), elem->defname)));
		}
		else if (strcmp(elem->defname, "include-origin") == 0)
		{
			if (elem->arg == NULL)
			{
				elog(DEBUG1, "include-origin argument is null");
				data->include_origin = true;
			}
			else if (!parse_bool(strVal(elem->arg), &data->include_origin))
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("could not parse value \"%s\" for parameter \"%s\"",
							 strVal(elem->arg), elem->defname)));
		}
		else if (strcmp(elem->defname, "include-schemas") == 0)
		{
			if (elem->arg == NULL)
			{
				elog(DEBUG1, "include-schemas argument is null");
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
				elog(DEBUG1, "include-types argument is null");
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
				elog(DEBUG1, "include-type-oids argument is null");
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
				elog(DEBUG1, "include-typmod argument is null");
				data->include_typmod = true;
			}
			else if (!parse_bool(strVal(elem->arg), &data->include_typmod))
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("could not parse value \"%s\" for parameter \"%s\"",
							 strVal(elem->arg), elem->defname)));
		}
		else if (strcmp(elem->defname, "include-domain-data-type") == 0)
		{
			if (elem->arg == NULL)
			{
				elog(DEBUG1, "include-types argument is null");
				data->include_domain_data_type = true;
			}
			else if (!parse_bool(strVal(elem->arg), &data->include_domain_data_type))
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("could not parse value \"%s\" for parameter \"%s\"",
							 strVal(elem->arg), elem->defname)));
		}
		else if (strcmp(elem->defname, "include-column-positions") == 0)
		{
			if (elem->arg == NULL)
			{
				elog(DEBUG1, "include-column-positions argument is null");
				data->include_column_positions = true;
			}
			else if (!parse_bool(strVal(elem->arg), &data->include_column_positions))
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("could not parse value \"%s\" for parameter \"%s\"",
							 strVal(elem->arg), elem->defname)));
		}
		else if (strcmp(elem->defname, "include-not-null") == 0)
		{
			if (elem->arg == NULL)
			{
				elog(DEBUG1, "include-not-null argument is null");
				data->include_not_null = true;
			}
			else if (!parse_bool(strVal(elem->arg), &data->include_not_null))
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("could not parse value \"%s\" for parameter \"%s\"",
							 strVal(elem->arg), elem->defname)));
		}
		else if (strcmp(elem->defname, "include-default") == 0)
		{
			if (elem->arg == NULL)
			{
				elog(DEBUG1, "include-default argument is null");
				data->include_default = true;
			}
			else if (!parse_bool(strVal(elem->arg), &data->include_default))
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("could not parse value \"%s\" for parameter \"%s\"",
							 strVal(elem->arg), elem->defname)));
		}
		else if (strcmp(elem->defname, "numeric-data-types-as-string") == 0)
		{
			if (elem->arg == NULL)
			{
				elog(DEBUG1, "numeric-data-types-as-string argument is null");
				data->numeric_data_types_as_string = true;
			}
			else if (!parse_bool(strVal(elem->arg), &data->numeric_data_types_as_string))
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("could not parse value \"%s\" for parameter \"%s\"",
							 strVal(elem->arg), elem->defname)));
		}
		else if (strcmp(elem->defname, "pretty-print") == 0)
		{
			if (elem->arg == NULL)
			{
				elog(DEBUG1, "pretty-print argument is null");
				data->pretty_print = true;
			}
			else if (!parse_bool(strVal(elem->arg), &data->pretty_print))
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("could not parse value \"%s\" for parameter \"%s\"",
							 strVal(elem->arg), elem->defname)));

			if (data->pretty_print)
			{
				data->ht[0] = '\t';
				data->nl[0] = '\n';
				data->sp[0] = ' ';
			}
		}
		else if (strcmp(elem->defname, "write-in-chunks") == 0)
		{
			if (elem->arg == NULL)
			{
				elog(DEBUG1, "write-in-chunks argument is null");
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
				elog(DEBUG1, "include-lsn argument is null");
				data->include_lsn = true;
			}
			else if (!parse_bool(strVal(elem->arg), &data->include_lsn))
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("could not parse value \"%s\" for parameter \"%s\"",
							 strVal(elem->arg), elem->defname)));
		}
		else if (strcmp(elem->defname, "include-unchanged-toast") == 0)
		{
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_NAME),
					 errmsg("parameter \"%s\" was deprecated", elem->defname)));
		}
		else if (strcmp(elem->defname, "actions") == 0)
		{
			char	*rawstr;

			if (elem->arg == NULL)
			{
				elog(DEBUG1, "actions argument is null");
				/* argument null means default; nothing to do here */
			}
			else
			{
				List		*selected_actions = NIL;
				ListCell	*lc;

				rawstr = pstrdup(strVal(elem->arg));
				if (!split_string_to_list(rawstr, ',', &selected_actions))
				{
					pfree(rawstr);
					ereport(ERROR,
							(errcode(ERRCODE_INVALID_NAME),
							 errmsg("could not parse value \"%s\" for parameter \"%s\"",
								 strVal(elem->arg), elem->defname)));
				}

				data->actions.insert = false;
				data->actions.update = false;
				data->actions.delete = false;
				data->actions.truncate = false;

				foreach(lc, selected_actions)
				{
					char *p = lfirst(lc);

					if (strcmp(p, "insert") == 0)
						data->actions.insert = true;
					else if (strcmp(p, "update") == 0)
						data->actions.update = true;
					else if (strcmp(p, "delete") == 0)
						data->actions.delete = true;
					else if (strcmp(p, "truncate") == 0)
						data->actions.truncate = true;
					else
						ereport(ERROR,
								(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
								 errmsg("could not parse value \"%s\" for parameter \"%s\"",
									 p, elem->defname)));
				}

				pfree(rawstr);
				list_free(selected_actions);
			}
		}
		else if (strcmp(elem->defname, "filter-origins") == 0)
		{
			char	*rawstr;

			if (elem->arg == NULL)
			{
				elog(DEBUG1, "filter-origins argument is null");
				data->filter_origins = NIL;
			}
			else
			{
				rawstr = pstrdup(strVal(elem->arg));
				if (!split_string_to_oid_list(rawstr, ',', &data->filter_origins))
				{
					pfree(rawstr);
					ereport(ERROR,
							(errcode(ERRCODE_INVALID_NAME),
							 errmsg("could not parse value \"%s\" for parameter \"%s\"",
								 strVal(elem->arg), elem->defname)));
				}
				pfree(rawstr);
			}
		}
		else if (strcmp(elem->defname, "filter-tables") == 0)
		{
			char	*rawstr;

			if (elem->arg == NULL)
			{
				elog(DEBUG1, "filter-tables argument is null");
				data->filter_tables = NIL;
			}
			else
			{
				rawstr = pstrdup(strVal(elem->arg));
				if (!string_to_SelectTable(rawstr, ',', &data->filter_tables))
				{
					pfree(rawstr);
					ereport(ERROR,
							(errcode(ERRCODE_INVALID_NAME),
							 errmsg("could not parse value \"%s\" for parameter \"%s\"",
								 strVal(elem->arg), elem->defname)));
				}
				pfree(rawstr);
			}
		}
		else if (strcmp(elem->defname, "add-tables") == 0)
		{
			char	*rawstr;

			/*
			 * If this parameter is specified, remove 'all tables in all
			 * schemas' value from list.
			 */
			list_free_deep(data->add_tables);
			data->add_tables = NIL;

			if (elem->arg == NULL)
			{
				elog(DEBUG1, "add-tables argument is null");
				data->add_tables = NIL;
			}
			else
			{
				rawstr = pstrdup(strVal(elem->arg));
				if (!string_to_SelectTable(rawstr, ',', &data->add_tables))
				{
					pfree(rawstr);
					ereport(ERROR,
							(errcode(ERRCODE_INVALID_NAME),
							 errmsg("could not parse value \"%s\" for parameter \"%s\"",
								 strVal(elem->arg), elem->defname)));
				}
				pfree(rawstr);
			}
		}
		else if (strcmp(elem->defname, "filter-msg-prefixes") == 0)
		{
			char	*rawstr;

			if (elem->arg == NULL)
			{
				elog(DEBUG1, "filter-msg-prefixes argument is null");
				data->filter_msg_prefixes = NIL;
			}
			else
			{
				rawstr = pstrdup(strVal(elem->arg));
				if (!split_string_to_list(rawstr, ',', &data->filter_msg_prefixes))
				{
					pfree(rawstr);
					ereport(ERROR,
							(errcode(ERRCODE_INVALID_NAME),
							 errmsg("could not parse value \"%s\" for parameter \"%s\"",
								 strVal(elem->arg), elem->defname)));
				}
				pfree(rawstr);
			}
		}
		else if (strcmp(elem->defname, "add-msg-prefixes") == 0)
		{
			char	*rawstr;

			if (elem->arg == NULL)
			{
				elog(DEBUG1, "add-msg-prefixes argument is null");
				data->add_msg_prefixes = NIL;
			}
			else
			{
				rawstr = pstrdup(strVal(elem->arg));
				if (!split_string_to_list(rawstr, ',', &data->add_msg_prefixes))
				{
					pfree(rawstr);
					ereport(ERROR,
							(errcode(ERRCODE_INVALID_NAME),
							 errmsg("could not parse value \"%s\" for parameter \"%s\"",
								 strVal(elem->arg), elem->defname)));
				}
				pfree(rawstr);
			}
		}
		else if (strcmp(elem->defname, "format-version") == 0)
		{
			if (elem->arg == NULL)
			{
				elog(DEBUG1, "format-version argument is null");
				data->format_version = WAL2JSON_FORMAT_VERSION;
			}
			else if (!parse_int(strVal(elem->arg), &data->format_version, 0, NULL))
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("could not parse value \"%s\" for parameter \"%s\"",
							 strVal(elem->arg), elem->defname)));

			if (data->format_version > WAL2JSON_FORMAT_VERSION)
				ereport(ERROR,
						(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
						 errmsg("client sent format_version=%d but we only support format %d or lower",
						 data->format_version, WAL2JSON_FORMAT_VERSION)));

			if (data->format_version < WAL2JSON_FORMAT_MIN_VERSION)
				ereport(ERROR,
						(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
						 errmsg("client sent format_version=%d but we only support format %d or higher",
						 data->format_version, WAL2JSON_FORMAT_MIN_VERSION)));
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

	elog(DEBUG2, "format version: %d", data->format_version);
}

/* cleanup this plugin's resources */
static void
pg_decode_shutdown(LogicalDecodingContext *ctx)
{
	JsonDecodingData *data = ctx->output_plugin_private;

	/* cleanup our own resources via memory context reset */
	MemoryContextDelete(data->context);
}

#if PG_VERSION_NUM >= 90500
static bool
pg_filter_by_origin(LogicalDecodingContext *ctx, RepOriginId origin_id)
{
	JsonDecodingData *data = ctx->output_plugin_private;

	elog(DEBUG3, "origin: %u", origin_id);

	/* changes produced locally are never filtered */
	if (origin_id == InvalidRepOriginId)
		return false;

	/* Filter origins, if available */
	if (list_length(data->filter_origins) > 0 && list_member_oid(data->filter_origins, origin_id))
	{
		elog(DEBUG2, "origin \"%u\" was filtered out", origin_id);
		return true;
	}

	/*
	 * There isn't a list of origins to filter or origin is not contained in
	 * the filter list hence forward to all subscribers.
	 */
	return false;
}
#endif

/* BEGIN callback */
static void
pg_decode_begin_txn(LogicalDecodingContext *ctx, ReorderBufferTXN *txn)
{
	JsonDecodingData *data = ctx->output_plugin_private;

	if (data->format_version == 2)
		pg_decode_begin_txn_v2(ctx, txn);
	else if (data->format_version == 1)
		pg_decode_begin_txn_v1(ctx, txn);
	else
		elog(ERROR, "format version %d is not supported", data->format_version);
}

static void
pg_decode_begin_txn_v1(LogicalDecodingContext *ctx, ReorderBufferTXN *txn)
{
	JsonDecodingData *data = ctx->output_plugin_private;

	data->nr_changes = 0;

	/* Transaction starts */
	OutputPluginPrepareWrite(ctx, true);

	appendStringInfo(ctx->out, "{%s", data->nl);

	if (data->include_xids)
		appendStringInfo(ctx->out, "%s\"xid\":%s%u,%s", data->ht, data->sp, txn->xid, data->nl);

	if (data->include_lsn)
	{
		char *lsn_str = DatumGetCString(DirectFunctionCall1(pg_lsn_out, UInt64GetDatum(txn->end_lsn)));

		appendStringInfo(ctx->out, "%s\"nextlsn\":%s\"%s\",%s", data->ht, data->sp, lsn_str, data->nl);

		pfree(lsn_str);
	}

#if PG_VERSION_NUM >= 150000
	if (data->include_timestamp)
		appendStringInfo(ctx->out, "%s\"timestamp\":%s\"%s\",%s", data->ht, data->sp, timestamptz_to_str(txn->xact_time.commit_time), data->nl);
#else
	if (data->include_timestamp)
		appendStringInfo(ctx->out, "%s\"timestamp\":%s\"%s\",%s", data->ht, data->sp, timestamptz_to_str(txn->commit_time), data->nl);
#endif

#if PG_VERSION_NUM >= 90500
	if (data->include_origin)
		appendStringInfo(ctx->out, "%s\"origin\":%s%u,%s", data->ht, data->sp, txn->origin_id, data->nl);
#endif

	appendStringInfo(ctx->out, "%s\"change\":%s[", data->ht, data->sp);

	if (data->write_in_chunks)
		OutputPluginWrite(ctx, true);
}

static void
pg_decode_begin_txn_v2(LogicalDecodingContext *ctx, ReorderBufferTXN *txn)
{
	JsonDecodingData *data = ctx->output_plugin_private;

	/* don't include BEGIN object */
	if (!data->include_transaction)
		return;

	OutputPluginPrepareWrite(ctx, true);
	appendStringInfoString(ctx->out, "{\"action\":\"B\"");
	if (data->include_xids)
		appendStringInfo(ctx->out, ",\"xid\":%u", txn->xid);

#if PG_VERSION_NUM >= 150000
	if (data->include_timestamp)
			appendStringInfo(ctx->out, ",\"timestamp\":\"%s\"", timestamptz_to_str(txn->xact_time.commit_time));
#else
	if (data->include_timestamp)
			appendStringInfo(ctx->out, ",\"timestamp\":\"%s\"", timestamptz_to_str(txn->commit_time));
#endif

#if PG_VERSION_NUM >= 90500
	if (data->include_origin)
		appendStringInfo(ctx->out, ",\"origin\":%u", txn->origin_id);
#endif

	if (data->include_lsn)
	{
		char *lsn_str = DatumGetCString(DirectFunctionCall1(pg_lsn_out, UInt64GetDatum(txn->final_lsn)));
		appendStringInfo(ctx->out, ",\"lsn\":\"%s\"", lsn_str);
		pfree(lsn_str);

		lsn_str = DatumGetCString(DirectFunctionCall1(pg_lsn_out, UInt64GetDatum(txn->end_lsn)));
		appendStringInfo(ctx->out, ",\"nextlsn\":\"%s\"", lsn_str);
		pfree(lsn_str);
	}

	appendStringInfoChar(ctx->out, '}');
	OutputPluginWrite(ctx, true);
}

/* COMMIT callback */
static void
pg_decode_commit_txn(LogicalDecodingContext *ctx, ReorderBufferTXN *txn,
					 XLogRecPtr commit_lsn)
{
	JsonDecodingData *data = ctx->output_plugin_private;

	/*
	 * Some older minor versions from back branches (10 to 14) calls
	 * OutputPluginUpdateProgress(). That's before the fix
	 * f95d53eded55ecbf037f6416ced6af29a2c3caca. After that,
	 * update_replication_progress() function is used for back branches. In
	 * version 15, update_replication_progress() changes the signature to
	 * support skipped transactions. In version 16,
	 * OutputPluginUpdateProgress() is back because a proper fix was added into
	 * logical decoding.
	 */
#if PG_VERSION_NUM >= 160000
	OutputPluginUpdateProgress(ctx, false);		/* XXX change 2nd param when skipped empty transaction is supported */
#elif PG_VERSION_NUM >= 150000 && PG_VERSION_NUM < 160000
	update_replication_progress(ctx, false);	/* XXX change 2nd param when skipped empty transaction is supported */
#elif PG_VERSION_NUM >= 140004 && PG_VERSION_NUM < 150000
	update_replication_progress(ctx);
#elif PG_VERSION_NUM >= 130008 && PG_VERSION_NUM < 140000
	update_replication_progress(ctx);
#elif PG_VERSION_NUM >= 120012 && PG_VERSION_NUM < 130000
	update_replication_progress(ctx);
#elif PG_VERSION_NUM >= 110017 && PG_VERSION_NUM < 120000
	update_replication_progress(ctx);
#elif PG_VERSION_NUM >= 100022 && PG_VERSION_NUM < 110000
	update_replication_progress(ctx);
#elif PG_VERSION_NUM >= 140000 && PG_VERSION_NUM < 140004
	OutputPluginUpdateProgress(ctx);
#elif PG_VERSION_NUM >= 130000 && PG_VERSION_NUM < 130008
	OutputPluginUpdateProgress(ctx);
#elif PG_VERSION_NUM >= 120000 && PG_VERSION_NUM < 120012
	OutputPluginUpdateProgress(ctx);
#elif PG_VERSION_NUM >= 110000 && PG_VERSION_NUM < 110017
	OutputPluginUpdateProgress(ctx);
#elif PG_VERSION_NUM >= 100000 && PG_VERSION_NUM < 100022
	OutputPluginUpdateProgress(ctx);
#endif

	elog(DEBUG2, "my change counter: " UINT64_FORMAT " ; # of changes: " UINT64_FORMAT " ; # of changes in memory: " UINT64_FORMAT, data->nr_changes, txn->nentries, txn->nentries_mem);
	elog(DEBUG2, "# of subxacts: %d", txn->nsubtxns);

	if (data->format_version == 2)
		pg_decode_commit_txn_v2(ctx, txn, commit_lsn);
	else if (data->format_version == 1)
		pg_decode_commit_txn_v1(ctx, txn, commit_lsn);
	else
		elog(ERROR, "format version %d is not supported", data->format_version);
}

static void
pg_decode_commit_txn_v1(LogicalDecodingContext *ctx, ReorderBufferTXN *txn,
					 XLogRecPtr commit_lsn)
{
	JsonDecodingData *data = ctx->output_plugin_private;

	/* Transaction ends */
	if (data->write_in_chunks)
		OutputPluginPrepareWrite(ctx, true);

	/* if we don't write in chunks, we need a newline here */
	if (!data->write_in_chunks)
		appendStringInfo(ctx->out, "%s", data->nl);

	appendStringInfo(ctx->out, "%s]%s}", data->ht, data->nl);

	OutputPluginWrite(ctx, true);
}

static void
pg_decode_commit_txn_v2(LogicalDecodingContext *ctx, ReorderBufferTXN *txn,
					 XLogRecPtr commit_lsn)
{
	JsonDecodingData *data = ctx->output_plugin_private;

	/* don't include COMMIT object */
	if (!data->include_transaction)
		return;

	OutputPluginPrepareWrite(ctx, true);
	appendStringInfoString(ctx->out, "{\"action\":\"C\"");
	if (data->include_xids)
		appendStringInfo(ctx->out, ",\"xid\":%u", txn->xid);

#if PG_VERSION_NUM >= 150000
	if (data->include_timestamp)
			appendStringInfo(ctx->out, ",\"timestamp\":\"%s\"", timestamptz_to_str(txn->xact_time.commit_time));
#else
	if (data->include_timestamp)
			appendStringInfo(ctx->out, ",\"timestamp\":\"%s\"", timestamptz_to_str(txn->commit_time));
#endif

#if PG_VERSION_NUM >= 90500
	if (data->include_origin)
		appendStringInfo(ctx->out, ",\"origin\":%u", txn->origin_id);
#endif

	if (data->include_lsn)
	{
		char *lsn_str = DatumGetCString(DirectFunctionCall1(pg_lsn_out, UInt64GetDatum(commit_lsn)));
		appendStringInfo(ctx->out, ",\"lsn\":\"%s\"", lsn_str);
		pfree(lsn_str);

		lsn_str = DatumGetCString(DirectFunctionCall1(pg_lsn_out, UInt64GetDatum(txn->end_lsn)));
		appendStringInfo(ctx->out, ",\"nextlsn\":\"%s\"", lsn_str);
		pfree(lsn_str);
	}

	appendStringInfoChar(ctx->out, '}');
	OutputPluginWrite(ctx, true);
}

/*
 * Accumulate tuple information and stores it at the end
 *
 * replident: is this tuple a replica identity?
 */
static void
tuple_to_stringinfo(LogicalDecodingContext *ctx, TupleDesc tupdesc, HeapTuple tuple, Bitmapset *bs, bool replident, bool addcomma, Relation relation, bool *yb_is_omitted)
{
	JsonDecodingData	*data;
	int					natt;

	StringInfoData		colnames;
	StringInfoData		coltypes;
	StringInfoData		coltypeoids;
	StringInfoData		colpositions;
	StringInfoData		colnotnulls;
	StringInfoData		coldefaults;
	StringInfoData		colvalues;
	char				comma[3] = "";

	Relation			defrel = NULL;

	data = ctx->output_plugin_private;

	initStringInfo(&colnames);
	initStringInfo(&coltypes);
	if (data->include_type_oids)
		initStringInfo(&coltypeoids);
	if (data->include_column_positions)
		initStringInfo(&colpositions);
	if (data->include_not_null)
		initStringInfo(&colnotnulls);
	if (data->include_default)
		initStringInfo(&coldefaults);
	initStringInfo(&colvalues);

	/*
	 * If replident is true, it will output info about replica identity. In this
	 * case, there are special JSON objects for it. Otherwise, it will print new
	 * tuple data.
	 */
	if (replident)
	{
		appendStringInfo(&colnames, "%s%s%s\"oldkeys\":%s{%s", data->ht, data->ht, data->ht, data->sp, data->nl);
		appendStringInfo(&colnames, "%s%s%s%s\"keynames\":%s[", data->ht, data->ht, data->ht, data->ht, data->sp);
		appendStringInfo(&coltypes, "%s%s%s%s\"keytypes\":%s[", data->ht, data->ht, data->ht, data->ht, data->sp);
		if (data->include_type_oids)
			appendStringInfo(&coltypeoids, "%s%s%s%s\"keytypeoids\":%s[", data->ht, data->ht, data->ht, data->ht, data->sp);
		appendStringInfo(&colvalues, "%s%s%s%s\"keyvalues\":%s[", data->ht, data->ht, data->ht, data->ht, data->sp);
	}
	else
	{
		appendStringInfo(&colnames, "%s%s%s\"columnnames\":%s[", data->ht, data->ht, data->ht, data->sp);
		appendStringInfo(&coltypes, "%s%s%s\"columntypes\":%s[", data->ht, data->ht, data->ht, data->sp);
		if (data->include_type_oids)
			appendStringInfo(&coltypeoids, "%s%s%s\"columntypeoids\":%s[", data->ht, data->ht, data->ht, data->sp);
		if (data->include_column_positions)
			appendStringInfo(&colpositions, "%s%s%s\"columnpositions\":%s[", data->ht, data->ht, data->ht, data->sp);
		if (data->include_not_null)
			appendStringInfo(&colnotnulls, "%s%s%s\"columnoptionals\":%s[", data->ht, data->ht, data->ht, data->sp);
		if (data->include_default)
			appendStringInfo(&coldefaults, "%s%s%s\"columndefaults\":%s[", data->ht, data->ht, data->ht, data->sp);
		appendStringInfo(&colvalues, "%s%s%s\"columnvalues\":%s[", data->ht, data->ht, data->ht, data->sp);
	}

	if (!replident && data->include_default)
	{
#if PG_VERSION_NUM >= 120000
		defrel = table_open(AttrDefaultRelationId, AccessShareLock);
#else
		defrel = heap_open(AttrDefaultRelationId, AccessShareLock);
#endif
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
		bool				yb_send_unchanged_toasted = false;

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

		/* Replica identity column? */
		if (bs != NULL && !bms_is_member(attr->attnum - YBGetFirstLowInvalidAttributeNumber(relation), bs))
			continue;

		if (IsYugaByteEnabled())
			yb_send_unchanged_toasted = yb_is_omitted && yb_is_omitted[natt];

		/* Get Datum from tuple */
		origval = heap_getattr(tuple, natt + 1, tupdesc, &isnull);

		/* Skip nulls iif printing key/identity */
		if (isnull && replident)
			continue;

		typid = attr->atttypid;

		/* Figure out type name */
		type_tuple = SearchSysCache1(TYPEOID, ObjectIdGetDatum(typid));
		if (!HeapTupleIsValid(type_tuple))
			elog(ERROR, "cache lookup failed for type %u", typid);

		/* Get information needed for printing values of a type */
		getTypeOutputInfo(typid, &typoutput, &typisvarlena);

		/* XXX Unchanged TOAST Datum does not need to be output */
		if (yb_send_unchanged_toasted ||
		   (!isnull && typisvarlena && VARATT_IS_EXTERNAL_ONDISK(origval))) 
		{
			elog(DEBUG1, "column \"%s\" has an unchanged TOAST", NameStr(attr->attname));
			continue;
		}

		/* Accumulate each column info */
		appendStringInfo(&colnames, "%s", comma);
		escape_json(&colnames, NameStr(attr->attname));

		if (data->include_types)
		{
			char	*type_str;
			int		len;
			Form_pg_type type_form = (Form_pg_type) GETSTRUCT(type_tuple);

			/*
			 * It is a domain. Replace domain name with base data type if
			 * include_domain_data_type is enabled.
			 */
			if (type_form->typtype == TYPTYPE_DOMAIN && data->include_domain_data_type)
			{
				typid = type_form->typbasetype;
				if (data->include_typmod)
				{
					getTypeOutputInfo(typid, &typoutput, &typisvarlena);
					type_str = format_type_with_typemod(type_form->typbasetype, type_form->typtypmod);
				}
				else
				{
					/*
					 * Since we are not using a format function, grab base type
					 * name from Form_pg_type.
					 */
					type_tuple = SearchSysCache1(TYPEOID, ObjectIdGetDatum(typid));
					if (!HeapTupleIsValid(type_tuple))
						elog(ERROR, "cache lookup failed for type %u", typid);
					type_form = (Form_pg_type) GETSTRUCT(type_tuple);
					type_str = pstrdup(NameStr(type_form->typname));
				}
			}
			else
			{
				if (data->include_typmod)
					type_str = TextDatumGetCString(DirectFunctionCall2(format_type, attr->atttypid, attr->atttypmod));
				else
					type_str = pstrdup(NameStr(type_form->typname));
			}

			appendStringInfo(&coltypes, "%s", comma);
			/*
			 * format_type() returns a quoted identifier, if
			 * required. In this case, it doesn't need to enclose the type name
			 * in double quotes. However, if it is an array type, it should
			 * escape it because the brackets are outside the double quotes.
			 */
			len = strlen(type_str);
			if (type_str[0] == '"' && type_str[len - 1] != ']')
				appendStringInfo(&coltypes, "%s", type_str);
			else
				escape_json(&coltypes, type_str);

			pfree(type_str);

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

		if (!replident && data->include_column_positions)
			appendStringInfo(&colpositions, "%s%d", comma, attr->attnum);

		/*
		 * Print default for columns.
		 */
		if (!replident && data->include_default)
		{
#if PG_VERSION_NUM >= 120000
			if (attr->atthasdef && attr->attgenerated == '\0')
#else
			if (attr->atthasdef)
#endif
			{
				ScanKeyData			scankeys[2];
				SysScanDesc			scan;
				HeapTuple			def_tuple;
				Datum				def_value;
				bool				attisnull;
				char				*result;

				ScanKeyInit(&scankeys[0],
							Anum_pg_attrdef_adrelid,
							BTEqualStrategyNumber, F_OIDEQ,
							ObjectIdGetDatum(relation->rd_id));
				ScanKeyInit(&scankeys[1],
							Anum_pg_attrdef_adnum,
							BTEqualStrategyNumber, F_INT2EQ,
							Int16GetDatum(attr->attnum));

				scan = systable_beginscan(defrel, AttrDefaultIndexId, true,
											NULL, 2, scankeys);

				def_tuple = systable_getnext(scan);
				if (HeapTupleIsValid(def_tuple))
				{
					def_value = fastgetattr(def_tuple, Anum_pg_attrdef_adbin, defrel->rd_att, &attisnull);

					if (!attisnull)
					{
						result = TextDatumGetCString(DirectFunctionCall2(pg_get_expr,
																	def_value,
																	ObjectIdGetDatum(relation->rd_id)));

						appendStringInfo(&coldefaults, "%s\"%s\"", comma, result);
						pfree(result);
					}
					else
					{
						/*
						 * null means that default was not set. Is it possible?
						 * atthasdef shouldn't be set.
						 */
						appendStringInfo(&coldefaults, "%snull", comma);
					}
				}

				systable_endscan(scan);
			}
			else
			{
				/*
				 * no DEFAULT clause implicitly means that the default is NULL
				 */
				appendStringInfo(&coldefaults, "%snull", comma);
			}
		}

		if (isnull && !yb_send_unchanged_toasted)
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
			 *
			 * Exception to this is when data->numeric_data_types_as_string is
			 * true. In this case, numbers (including NaN and Infinity values)
			 * are printed with quotes.
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
					if (data->numeric_data_types_as_string) {
						if (strspn(outputstr, "0123456789+-eE.") == strlen(outputstr) ||
								pg_strncasecmp(outputstr, "NaN", 3) == 0 ||
								pg_strncasecmp(outputstr, "Infinity", 8) == 0 ||
								pg_strncasecmp(outputstr, "-Infinity", 9) == 0) {
							appendStringInfo(&colvalues, "%s", comma);
							escape_json(&colvalues, outputstr);
						} else {
							elog(ERROR, "%s is not a number", outputstr);
						}
					}
					else if (pg_strncasecmp(outputstr, "NaN", 3) == 0 ||
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
				case BYTEAOID:
					appendStringInfo(&colvalues, "%s", comma);
					/* string is "\x54617069727573", start after "\x" */
					escape_json(&colvalues, (outputstr + 2));
					break;
				default:
					appendStringInfo(&colvalues, "%s", comma);
					escape_json(&colvalues, outputstr);
					break;
			}
		}

		/* The first column does not have comma */
		if (strcmp(comma, "") == 0)
			snprintf(comma, 3, ",%s", data->sp);
	}

	if (!replident && data->include_default)
	{
#if PG_VERSION_NUM >= 120000
		table_close(defrel, AccessShareLock);
#else
		heap_close(defrel, AccessShareLock);
#endif
	}

	/* Column info ends */
	if (replident)
	{
		appendStringInfo(&colnames, "],%s", data->nl);
		if (data->include_types)
			appendStringInfo(&coltypes, "],%s", data->nl);
		if (data->include_type_oids)
			appendStringInfo(&coltypeoids, "],%s", data->nl);
		appendStringInfo(&colvalues, "]%s", data->nl);
		appendStringInfo(&colvalues, "%s%s%s}%s", data->ht, data->ht, data->ht, data->nl);
	}
	else
	{
		appendStringInfo(&colnames, "],%s", data->nl);
		if (data->include_types)
			appendStringInfo(&coltypes, "],%s", data->nl);
		if (data->include_type_oids)
			appendStringInfo(&coltypeoids, "],%s", data->nl);
		if (data->include_column_positions)
			appendStringInfo(&colpositions, "],%s", data->nl);
		if (data->include_not_null)
			appendStringInfo(&colnotnulls, "],%s", data->nl);
		if (data->include_default)
			appendStringInfo(&coldefaults, "],%s", data->nl);
		if (addcomma)
			appendStringInfo(&colvalues, "],%s", data->nl);
		else
			appendStringInfo(&colvalues, "]%s", data->nl);
	}

	/* Print data */
	appendStringInfoString(ctx->out, colnames.data);
	if (data->include_types)
		appendStringInfoString(ctx->out, coltypes.data);
	if (data->include_type_oids)
		appendStringInfoString(ctx->out, coltypeoids.data);
	if (data->include_column_positions)
		appendStringInfoString(ctx->out, colpositions.data);
	if (data->include_not_null)
		appendStringInfoString(ctx->out, colnotnulls.data);
	if (data->include_default)
		appendStringInfoString(ctx->out, coldefaults.data);
	appendStringInfoString(ctx->out, colvalues.data);

	pfree(colnames.data);
	pfree(coltypes.data);
	if (data->include_type_oids)
		pfree(coltypeoids.data);
	if (data->include_column_positions)
		pfree(colpositions.data);
	if (data->include_not_null)
		pfree(colnotnulls.data);
	if (data->include_default)
		pfree(coldefaults.data);
	pfree(colvalues.data);
}

/* Print columns information */
static void
columns_to_stringinfo(LogicalDecodingContext *ctx, TupleDesc tupdesc, HeapTuple tuple, bool addcomma, Relation relation, bool *yb_is_omitted)
{
	tuple_to_stringinfo(ctx, tupdesc, tuple, NULL, false, addcomma, relation, yb_is_omitted);
}

/* Print replica identity information */
static void
identity_to_stringinfo(LogicalDecodingContext *ctx, TupleDesc tupdesc, HeapTuple tuple, Bitmapset *bs, bool *yb_is_omitted, Relation relation)
{
	/* Last parameter does not matter */
	tuple_to_stringinfo(ctx, tupdesc, tuple, bs, true, false, relation, yb_is_omitted);
}

/* Print primary key information */
static void
pk_to_stringinfo(LogicalDecodingContext *ctx, TupleDesc tupdesc, HeapTuple tuple, Bitmapset *bs, bool addcomma, Relation relation)
{
	JsonDecodingData	*data;
	int					natt;
	char				comma[3] = "";

	StringInfoData		pknames;
	StringInfoData		pktypes;

	data = ctx->output_plugin_private;

	initStringInfo(&pknames);
	initStringInfo(&pktypes);

	appendStringInfo(&pknames, "%s%s%s\"pk\":%s{%s", data->ht, data->ht, data->ht, data->sp, data->nl);
	appendStringInfo(&pknames, "%s%s%s%s\"pknames\":%s[", data->ht, data->ht, data->ht, data->ht, data->sp);
	appendStringInfo(&pktypes, "%s%s%s%s\"pktypes\":%s[", data->ht, data->ht, data->ht, data->ht, data->sp);

	/* Print column information (name, type, value) */
	for (natt = 0; natt < tupdesc->natts; natt++)
	{
		Form_pg_attribute	attr;		/* the attribute itself */
		Oid					typid;		/* type of current attribute */
		HeapTuple			type_tuple;	/* information about a type */

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

		/* Do not print dropped or system columns */
		if (attr->attisdropped || attr->attnum < 0)
			continue;

		/* Primary key column? */
		if (bs != NULL && !bms_is_member(attr->attnum - YBGetFirstLowInvalidAttributeNumber(relation), bs))
			continue;

		typid = attr->atttypid;

		/* Figure out type name */
		type_tuple = SearchSysCache1(TYPEOID, ObjectIdGetDatum(typid));
		if (!HeapTupleIsValid(type_tuple))
			elog(ERROR, "cache lookup failed for type %u", typid);

		/* Accumulate each column info */
		appendStringInfo(&pknames, "%s", comma);
		escape_json(&pknames, NameStr(attr->attname));

		if (data->include_types)
		{
			char	*type_str;
			Form_pg_type type_form = (Form_pg_type) GETSTRUCT(type_tuple);

			/*
			 * It is a domain. Replace domain name with base data type if
			 * include_domain_data_type is enabled.
			 */
			if (type_form->typtype == TYPTYPE_DOMAIN && data->include_domain_data_type)
			{
				typid = type_form->typbasetype;
				if (data->include_typmod)
				{
					type_str = format_type_with_typemod(type_form->typbasetype, type_form->typtypmod);
				}
				else
				{
					/*
					 * Since we are not using a format function, grab base type
					 * name from Form_pg_type.
					 */
					type_tuple = SearchSysCache1(TYPEOID, ObjectIdGetDatum(typid));
					if (!HeapTupleIsValid(type_tuple))
						elog(ERROR, "cache lookup failed for type %u", typid);
					type_form = (Form_pg_type) GETSTRUCT(type_tuple);
					type_str = pstrdup(NameStr(type_form->typname));
				}
			}
			else
			{
				if (data->include_typmod)
					type_str = TextDatumGetCString(DirectFunctionCall2(format_type, attr->atttypid, attr->atttypmod));
				else
					type_str = pstrdup(NameStr(type_form->typname));
			}

			appendStringInfo(&pktypes, "%s", comma);
			/*
			 * format_type() returns a quoted identifier, if
			 * required. In this case, it doesn't need to enclose the type name
			 * in double quotes.
			 */
			if (type_str[0] == '"')
				appendStringInfo(&pktypes, "%s", type_str);
			else
				escape_json(&pktypes, type_str);

			pfree(type_str);
		}

		ReleaseSysCache(type_tuple);

		/* The first column does not have comma */
		if (strcmp(comma, "") == 0)
			snprintf(comma, 3, ",%s", data->sp);
	}

	appendStringInfo(&pknames, "],%s", data->nl);
	appendStringInfo(&pktypes, "]%s", data->nl);
	if (addcomma)
		appendStringInfo(&pktypes, "%s%s%s},%s", data->ht, data->ht, data->ht, data->nl);
	else
		appendStringInfo(&pktypes, "%s%s%s}%s", data->ht, data->ht, data->ht, data->nl);

	appendStringInfoString(ctx->out, pknames.data);
	appendStringInfoString(ctx->out, pktypes.data);

	pfree(pknames.data);
	pfree(pktypes.data);
}

static bool
pg_filter_by_action(int change_type, JsonAction actions)
{
	if (change_type == REORDER_BUFFER_CHANGE_INSERT && !actions.insert)
	{
		elog(DEBUG3, "ignore INSERT");
		return true;
	}
	if (change_type == REORDER_BUFFER_CHANGE_UPDATE && !actions.update)
	{
		elog(DEBUG3, "ignore UPDATE");
		return true;
	}
	if (change_type == REORDER_BUFFER_CHANGE_DELETE && !actions.delete)
	{
		elog(DEBUG3, "ignore DELETE");
		return true;
	}

	return false;
}

static bool
pg_filter_by_table(List *filter_tables, char *schemaname, char *tablename)
{
	if (list_length(filter_tables) > 0)
	{
		ListCell	*lc;

		foreach(lc, filter_tables)
		{
			SelectTable	*t = lfirst(lc);

			if (t->allschemas || strcmp(t->schemaname, schemaname) == 0)
			{
				if (t->alltables || strcmp(t->tablename, tablename) == 0)
				{
					elog(DEBUG2, "\"%s\".\"%s\" was filtered out",
								((t->allschemas) ? "*" : t->schemaname),
								((t->alltables) ? "*" : t->tablename));
					return true;
				}
			}
		}
	}

	return false;
}

static bool
pg_add_by_table(List *add_tables, char *schemaname, char *tablename)
{
	if (list_length(add_tables) > 0)
	{
		ListCell	*lc;

		/* all tables in all schemas are added by default */
		foreach(lc, add_tables)
		{
			SelectTable	*t = lfirst(lc);

			if (t->allschemas || strcmp(t->schemaname, schemaname) == 0)
			{
				if (t->alltables || strcmp(t->tablename, tablename) == 0)
				{
					elog(DEBUG2, "\"%s\".\"%s\" was added",
								((t->allschemas) ? "*" : t->schemaname),
								((t->alltables) ? "*" : t->tablename));
					return true;
				}
			}
		}
	}

	return false;
}

/* Callback for individual changed tuples */
static void
pg_decode_change(LogicalDecodingContext *ctx, ReorderBufferTXN *txn,
				 Relation relation, ReorderBufferChange *change)
{
	JsonDecodingData *data = ctx->output_plugin_private;

#if PG_VERSION_NUM >= 150000 && PG_VERSION_NUM < 160000
	update_replication_progress(ctx, false);
#elif PG_VERSION_NUM >= 140004 && PG_VERSION_NUM < 150000
	update_replication_progress(ctx);
#elif PG_VERSION_NUM >= 130008 && PG_VERSION_NUM < 140000
	update_replication_progress(ctx);
#elif PG_VERSION_NUM >= 120012 && PG_VERSION_NUM < 130000
	update_replication_progress(ctx);
#elif PG_VERSION_NUM >= 110017 && PG_VERSION_NUM < 120000
	update_replication_progress(ctx);
#elif PG_VERSION_NUM >= 100022 && PG_VERSION_NUM < 110000
	update_replication_progress(ctx);
#endif

	if (data->format_version == 2)
		pg_decode_change_v2(ctx, txn, relation, change);
	else if (data->format_version == 1)
		pg_decode_change_v1(ctx, txn, relation, change);
	else
		elog(ERROR, "format version %d is not supported", data->format_version);
}

static void
pg_decode_change_v1(LogicalDecodingContext *ctx, ReorderBufferTXN *txn,
				 Relation relation, ReorderBufferChange *change)
{
	JsonDecodingData *data;
	Form_pg_class class_form;
	TupleDesc	tupdesc;
	MemoryContext old;

	Bitmapset	*pkbs = NULL;
	Bitmapset	*ribs = NULL;

	char		*schemaname;
	char		*tablename;

	AssertVariableIsOfType(&pg_decode_change, LogicalDecodeChangeCB);

	data = ctx->output_plugin_private;

	/* filter changes by action */
	if (pg_filter_by_action(change->action, data->actions))
		return;

	class_form = RelationGetForm(relation);
	tupdesc = RelationGetDescr(relation);

	/* Avoid leaking memory by using and resetting our own context */
	old = MemoryContextSwitchTo(data->context);

	/* schema and table names are used for select tables */
	schemaname = get_namespace_name(class_form->relnamespace);
	tablename = NameStr(class_form->relname);

	if (data->write_in_chunks)
		OutputPluginPrepareWrite(ctx, true);

	/* Make sure rd_replidindex is set */
	RelationGetIndexList(relation);

	/* Filter tables, if available */
	if (pg_filter_by_table(data->filter_tables, schemaname, tablename))
	{
		MemoryContextSwitchTo(old);
		MemoryContextReset(data->context);
		return;
	}

	/* Add tables */
	if (!pg_add_by_table(data->add_tables, schemaname, tablename))
	{
		MemoryContextSwitchTo(old);
		MemoryContextReset(data->context);
		return;
	}

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
			if ((relation->rd_rel->relreplident != YB_REPLICA_IDENTITY_CHANGE) &&
				(!OidIsValid(relation->rd_replidindex) && relation->rd_rel->relreplident != REPLICA_IDENTITY_FULL))
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
			if (relation->rd_rel->relreplident != YB_REPLICA_IDENTITY_CHANGE && 
				!OidIsValid(relation->rd_replidindex) && relation->rd_rel->relreplident != REPLICA_IDENTITY_FULL)
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

	/* if we don't write in chunks, we need a newline here */
	if (!data->write_in_chunks)
		appendStringInfo(ctx->out, "%s", data->nl);

	appendStringInfo(ctx->out, "%s%s", data->ht, data->ht);

	if (data->nr_changes > 1)
		appendStringInfoChar(ctx->out, ',');

	appendStringInfo(ctx->out, "{%s", data->nl);

	/* Print change kind */
	switch (change->action)
	{
		case REORDER_BUFFER_CHANGE_INSERT:
			appendStringInfo(ctx->out, "%s%s%s\"kind\":%s\"insert\",%s", data->ht, data->ht, data->ht, data->sp, data->nl);
			break;
		case REORDER_BUFFER_CHANGE_UPDATE:
			appendStringInfo(ctx->out, "%s%s%s\"kind\":%s\"update\",%s", data->ht, data->ht, data->ht, data->sp, data->nl);
			break;
		case REORDER_BUFFER_CHANGE_DELETE:
			appendStringInfo(ctx->out, "%s%s%s\"kind\":%s\"delete\",%s", data->ht, data->ht, data->ht, data->sp, data->nl);
			break;
		default:
			Assert(false);
	}

	/* Print table name (possibly) qualified */
	if (data->include_schemas)
	{
		appendStringInfo(ctx->out, "%s%s%s\"schema\":%s", data->ht, data->ht, data->ht, data->sp);
		escape_json(ctx->out, get_namespace_name(class_form->relnamespace));
		appendStringInfo(ctx->out, ",%s", data->nl);
	}
	appendStringInfo(ctx->out, "%s%s%s\"table\":%s", data->ht, data->ht, data->ht, data->sp);
	escape_json(ctx->out, NameStr(class_form->relname));
	appendStringInfo(ctx->out, ",%s", data->nl);

	if (data->include_pk)
#if PG_VERSION_NUM >= 100000
		pkbs = RelationGetIndexAttrBitmap(relation, INDEX_ATTR_BITMAP_PRIMARY_KEY);
#else
		pkbs = RelationGetIndexAttrBitmap(relation, INDEX_ATTR_BITMAP_KEY);
#endif

	switch (change->action)
	{
		case REORDER_BUFFER_CHANGE_INSERT:
			/* Print the new tuple */
#if	PG_VERSION_NUM >= 100000
			if (data->include_pk && OidIsValid(relation->rd_pkindex))
#else
			if (data->include_pk && OidIsValid(relation->rd_replidindex) &&
					relation->rd_rel->relreplident == REPLICA_IDENTITY_DEFAULT)
#endif
			{
#if	PG_VERSION_NUM >= 170000
				columns_to_stringinfo(ctx, tupdesc, change->data.tp.newtuple, true, relation, change->data.tp.newtuple->yb_is_omitted);
				pk_to_stringinfo(ctx, tupdesc, change->data.tp.newtuple, pkbs, false, relation);
#else
				columns_to_stringinfo(ctx, tupdesc, &change->data.tp.newtuple->tuple, true, relation, change->data.tp.newtuple->yb_is_omitted);
				pk_to_stringinfo(ctx, tupdesc, &change->data.tp.newtuple->tuple, pkbs, false, relation);
#endif
			}
			else
			{
#if	PG_VERSION_NUM >= 170000
				columns_to_stringinfo(ctx, tupdesc, change->data.tp.newtuple, false, relation, change->data.tp.newtuple->yb_is_omitted);
#else
				columns_to_stringinfo(ctx, tupdesc, &change->data.tp.newtuple->tuple, false, relation, change->data.tp.newtuple->yb_is_omitted);
#endif
			}
			break;
		case REORDER_BUFFER_CHANGE_UPDATE:
			/* Print the new tuple */
#if	PG_VERSION_NUM >= 170000
			/* YB Note: Since no before image will come for CHANGE, skip adding the trailing comma */
			if (IsYugaByteEnabled())
				columns_to_stringinfo(ctx, tupdesc, change->data.tp.newtuple, relation->rd_rel->relreplident != YB_REPLICA_IDENTITY_CHANGE, relation, change->data.tp.newtuple->yb_is_omitted);
			else
			columns_to_stringinfo(ctx, tupdesc, change->data.tp.newtuple, true, relation, change->data.tp.newtuple->yb_is_omitted);
#else
			/* YB Note: Since no before image will come for CHANGE, skip adding the trailing comma */
			if (IsYugaByteEnabled())
				columns_to_stringinfo(ctx, tupdesc, &change->data.tp.newtuple->tuple, relation->rd_rel->relreplident != YB_REPLICA_IDENTITY_CHANGE, relation, change->data.tp.newtuple->yb_is_omitted);
			else
				columns_to_stringinfo(ctx, tupdesc, &change->data.tp.newtuple->tuple, true, relation, change->data.tp.newtuple->yb_is_omitted);
#endif

#if	PG_VERSION_NUM >= 100000
			if (data->include_pk && OidIsValid(relation->rd_pkindex))
#else
			if (data->include_pk && OidIsValid(relation->rd_replidindex) &&
					relation->rd_rel->relreplident == REPLICA_IDENTITY_DEFAULT)
#endif
			{
#if	PG_VERSION_NUM >= 170000
				pk_to_stringinfo(ctx, tupdesc, change->data.tp.newtuple, pkbs, true, relation);
#else
				pk_to_stringinfo(ctx, tupdesc, &change->data.tp.newtuple->tuple, pkbs, true, relation);
#endif
			}

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
				/*
				 * YB note: wal2json tries to populate the primary key
				 * (identity) in the before image even when it has not changed.
				 * However for Replica identity CHANGE we do not want any before
				 * image to be populated.
				 */
				if (!IsYugaByteEnabled() || relation->rd_rel->relreplident != YB_REPLICA_IDENTITY_CHANGE)
				{
					ribs = RelationGetIndexAttrBitmap(relation, INDEX_ATTR_BITMAP_IDENTITY_KEY);
#if	PG_VERSION_NUM >= 170000
					identity_to_stringinfo(ctx, tupdesc, change->data.tp.newtuple, ribs, change->data.tp.newtuple->yb_is_omitted, relation);
#else
					identity_to_stringinfo(ctx, tupdesc, &change->data.tp.newtuple->tuple, ribs, change->data.tp.newtuple->yb_is_omitted, relation);
#endif
				}
			}
			else
			{
				elog(DEBUG1, "old tuple is not null");
#if	PG_VERSION_NUM >= 170000
				identity_to_stringinfo(ctx, tupdesc, change->data.tp.oldtuple, NULL, change->data.tp.oldtuple->yb_is_omitted, relation);
#else
				identity_to_stringinfo(ctx, tupdesc, &change->data.tp.oldtuple->tuple, NULL, change->data.tp.oldtuple->yb_is_omitted, relation);
#endif
			}
			break;
		case REORDER_BUFFER_CHANGE_DELETE:
#if	PG_VERSION_NUM >= 100000
			if (data->include_pk && OidIsValid(relation->rd_pkindex))
#else
			if (data->include_pk && OidIsValid(relation->rd_replidindex) &&
					relation->rd_rel->relreplident == REPLICA_IDENTITY_DEFAULT)
#endif
			{
#if	PG_VERSION_NUM >= 170000
				pk_to_stringinfo(ctx, tupdesc, change->data.tp.oldtuple, pkbs, true, relation);
#else
				pk_to_stringinfo(ctx, tupdesc, &change->data.tp.oldtuple->tuple, pkbs, true, relation);
#endif
			}

			ribs = RelationGetIndexAttrBitmap(relation, INDEX_ATTR_BITMAP_IDENTITY_KEY);
#if	PG_VERSION_NUM >= 170000
			identity_to_stringinfo(ctx, tupdesc, change->data.tp.oldtuple, ribs, change->data.tp.oldtuple->yb_is_omitted, relation);
#else
			identity_to_stringinfo(ctx, tupdesc, &change->data.tp.oldtuple->tuple, ribs, change->data.tp.oldtuple->yb_is_omitted, relation);
#endif

			if (change->data.tp.oldtuple == NULL)
				elog(DEBUG1, "old tuple is null");
			else
				elog(DEBUG1, "old tuple is not null");
			break;
		default:
			Assert(false);
	}

	bms_free(pkbs);
	bms_free(ribs);

	appendStringInfo(ctx->out, "%s%s}", data->ht, data->ht);

	MemoryContextSwitchTo(old);
	MemoryContextReset(data->context);

	if (data->write_in_chunks)
		OutputPluginWrite(ctx, true);
}

static void
pg_decode_write_value(LogicalDecodingContext *ctx, Datum value, bool isnull, Oid typid, bool yb_unchanged_toasted)
{
	JsonDecodingData	*data;
	Oid					typoutfunc;
	bool				isvarlena;
	char				*outstr;

	data = ctx->output_plugin_private;

	if (isnull && !yb_unchanged_toasted)
	{
		appendStringInfoString(ctx->out, "null");
		return;
	}

	/* get type information and call its output function */
	getTypeOutputInfo(typid, &typoutfunc, &isvarlena);

	/* XXX dead code? check is one level above. */
	if ((yb_unchanged_toasted) || (isvarlena && VARATT_IS_EXTERNAL_ONDISK(value)))
	{
		elog(WARNING, "unchanged TOAST Datum");
		return;
	}

	/* if value is varlena, detoast Datum */
	if (isvarlena)
	{
		Datum	detoastedval;

		detoastedval = PointerGetDatum(PG_DETOAST_DATUM(value));
		outstr = OidOutputFunctionCall(typoutfunc, detoastedval);
	}
	else
	{
		outstr = OidOutputFunctionCall(typoutfunc, value);
	}

	/*
	 * Data types are printed with quotes unless they are number, true, false,
	 * null, an array or an object.
	 *
	 * The NaN an Infinity are not valid JSON symbols. Hence, regardless of
	 * sign they are represented as the string null.
	 *
	 * Exception to this is when data->numeric_data_types_as_string is
	 * true. In this case, numbers (including NaN and Infinity values)
	 * are printed with quotes.
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
			if (data->numeric_data_types_as_string) {
				if (strspn(outstr, "0123456789+-eE.") == strlen(outstr) ||
						pg_strncasecmp(outstr, "NaN", 3) == 0 ||
						pg_strncasecmp(outstr, "Infinity", 8) == 0 ||
						pg_strncasecmp(outstr, "-Infinity", 9) == 0) {
					escape_json(ctx->out, outstr);
				} else {
					elog(ERROR, "%s is not a number", outstr);
				}
			}
			else if (pg_strncasecmp(outstr, "NaN", 3) == 0 ||
					pg_strncasecmp(outstr, "Infinity", 8) == 0 ||
					pg_strncasecmp(outstr, "-Infinity", 9) == 0)
			{
				appendStringInfoString(ctx->out, "null");
				elog(DEBUG1, "special value: %s", outstr);
			}
			else if (strspn(outstr, "0123456789+-eE.") == strlen(outstr))
				appendStringInfo(ctx->out, "%s", outstr);
			else
				elog(ERROR, "%s is not a number", outstr);
			break;
		case BOOLOID:
			if (strcmp(outstr, "t") == 0)
				appendStringInfoString(ctx->out, "true");
			else
				appendStringInfoString(ctx->out, "false");
			break;
		case BYTEAOID:
			/* string is "\x54617069727573", start after \x */
			escape_json(ctx->out, (outstr + 2));
			break;
		default:
			escape_json(ctx->out, outstr);
			break;
	}
	pfree(outstr);
}

static void
pg_decode_write_tuple(LogicalDecodingContext *ctx, Relation relation, HeapTuple tuple, PGOutputJsonKind kind, bool *yb_is_omitted)
{
	JsonDecodingData	*data;
	TupleDesc			tupdesc;
	Relation			defrel = NULL;
	Bitmapset			*bs = NULL;
	int					i;
	Datum				*values;
	bool				*nulls;
	bool				need_sep = false;

	data = ctx->output_plugin_private;

	tupdesc = RelationGetDescr(relation);
	values = (Datum *) palloc(tupdesc->natts * sizeof(Datum));
	nulls = (bool *) palloc(tupdesc->natts * sizeof(bool));

	/* break down the tuple into fields */
	heap_deform_tuple(tuple, tupdesc, values, nulls);

	/* figure out replica identity columns */
	if (kind == PGOUTPUTJSON_IDENTITY)
	{
		bs = RelationGetIndexAttrBitmap(relation, INDEX_ATTR_BITMAP_IDENTITY_KEY);
	}
	else if (kind == PGOUTPUTJSON_PK)
	{
#if PG_VERSION_NUM >= 100000
		bs = RelationGetIndexAttrBitmap(relation, INDEX_ATTR_BITMAP_PRIMARY_KEY);
#else
		bs = RelationGetIndexAttrBitmap(relation, INDEX_ATTR_BITMAP_KEY);
#endif
	}

	/* open pg_attrdef in preparation to get default values from columns */
	if (kind == PGOUTPUTJSON_CHANGE && data->include_default)
	{
#if PG_VERSION_NUM >= 120000
		defrel = table_open(AttrDefaultRelationId, AccessShareLock);
#else
		defrel = heap_open(AttrDefaultRelationId, AccessShareLock);
#endif
	}

	for (i = 0; i < tupdesc->natts; i++)
	{
		Form_pg_attribute	attr;
		bool				yb_unchanged_toasted = false;

#if (PG_VERSION_NUM >= 90600 && PG_VERSION_NUM < 90605) || (PG_VERSION_NUM >= 90500 && PG_VERSION_NUM < 90509) || (PG_VERSION_NUM >= 90400 && PG_VERSION_NUM < 90414)
		attr = tupdesc->attrs[i];
#else
		attr = TupleDescAttr(tupdesc, i);
#endif

		/* skip dropped or system columns */
		if (attr->attisdropped || attr->attnum < 0)
			continue;

		if (bs != NULL && !bms_is_member(attr->attnum - YBGetFirstLowInvalidAttributeNumber(relation), bs))
			continue;

		if (IsYugaByteEnabled())
			yb_unchanged_toasted = yb_is_omitted && yb_is_omitted[i];

		/* don't send unchanged TOAST Datum */
		if (yb_unchanged_toasted ||
		   (!nulls[i] && (attr->attlen == -1 && VARATT_IS_EXTERNAL_ONDISK(values[i])))) 
			continue;

		if (need_sep)
			appendStringInfoChar(ctx->out, ',');
		need_sep = true;

		appendStringInfoChar(ctx->out, '{');
		appendStringInfoString(ctx->out, "\"name\":");
		escape_json(ctx->out, NameStr(attr->attname));

		/* type name (with typmod, if available) */
		if (data->include_types)
		{
			HeapTuple		type_tuple;
			Form_pg_type	type_form;
			char			*type_str;
			int				len;

			type_tuple = SearchSysCache1(TYPEOID, ObjectIdGetDatum(attr->atttypid));
			type_form = (Form_pg_type) GETSTRUCT(type_tuple);

			/*
			 * It is a domain. Replace domain name with base data type if
			 * include_domain_data_type is enabled.
			 */
			if (type_form->typtype == TYPTYPE_DOMAIN && data->include_domain_data_type)
				type_str = format_type_with_typemod(type_form->typbasetype, type_form->typtypmod);
			else
				type_str = format_type_with_typemod(attr->atttypid, attr->atttypmod);

			appendStringInfoString(ctx->out, ",\"type\":");
			/*
			 * format_type_with_typemod() returns a quoted identifier, if
			 * required. In this case, it doesn't need to enclose the type name
			 * in double quotes. However, if it is an array type, it should
			 * escape it because the brackets are outside the double quotes.
			 */
			len = strlen(type_str);
			if (type_str[0] == '"' && type_str[len -1] != ']')
				appendStringInfo(ctx->out, "%s", type_str);
			else
				escape_json(ctx->out, type_str);
			pfree(type_str);

			ReleaseSysCache(type_tuple);
		}

		/*
		 * Print type oid for columns.
		 */
		if (data->include_type_oids)
		{
			appendStringInfoString(ctx->out, ",\"typeoid\":");
			appendStringInfo(ctx->out, "%d", attr->atttypid);
		}

		if (kind != PGOUTPUTJSON_PK)
		{
			appendStringInfoString(ctx->out, ",\"value\":");
			pg_decode_write_value(ctx, values[i], nulls[i], attr->atttypid, yb_unchanged_toasted);
		}

		/*
		 * Print optional for columns. This information is redundant for
		 * replica identity (index) because all attributes are not null.
		 */
		if (kind == PGOUTPUTJSON_CHANGE && data->include_not_null)
		{
			if (attr->attnotnull)
				appendStringInfoString(ctx->out, ",\"optional\":false");
			else
				appendStringInfoString(ctx->out, ",\"optional\":true");
		}

		/*
		 * Print position for columns. Positions are only available for new
		 * tuple (INSERT, UPDATE).
		 */
		if (kind == PGOUTPUTJSON_CHANGE && data->include_column_positions)
		{
			appendStringInfoString(ctx->out, ",\"position\":");
			appendStringInfo(ctx->out, "%d", attr->attnum);
		}

		/*
		 * Print default for columns.
		 */
		if (kind == PGOUTPUTJSON_CHANGE && data->include_default)
		{
#if PG_VERSION_NUM >= 120000
			if (attr->atthasdef && attr->attgenerated == '\0')
#else
			if (attr->atthasdef)
#endif
			{
				ScanKeyData			scankeys[2];
				SysScanDesc			scan;
				HeapTuple			def_tuple;
				Datum				def_value;
				bool				isnull;
				char				*result;

				ScanKeyInit(&scankeys[0],
							Anum_pg_attrdef_adrelid,
							BTEqualStrategyNumber, F_OIDEQ,
							ObjectIdGetDatum(relation->rd_id));
				ScanKeyInit(&scankeys[1],
							Anum_pg_attrdef_adnum,
							BTEqualStrategyNumber, F_INT2EQ,
							Int16GetDatum(attr->attnum));

				scan = systable_beginscan(defrel, AttrDefaultIndexId, true,
											NULL, 2, scankeys);

				def_tuple = systable_getnext(scan);
				if (HeapTupleIsValid(def_tuple))
				{
					def_value = fastgetattr(def_tuple, Anum_pg_attrdef_adbin, defrel->rd_att, &isnull);

					if (!isnull)
					{
						result = TextDatumGetCString(DirectFunctionCall2(pg_get_expr,
																	def_value,
																	ObjectIdGetDatum(relation->rd_id)));

						appendStringInfoString(ctx->out, ",\"default\":");
						appendStringInfo(ctx->out, "\"%s\"", result);
						pfree(result);
					}
					else
					{
						/*
						 * null means that default was not set. Is it possible?
						 * atthasdef shouldn't be set.
						 */
						appendStringInfoString(ctx->out, ",\"default\":null");
					}
				}

				systable_endscan(scan);
			}
			else
			{
				/*
				 * no DEFAULT clause implicitly means that the default is NULL
				 */
				appendStringInfoString(ctx->out, ",\"default\":null");
			}
		}

		appendStringInfoChar(ctx->out, '}');
	}

	/* close pg_attrdef */
	if (kind == PGOUTPUTJSON_CHANGE && data->include_default)
	{
#if PG_VERSION_NUM >= 120000
		table_close(defrel, AccessShareLock);
#else
		heap_close(defrel, AccessShareLock);
#endif
	}

	bms_free(bs);

	pfree(values);
	pfree(nulls);
}

static void
pg_decode_write_change(LogicalDecodingContext *ctx, ReorderBufferTXN *txn, Relation relation, ReorderBufferChange *change)
{
	JsonDecodingData *data = ctx->output_plugin_private;

	/* make sure rd_pkindex and rd_replidindex are set */
	RelationGetIndexList(relation);

	switch (change->action)
	{
		case REORDER_BUFFER_CHANGE_INSERT:
			if (change->data.tp.newtuple == NULL)
			{
				elog(WARNING, "no tuple data for INSERT in table \"%s\".\"%s\"", get_namespace_name(RelationGetNamespace(relation)), RelationGetRelationName(relation));
				return;
			}
			break;
		case REORDER_BUFFER_CHANGE_UPDATE:
			if (change->data.tp.newtuple == NULL)
			{
				elog(WARNING, "no tuple data for UPDATE in table \"%s\".\"%s\"", get_namespace_name(RelationGetNamespace(relation)), RelationGetRelationName(relation));
				return;
			}
			if (change->data.tp.oldtuple == NULL)
			{
				if (!OidIsValid(relation->rd_replidindex) && relation->rd_rel->relreplident != REPLICA_IDENTITY_FULL)
				{
					if (!IsYugaByteEnabled() || relation->rd_rel->relreplident != YB_REPLICA_IDENTITY_CHANGE)
					{
						elog(WARNING,"no tuple identifier for UPDATE in table \"%s\".\"%s\"", get_namespace_name(RelationGetNamespace(relation)), RelationGetRelationName(relation));
						return;
					}
				}
			}
			break;
		case REORDER_BUFFER_CHANGE_DELETE:
			if (change->data.tp.oldtuple == NULL)
			{
				if (!OidIsValid(relation->rd_replidindex) && relation->rd_rel->relreplident != REPLICA_IDENTITY_FULL)
				{
					elog(WARNING, "no tuple identifier for DELETE in table \"%s\".\"%s\"", get_namespace_name(RelationGetNamespace(relation)), RelationGetRelationName(relation));
					return;
				}
			}
			break;
		default:
			Assert(false);
	}

	OutputPluginPrepareWrite(ctx, true);

	appendStringInfoChar(ctx->out, '{');

	switch (change->action)
	{
		case REORDER_BUFFER_CHANGE_INSERT:
			appendStringInfoString(ctx->out, "\"action\":\"I\"");
			break;
		case REORDER_BUFFER_CHANGE_UPDATE:
			appendStringInfoString(ctx->out, "\"action\":\"U\"");
			break;
		case REORDER_BUFFER_CHANGE_DELETE:
			appendStringInfoString(ctx->out, "\"action\":\"D\"");
			break;
		default:
			Assert(false);
	}

	if (data->include_xids)
		appendStringInfo(ctx->out, ",\"xid\":%u", txn->xid);

#if PG_VERSION_NUM >= 150000
	if (data->include_timestamp)
		appendStringInfo(ctx->out, ",\"timestamp\":\"%s\"", timestamptz_to_str(txn->xact_time.commit_time));
#else
	if (data->include_timestamp)
		appendStringInfo(ctx->out, ",\"timestamp\":\"%s\"", timestamptz_to_str(txn->commit_time));
#endif

#if PG_VERSION_NUM >= 90500
	if (data->include_origin)
		appendStringInfo(ctx->out, ",\"origin\":%u", txn->origin_id);
#endif

	if (data->include_lsn)
	{
		char *lsn_str = DatumGetCString(DirectFunctionCall1(pg_lsn_out, UInt64GetDatum(change->lsn)));
		appendStringInfo(ctx->out, ",\"lsn\":\"%s\"", lsn_str);
		pfree(lsn_str);
	}

	if (data->include_schemas)
	{
		appendStringInfo(ctx->out, ",\"schema\":");
		escape_json(ctx->out, get_namespace_name(RelationGetNamespace(relation)));
	}

	appendStringInfo(ctx->out, ",\"table\":");
	escape_json(ctx->out, RelationGetRelationName(relation));

	/* print new tuple (INSERT, UPDATE) */
	if (change->data.tp.newtuple != NULL)
	{
		appendStringInfoString(ctx->out, ",\"columns\":[");
#if PG_VERSION_NUM >= 170000
		pg_decode_write_tuple(ctx, relation, change->data.tp.newtuple, PGOUTPUTJSON_CHANGE, change->data.tp.newtuple->yb_is_omitted);
#else
		pg_decode_write_tuple(ctx, relation, &change->data.tp.newtuple->tuple, PGOUTPUTJSON_CHANGE, change->data.tp.newtuple->yb_is_omitted);
#endif
		appendStringInfoChar(ctx->out, ']');
	}

	/*
	 * Print old tuple (UPDATE, DELETE)
	 *
	 * old tuple is available when:
	 * (1) primary key changes;
	 * (2) replica identity is index and one of the indexed columns changes;
	 * (3) replica identity is full.
	 *
	 * If old tuple is not available (because (a) primary key does not change
	 * or (b) replica identity is index and none of indexed columns does not
	 * change) identity is obtained from new tuple (because it doesn't change).
	 *
	 */
	if (change->data.tp.oldtuple != NULL)
	{
		appendStringInfoString(ctx->out, ",\"identity\":[");
#if	PG_VERSION_NUM >= 170000
		pg_decode_write_tuple(ctx, relation, change->data.tp.oldtuple, PGOUTPUTJSON_IDENTITY, change->data.tp.oldtuple->yb_is_omitted);
#else
		pg_decode_write_tuple(ctx, relation, &change->data.tp.oldtuple->tuple, PGOUTPUTJSON_IDENTITY, change->data.tp.oldtuple->yb_is_omitted);
#endif
		appendStringInfoChar(ctx->out, ']');
	}
	else
	{
		/*
		 * Old tuple is not available, however, identity can be obtained from
		 * new tuple (because it doesn't change).
		 */
		if (change->action == REORDER_BUFFER_CHANGE_UPDATE)
		{
			elog(DEBUG2, "old tuple is null on UPDATE");

			/*
			 * Before v10, there is not rd_pkindex then rely on REPLICA
			 * IDENTITY DEFAULT to obtain primary key.
			 */
#if PG_VERSION_NUM >= 100000
			if (OidIsValid(relation->rd_pkindex) || OidIsValid(relation->rd_replidindex))
#else
			if (OidIsValid(relation->rd_replidindex))
#endif
			{
				/*
				 * YB note: wal2json tries to populate the primary key
				 * (identity) in the before image even when it has not changed.
				 * However for Replica identity CHANGE we do not want any before
				 * image to be populated.
				 */
				if (!IsYugaByteEnabled() || relation->rd_rel->relreplident != YB_REPLICA_IDENTITY_CHANGE)
				{
					elog(DEBUG1, "REPLICA IDENTITY: obtain old tuple using new tuple");
					appendStringInfoString(ctx->out, ",\"identity\":[");
#if PG_VERSION_NUM >= 170000
					pg_decode_write_tuple(ctx, relation, change->data.tp.newtuple, PGOUTPUTJSON_IDENTITY, change->data.tp.newtuple->yb_is_omitted);
#else
					pg_decode_write_tuple(ctx, relation, &change->data.tp.newtuple->tuple, PGOUTPUTJSON_IDENTITY, change->data.tp.newtuple->yb_is_omitted);
#endif
					appendStringInfoChar(ctx->out, ']');
				}
			}
			else
			{
				/* old tuple is not available and can't be obtained, report it */
				elog(WARNING, "no old tuple data for UPDATE in table \"%s\".\"%s\"", get_namespace_name(RelationGetNamespace(relation)), RelationGetRelationName(relation));
			}
		}

		/* old tuple is not available and can't be obtained, report it */
		if (change->action == REORDER_BUFFER_CHANGE_DELETE)
		{
			elog(WARNING, "no old tuple data for DELETE in table \"%s\".\"%s\"", get_namespace_name(RelationGetNamespace(relation)), RelationGetRelationName(relation));
		}
	}

	if (data->include_pk)
	{
		appendStringInfoString(ctx->out, ",\"pk\":[");
#if PG_VERSION_NUM >= 100000
		if (OidIsValid(relation->rd_pkindex))
#else
		if (OidIsValid(relation->rd_replidindex) && relation->rd_rel->relreplident == REPLICA_IDENTITY_DEFAULT)
#endif
		{
#if PG_VERSION_NUM >= 170000
			if (change->data.tp.oldtuple != NULL)
				pg_decode_write_tuple(ctx, relation, change->data.tp.oldtuple, PGOUTPUTJSON_PK, change->data.tp.oldtuple->yb_is_omitted);
			else
				pg_decode_write_tuple(ctx, relation, change->data.tp.newtuple, PGOUTPUTJSON_PK, change->data.tp.newtuple->yb_is_omitted);
#else
			if (change->data.tp.oldtuple != NULL)
				pg_decode_write_tuple(ctx, relation, &change->data.tp.oldtuple->tuple, PGOUTPUTJSON_PK, change->data.tp.oldtuple->yb_is_omitted);
			else
				pg_decode_write_tuple(ctx, relation, &change->data.tp.newtuple->tuple, PGOUTPUTJSON_PK, change->data.tp.newtuple->yb_is_omitted);
#endif
		}
		appendStringInfoChar(ctx->out, ']');
	}

	appendStringInfoChar(ctx->out, '}');

	OutputPluginWrite(ctx, true);
}

static void
pg_decode_change_v2(LogicalDecodingContext *ctx, ReorderBufferTXN *txn,
				 Relation relation, ReorderBufferChange *change)
{
	JsonDecodingData *data = ctx->output_plugin_private;
	MemoryContext old;

	char	*schemaname;
	char	*tablename;

	/* filter changes by action */
	if (pg_filter_by_action(change->action, data->actions))
		return;

	/* avoid leaking memory by using and resetting our own context */
	old = MemoryContextSwitchTo(data->context);

	/* schema and table names are used for chosen tables */
	schemaname = get_namespace_name(RelationGetNamespace(relation));
	tablename = RelationGetRelationName(relation);

	/* Exclude tables, if available */
	if (pg_filter_by_table(data->filter_tables, schemaname, tablename))
	{
		MemoryContextSwitchTo(old);
		MemoryContextReset(data->context);
		return;
	}

	/* Add tables */
	if (!pg_add_by_table(data->add_tables, schemaname, tablename))
	{
		MemoryContextSwitchTo(old);
		MemoryContextReset(data->context);
		return;
	}

	pg_decode_write_change(ctx, txn, relation, change);

	MemoryContextSwitchTo(old);
	MemoryContextReset(data->context);
}

#if PG_VERSION_NUM >= 90600
/* Callback for generic logical decoding messages */
static void
pg_decode_message(LogicalDecodingContext *ctx, ReorderBufferTXN *txn,
		XLogRecPtr lsn, bool transactional, const char *prefix, Size
		content_size, const char *content)
{
	JsonDecodingData *data = ctx->output_plugin_private;

#if PG_VERSION_NUM >= 150000 && PG_VERSION_NUM < 160000
	update_replication_progress(ctx, false);
#elif PG_VERSION_NUM >= 140004 && PG_VERSION_NUM < 150000
	update_replication_progress(ctx);
#elif PG_VERSION_NUM >= 130008 && PG_VERSION_NUM < 140000
	update_replication_progress(ctx);
#elif PG_VERSION_NUM >= 120012 && PG_VERSION_NUM < 130000
	update_replication_progress(ctx);
#elif PG_VERSION_NUM >= 110017 && PG_VERSION_NUM < 120000
	update_replication_progress(ctx);
#elif PG_VERSION_NUM >= 100022 && PG_VERSION_NUM < 110000
	update_replication_progress(ctx);
#endif

	/* Filter message prefixes, if available */
	if (list_length(data->filter_msg_prefixes) > 0)
	{
		ListCell	*lc;

		foreach(lc, data->filter_msg_prefixes)
		{
			char	*p = lfirst(lc);

			if (strcmp(p, prefix) == 0)
			{
				elog(DEBUG2, "message prefix \"%s\" was filtered out", p);
				return;
			}
		}
	}

	/* Add messages by prefix */
	if (list_length(data->add_msg_prefixes) > 0)
	{
		ListCell	*lc;
		bool		skip = true;

		foreach(lc, data->add_msg_prefixes)
		{
			char	*p = lfirst(lc);

			if (strcmp(p, prefix) == 0)
				skip = false;
		}

		if (skip)
		{
			elog(DEBUG2, "message prefix \"%s\" was skipped", prefix);
			return;
		}
	}

	if (data->format_version == 2)
		pg_decode_message_v2(ctx, txn, lsn, transactional, prefix, content_size, content);
	else if (data->format_version == 1)
		pg_decode_message_v1(ctx, txn, lsn, transactional, prefix, content_size, content);
	else
		elog(ERROR, "format version %d is not supported", data->format_version);
}

static void
pg_decode_message_v1(LogicalDecodingContext *ctx, ReorderBufferTXN *txn,
		XLogRecPtr lsn, bool transactional, const char *prefix, Size
		content_size, const char *content)
{
	JsonDecodingData *data;
	MemoryContext old;
	char *content_str;

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

	/* if we don't write in chunks, we need a newline here */
	if (!data->write_in_chunks && transactional)
			appendStringInfo(ctx->out, "%s", data->nl);

	/* build a complete JSON object for non-transactional message */
	if (!transactional)
		appendStringInfo(ctx->out, "{%s%s\"change\":%s[%s", data->nl, data->ht, data->sp, data->nl);

	appendStringInfo(ctx->out, "%s%s", data->ht, data->ht);

	/*
	 * Non-transactional message contains only one object. Comma is not
	 * required. Avoid printing a comma for non-transactional messages that was
	 * provided in a transaction.
	 */
	if (transactional && data->nr_changes > 1)
		appendStringInfoChar(ctx->out, ',');

	appendStringInfo(ctx->out, "{%s%s%s%s\"kind\":%s\"message\",%s", data->nl, data->ht, data->ht, data->ht, data->sp, data->nl);

	if (transactional)
		appendStringInfo(ctx->out, "%s%s%s\"transactional\":%strue,%s", data->ht, data->ht, data->ht, data->sp, data->nl);
	else
		appendStringInfo(ctx->out, "%s%s%s\"transactional\":%sfalse,%s", data->ht, data->ht, data->ht, data->sp, data->nl);

	appendStringInfo(ctx->out, "%s%s%s\"prefix\":%s", data->ht, data->ht, data->ht, data->sp);
	escape_json(ctx->out, prefix);
	appendStringInfo(ctx->out, ",%s%s%s%s\"content\":%s", data->nl, data->ht, data->ht, data->ht, data->sp);

	content_str = (char *) palloc0((content_size + 1) * sizeof(char));
	strncpy(content_str, content, content_size);
	escape_json(ctx->out, content_str);
	pfree(content_str);

	appendStringInfo(ctx->out, "%s%s%s}", data->nl, data->ht, data->ht);

	/* build a complete JSON object for non-transactional message */
	if (!transactional)
		appendStringInfo(ctx->out, "%s%s]%s}", data->nl, data->ht, data->nl);

	MemoryContextSwitchTo(old);
	MemoryContextReset(data->context);

	if (data->write_in_chunks || !transactional)
		OutputPluginWrite(ctx, true);
}

static void
pg_decode_message_v2(LogicalDecodingContext *ctx, ReorderBufferTXN *txn,
		XLogRecPtr lsn, bool transactional, const char *prefix, Size
		content_size, const char *content)
{
	JsonDecodingData	*data = ctx->output_plugin_private;
	MemoryContext		old;
	char				*content_str;

	/* Avoid leaking memory by using and resetting our own context */
	old = MemoryContextSwitchTo(data->context);

	OutputPluginPrepareWrite(ctx, true);
	appendStringInfoChar(ctx->out, '{');
	appendStringInfoString(ctx->out, "\"action\":\"M\"");

	if (data->include_xids)
	{
		/*
		 * Non-transactional messages can have no xid, hence, assigns null in
		 * this case.  Assigns null for xid in non-transactional messages
		 * because in some cases there isn't an assigned xid.
		 * This same logic is valid for timestamp and origin.
		 */
		if (transactional)
			appendStringInfo(ctx->out, ",\"xid\":%u", txn->xid);
		else
			appendStringInfoString(ctx->out, ",\"xid\":null");
	}

	if (data->include_timestamp)
	{
#if PG_VERSION_NUM >= 150000
		if (transactional)
			appendStringInfo(ctx->out, ",\"timestamp\":\"%s\"", timestamptz_to_str(txn->xact_time.commit_time));
#else
		if (transactional)
			appendStringInfo(ctx->out, ",\"timestamp\":\"%s\"", timestamptz_to_str(txn->commit_time));
#endif
		else
			appendStringInfoString(ctx->out, ",\"timestamp\":null");
	}

	if (data->include_origin)
	{
		if (transactional)
			appendStringInfo(ctx->out, ",\"origin\":%u", txn->origin_id);
		else
			appendStringInfo(ctx->out, ",\"origin\":null");
	}

	if (data->include_lsn)
	{
		char *lsn_str = DatumGetCString(DirectFunctionCall1(pg_lsn_out, UInt64GetDatum(lsn)));
		appendStringInfo(ctx->out, ",\"lsn\":\"%s\"", lsn_str);
		pfree(lsn_str);
	}

	if (transactional)
		appendStringInfoString(ctx->out, ",\"transactional\":true");
	else
		appendStringInfoString(ctx->out, ",\"transactional\":false");

	appendStringInfoString(ctx->out, ",\"prefix\":");
	escape_json(ctx->out, prefix);

	appendStringInfoString(ctx->out, ",\"content\":");
	content_str = (char *) palloc0((content_size + 1) * sizeof(char));
	strncpy(content_str, content, content_size);
	escape_json(ctx->out, content_str);
	pfree(content_str);

	appendStringInfoChar(ctx->out, '}');
	OutputPluginWrite(ctx, true);

	MemoryContextSwitchTo(old);
	MemoryContextReset(data->context);
}
#endif

#if PG_VERSION_NUM >= 110000
/* Callback for TRUNCATE command */
static void pg_decode_truncate(LogicalDecodingContext *ctx,
					ReorderBufferTXN *txn, int n, Relation relations[],
					ReorderBufferChange *change)
{
	JsonDecodingData *data = ctx->output_plugin_private;

	/*
	 * For back branches (10 to 15), update_replication_progress() should be called here.
	 * FIXME call OutputPluginUpdateProgress() for old minor versions?
	 */
#if PG_VERSION_NUM >= 150000 && PG_VERSION_NUM < 160000
	update_replication_progress(ctx, false);
#elif PG_VERSION_NUM >= 140004 && PG_VERSION_NUM < 150000
	update_replication_progress(ctx);
#elif PG_VERSION_NUM >= 130008 && PG_VERSION_NUM < 140000
	update_replication_progress(ctx);
#elif PG_VERSION_NUM >= 120012 && PG_VERSION_NUM < 130000
	update_replication_progress(ctx);
#elif PG_VERSION_NUM >= 110017 && PG_VERSION_NUM < 120000
	update_replication_progress(ctx);
#endif

	if (data->format_version == 2)
		pg_decode_truncate_v2(ctx, txn, n, relations, change);
	else if (data->format_version == 1)
		pg_decode_truncate_v1(ctx, txn, n, relations, change);
	else
		elog(ERROR, "format version %d is not supported", data->format_version);
}

static void pg_decode_truncate_v1(LogicalDecodingContext *ctx,
					ReorderBufferTXN *txn, int n, Relation relations[],
					ReorderBufferChange *change)
{
#ifdef	_NOT_USED
	JsonDecodingData *data;
	MemoryContext old;
	int		i;

	data = ctx->output_plugin_private;

	if (!data->actions.truncate)
	{
		elog(DEBUG3, "ignore TRUNCATE");
		return;
	}

	/* Avoid leaking memory by using and resetting our own context */
	old = MemoryContextSwitchTo(data->context);

	/* Exclude tables, if available */
	if (pg_filter_by_table(data->filter_tables, schemaname, tablename))
	{
		MemoryContextSwitchTo(old);
		MemoryContextReset(data->context);
		continue;
	}

	/* Add tables */
	if (!pg_add_by_table(data->add_tables, schemaname, tablename))
	{
		MemoryContextSwitchTo(old);
		MemoryContextReset(data->context);
		continue;
	}

	if (data->write_in_chunks)
		OutputPluginPrepareWrite(ctx, true);

	/*
	 * increment counter only for transactional messages because
	 * non-transactional message has only one object.
	 */
	data->nr_changes++;

	/* if we don't write in chunks, we need a newline here */
	if (!data->write_in_chunks)
			appendStringInfo(ctx->out, "%s", data->nl);

	appendStringInfo(ctx->out, "%s%s", data->ht, data->ht);

	if (data->nr_changes > 1)
		appendStringInfoChar(ctx->out, ',');

	appendStringInfo(ctx->out, "{%s%s%s%s\"kind\":%s\"truncate\",%s", data->nl, data->ht, data->ht, data->ht, data->sp, data->nl);

	if (data->include_xids)
		appendStringInfo(ctx->out, "%s%s%s\"xid\":%s%u,%s", data->ht, data->ht, data->ht, data->sp, txn->xid, data->nl);

#if PG_VERSION_NUM >= 150000
	if (data->include_timestamp)
		appendStringInfo(ctx->out, "%s%s%s\"timestamp\":%s\"%s\",%s", data->ht, data->ht, data->ht, data->sp, timestamptz_to_str(txn->xact_time.commit_time), data->nl);
#else
	if (data->include_timestamp)
		appendStringInfo(ctx->out, "%s%s%s\"timestamp\":%s\"%s\",%s", data->ht, data->ht, data->ht, data->sp, timestamptz_to_str(txn->commit_time), data->nl);
#endif

	if (data->include_origin)
		appendStringInfo(ctx->out, "%s%s%s\"origin\":%s%u,%s", data->ht, data->ht, data->ht, data->sp, txn->origin_id, data->nl);

	if (data->include_lsn)
	{
		char *lsn_str = DatumGetCString(DirectFunctionCall1(pg_lsn_out, UInt64GetDatum(change->lsn)));
		appendStringInfo(ctx->out, "%s%s%s\"lsn\":%s\"%s\",%s", data->ht, data->ht, data->ht, data->sp, lsn_str, data->nl);
		pfree(lsn_str);
	}

	for (i = 0; i < n; i++)
	{
		if (data->include_schemas)
		{
			appendStringInfo(ctx->out, "%s%s%s\"schema\":%s", data->ht, data->ht, data->ht, data->sp);
			escape_json(ctx->out, get_namespace_name(RelationGetNamespace(relations[i])));
			appendStringInfo(ctx->out, ",%s", data->nl);
		}

		appendStringInfo(ctx->out, "%s%s%s\"table\":%s", data->ht, data->ht, data->ht, data->sp);
		escape_json(ctx->out, RelationGetRelationName(relations[i]));
	}

	appendStringInfo(ctx->out, "%s%s%s}", data->nl, data->ht, data->ht);

	MemoryContextSwitchTo(old);
	MemoryContextReset(data->context);

	if (data->write_in_chunks)
		OutputPluginWrite(ctx, true);
#endif
}

static void pg_decode_truncate_v2(LogicalDecodingContext *ctx,
					ReorderBufferTXN *txn, int n, Relation relations[],
					ReorderBufferChange *change)
{
	JsonDecodingData *data = ctx->output_plugin_private;
	MemoryContext old;
	int		i;

	if (!data->actions.truncate)
	{
		elog(DEBUG3, "ignore TRUNCATE");
		return;
	}

	/* avoid leaking memory by using and resetting our own context */
	old = MemoryContextSwitchTo(data->context);

	for (i = 0; i < n; i++)
	{
		char	*schemaname;
		char	*tablename;

		/* schema and table names are used for chosen tables */
		schemaname = get_namespace_name(RelationGetNamespace(relations[i]));
		tablename = RelationGetRelationName(relations[i]);

		/* Exclude tables, if available */
		if (pg_filter_by_table(data->filter_tables, schemaname, tablename))
		{
			MemoryContextSwitchTo(old);
			MemoryContextReset(data->context);
			continue;
		}

		/* Add tables */
		if (!pg_add_by_table(data->add_tables, schemaname, tablename))
		{
			MemoryContextSwitchTo(old);
			MemoryContextReset(data->context);
			continue;
		}

		OutputPluginPrepareWrite(ctx, true);
		appendStringInfoChar(ctx->out, '{');
		appendStringInfoString(ctx->out, "\"action\":\"T\"");

		if (data->include_xids)
			appendStringInfo(ctx->out, ",\"xid\":%u", txn->xid);

#if PG_VERSION_NUM >= 150000
		if (data->include_timestamp)
			appendStringInfo(ctx->out, ",\"timestamp\":\"%s\"", timestamptz_to_str(txn->xact_time.commit_time));
#else
		if (data->include_timestamp)
			appendStringInfo(ctx->out, ",\"timestamp\":\"%s\"", timestamptz_to_str(txn->commit_time));
#endif

		if (data->include_origin)
			appendStringInfo(ctx->out, ",\"origin\":%u", txn->origin_id);

		if (data->include_lsn)
		{
			char *lsn_str = DatumGetCString(DirectFunctionCall1(pg_lsn_out, UInt64GetDatum(change->lsn)));
			appendStringInfo(ctx->out, ",\"lsn\":\"%s\"", lsn_str);
			pfree(lsn_str);
		}

		if (data->include_schemas)
		{
			appendStringInfo(ctx->out, ",\"schema\":");
			escape_json(ctx->out, schemaname);
		}

		appendStringInfo(ctx->out, ",\"table\":");
		escape_json(ctx->out, tablename);

		appendStringInfoChar(ctx->out, '}');
		OutputPluginWrite(ctx, true);
	}

	MemoryContextSwitchTo(old);
	MemoryContextReset(data->context);
}
#endif

static bool
parse_table_identifier(List *qualified_tables, char separator, List **select_tables)
{
	ListCell	*lc;

	foreach(lc, qualified_tables)
	{
		char		*str = lfirst(lc);
		char		*startp;
		char		*nextp;
		int			len;
		SelectTable	*t = palloc0(sizeof(SelectTable));

		/*
		 * Detect a special character that means all schemas. There could be a
		 * schema named "*" thus this test should be before we remove the
		 * escape character.
		 */
		if (str[0] == '*' && str[1] == '.')
			t->allschemas = true;
		else
			t->allschemas = false;

		startp = nextp = str;
		while (*nextp && *nextp != separator)
		{
			/* remove escape character */
			if (*nextp == '\\')
				memmove(nextp, nextp + 1, strlen(nextp));
			nextp++;
		}
		len = nextp - startp;

		/* if separator was not found, schema was not informed */
		if (*nextp == '\0')
		{
			pfree(t);
			return false;
		}
		else
		{
			/* schema name */
			t->schemaname = (char *) palloc0((len + 1) * sizeof(char));
			strncpy(t->schemaname, startp, len);

			nextp++;			/* jump separator */
			startp = nextp;		/* start new identifier (table name) */

			/*
			 * Detect a special character that means all tables. There could be
			 * a table named "*" thus this test should be before that we remove
			 * the escape character.
			 */
			if (startp[0] == '*' && startp[1] == '\0')
				t->alltables = true;
			else
				t->alltables = false;

			while (*nextp)
			{
				/* remove escape character */
				if (*nextp == '\\')
					memmove(nextp, nextp + 1, strlen(nextp));
				nextp++;
			}
			len = nextp - startp;

			/* table name */
			t->tablename = (char *) palloc0((len + 1) * sizeof(char));
			strncpy(t->tablename, startp, len);
		}

		*select_tables = lappend(*select_tables, t);
	}

	return true;
}

static bool
string_to_SelectTable(char *rawstring, char separator, List **select_tables)
{
	char	   *nextp;
	bool		done = false;
	List	   *qualified_tables = NIL;

	nextp = rawstring;

	while (isspace(*nextp))
		nextp++;				/* skip leading whitespace */

	if (*nextp == '\0')
		return true;			/* allow empty string */

	/* At the top of the loop, we are at start of a new identifier. */
	do
	{
		char	   *curname;
		char	   *endp;
		char	   *qname;

		curname = nextp;
		while (*nextp && *nextp != separator && !isspace(*nextp))
		{
			if (*nextp == '\\')
				nextp++;	/* ignore next character because of escape */
			nextp++;
		}
		endp = nextp;
		if (curname == nextp)
			return false;	/* empty unquoted name not allowed */

		while (isspace(*nextp))
			nextp++;			/* skip trailing whitespace */

		if (*nextp == separator)
		{
			nextp++;
			while (isspace(*nextp))
				nextp++;		/* skip leading whitespace for next */
			/* we expect another name, so done remains false */
		}
		else if (*nextp == '\0')
			done = true;
		else
			return false;		/* invalid syntax */

		/* Now safe to overwrite separator with a null */
		*endp = '\0';

		/*
		 * Finished isolating current name --- add it to list
		 */
		qname = pstrdup(curname);
		qualified_tables = lappend(qualified_tables, qname);

		/* Loop back if we didn't reach end of string */
	} while (!done);

	if (!parse_table_identifier(qualified_tables, '.', select_tables))
		return false;

	list_free_deep(qualified_tables);

	return true;
}

static bool
split_string_to_list(char *rawstring, char separator, List **sl)
{
	char	   *nextp;
	bool		done = false;

	nextp = rawstring;

	while (isspace(*nextp))
		nextp++;				/* skip leading whitespace */

	if (*nextp == '\0')
		return true;			/* allow empty string */

	/* At the top of the loop, we are at start of a new identifier. */
	do
	{
		char	   *curname;
		char	   *endp;
		char	   *pname;

		curname = nextp;
		while (*nextp && *nextp != separator && !isspace(*nextp))
		{
			if (*nextp == '\\')
				nextp++;	/* ignore next character because of escape */
			nextp++;
		}
		endp = nextp;
		if (curname == nextp)
			return false;	/* empty unquoted name not allowed */

		while (isspace(*nextp))
			nextp++;			/* skip trailing whitespace */

		if (*nextp == separator)
		{
			nextp++;
			while (isspace(*nextp))
				nextp++;		/* skip leading whitespace for next */
			/* we expect another name, so done remains false */
		}
		else if (*nextp == '\0')
			done = true;
		else
			return false;		/* invalid syntax */

		/* Now safe to overwrite separator with a null */
		*endp = '\0';

		/*
		 * Finished isolating current name --- add it to list
		 */
		pname = pstrdup(curname);
		*sl = lappend(*sl, pname);

		/* Loop back if we didn't reach end of string */
	} while (!done);

	return true;
}

/*
 * Convert a string into a list of Oids
 */
static bool
split_string_to_oid_list(char *rawstring, char separator, List **sl)
{
	char	   *nextp;
	bool		done = false;

	nextp = rawstring;

	while (isspace(*nextp))
		nextp++;				/* skip leading whitespace */

	if (*nextp == '\0')
		return true;			/* allow empty string */

	/* At the top of the loop, we are at start of a new identifier. */
	do
	{
		char	   *tok;
		char	   *endp;
		Oid			originid;

		tok = nextp;
		while (*nextp && *nextp != separator && !isspace(*nextp))
		{
			if (*nextp == '\\')
				nextp++;	/* ignore next character because of escape */
			nextp++;
		}
		endp = nextp;

		while (isspace(*nextp))
			nextp++;			/* skip trailing whitespace */

		if (*nextp == separator)
		{
			nextp++;
			while (isspace(*nextp))
				nextp++;		/* skip leading whitespace for next */
			/* we expect another name, so done remains false */
		}
		else if (*nextp == '\0')
			done = true;
		else
			return false;		/* invalid syntax */

		/* Now safe to overwrite separator with a null */
		*endp = '\0';

		/*
		 * Finished isolating origin id --- add it to list
		 */
		originid = (Oid) atoi(tok);
		*sl = lappend_oid(*sl, originid);

		/* Loop back if we didn't reach end of string */
	} while (!done);

	return true;
}

/*
 * Try to update progress and send a keepalive message if too many changes were
 * processed.
 *
 * For a large transaction, if we don't send any change to the downstream for a
 * long time (exceeds the wal_receiver_timeout of standby) then it can timeout.
 * This can happen when all or most of the changes are not published.
 *
 * Copied from Postgres commit f95d53eded55ecbf037f6416ced6af29a2c3caca
 */
#if PG_VERSION_NUM >= 150000 && PG_VERSION_NUM < 160000
static void
update_replication_progress(LogicalDecodingContext *ctx, bool skipped_xact)
{
	static int	changes_count = 0;

	/*
	 * We don't want to try sending a keepalive message after processing each
	 * change as that can have overhead. Tests revealed that there is no
	 * noticeable overhead in doing it after continuously processing 100 or so
	 * changes.
	 */
#define CHANGES_THRESHOLD 100

	/*
	 * If we are at the end of transaction LSN, update progress tracking.
	 * Otherwise, after continuously processing CHANGES_THRESHOLD changes, we
	 * try to send a keepalive message if required.
	 */
	if (ctx->end_xact || ++changes_count >= CHANGES_THRESHOLD)
	{
		OutputPluginUpdateProgress(ctx, skipped_xact);
		changes_count = 0;
	}
}
#elif PG_VERSION_NUM >= 100000 && PG_VERSION_NUM < 150000
static void
update_replication_progress(LogicalDecodingContext *ctx)
{
	static int	changes_count = 0;

	/*
	 * We don't want to try sending a keepalive message after processing each
	 * change as that can have overhead. Tests revealed that there is no
	 * noticeable overhead in doing it after continuously processing 100 or so
	 * changes.
	 */
#define CHANGES_THRESHOLD 100

	/*
	 * If we are at the end of transaction LSN, update progress tracking.
	 * Otherwise, after continuously processing CHANGES_THRESHOLD changes, we
	 * try to send a keepalive message if required.
	 */
	if (ctx->end_xact || ++changes_count >= CHANGES_THRESHOLD)
	{
		OutputPluginUpdateProgress(ctx);
		changes_count = 0;
	}
}
#endif

static void
yb_pgoutput_schema_change(LogicalDecodingContext *ctx, Oid relid)
{
	/* NOOP. */
}

static void
yb_support_yb_specific_replica_identity(bool support_yb_specific_replica_identity)
{
	/* NOOP. */
}
