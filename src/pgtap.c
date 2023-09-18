/*
 * PostgreSQL utility functions for pgTAP.
 */

#include "postgres.h"
#include "fmgr.h"
#include "funcapi.h"            /* for returning composite type */
#include "utils/builtins.h"     /* text_to_cstring() */
#include "parser/parse_type.h"  /* parseTypeString() */


#if PG_VERSION_NUM < 90300
#include "access/htup.h"         /* heap_form_tuple() */
#elif PG_VERSION_NUM < 130000
#include "access/htup_details.h" /* heap_form_tuple() */
#endif

#ifdef PG_MODULE_MAGIC
PG_MODULE_MAGIC;
#endif


/*
 * Given a string that is supposed to be a SQL-compatible type declaration,
 * such as "int4" or "integer" or "character varying(32)", parse
 * the string and convert it to a type OID and type modifier.
 *
 * Raises an error on an invalid type.
 */

/*
 * parse_type() is the inverse of pg_catalog.format_type(): it takes a string
 * representing an SQL-compatible type declaration, such as "int4" or "integer"
 * or "character varying(32)", parses it, and returns the OID and type modifier.
 * Returns NULL for an invalid type.
 *
 * Internally it relies on the Postgres core parseTypeString() function defined
 * in src/backend/parser/parse_type.c.
 */
Datum parse_type(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(parse_type);

Datum
parse_type(PG_FUNCTION_ARGS)
{
#define PARSE_TYPE_STRING_COLS 2 /* Returns two columns. */
    const char *type;            /* the type string we want to resolve */
    Oid         typid;           /* the resolved type oid */
    int32       typmod;          /* the resolved type modifier */
    TupleDesc   tupdesc;
    HeapTuple   rettuple;
    Datum       values[PARSE_TYPE_STRING_COLS] = {0};
    bool        nulls[PARSE_TYPE_STRING_COLS] = {0};

    type = text_to_cstring(PG_GETARG_TEXT_PP(0));

    /*
     * Build a tuple descriptor for our result type; return an error if not
     * called in a context that expects a record.
     */
    if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE) {
        ereport(
            ERROR,
            (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
            errmsg("function returning record called in context that cannot accept type record"))
        );
    }

    BlessTupleDesc(tupdesc);

	/*
	 * Parse type-name argument to obtain type OID and encoded typmod. We don't
	 * need to check for parseTypeString failure, but just let the error be
	 * raised. The 0 arg works both as the `Node *escontext` arg in Postgres 16
	 * and the `bool missing_ok` arg in 9.4-15.
	 */
#if PG_VERSION_NUM < 90400
    (void) parseTypeString(type, &typid, &typmod);
#else
    (void) parseTypeString(type, &typid, &typmod, 0);
#endif

    /* Create and return tuple. */
    values[0] = typid;
    values[1] = typmod;
    rettuple = heap_form_tuple(tupdesc, values, nulls);
    return HeapTupleGetDatum(rettuple);
#undef PARSE_TYPE_STRING_COLS
}
