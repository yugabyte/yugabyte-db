/*-------------------------------------------------------------------------
 *
 * ag_jsonbx.c
 *		I/O routines for jsonbx type
 *
 * Copyright (c) 2014-2018, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *	  ag_jsonbx.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "miscadmin.h"
#include "access/htup_details.h"
#include "access/transam.h"
#include "catalog/pg_type.h"
#include "funcapi.h"
#include "libpq/pqformat.h"
#include "parser/parse_coerce.h"
#include "utils/builtins.h"
#include "utils/date.h"
#include "utils/datetime.h"
#include "utils/lsyscache.h"
#include "utils/json.h"
#include "utils/jsonapi.h"
#include "utils/syscache.h"
#include "utils/typcache.h"

#include "ag_jsonbx.h"

typedef struct JsonbXInState
{
	JsonbXParseState *parseState;
	JsonbXValue *res;
} JsonbXInState;

/* unlike with json categories, we need to treat json and jsonbx differently */
typedef enum					/* type categories for datum_to_jsonbx */
{
	JSONBXTYPE_NULL,			/* null, so we didn't bother to identify */
	JSONBXTYPE_BOOL,			/* boolean (built-in types only) */
	JSONBXTYPE_NUMERIC,			/* numeric (ditto) */
	JSONBXTYPE_DATE,			/* we use special formatting for datetimes */
	JSONBXTYPE_TIMESTAMP,		/* we use special formatting for timestamp */
	JSONBXTYPE_TIMESTAMPTZ,		/* ... and timestamptz */
	JSONBXTYPE_JSON,			/* JSON */
	JSONBXTYPE_JSONB,			/* JSONB */
	JSONBXTYPE_ARRAY,			/* array */
	JSONBXTYPE_COMPOSITE,		/* composite */
	JSONBXTYPE_JSONCAST,		/* something with an explicit cast to JSON */
	JSONBXTYPE_OTHER			/* all else */
} JsonbXTypeCategory;

typedef struct JsonbXAggState
{
	JsonbXInState *res;
	JsonbXTypeCategory key_category;
	Oid			key_output_func;
	JsonbXTypeCategory val_category;
	Oid			val_output_func;
} JsonbXAggState;

static inline Datum jsonbx_from_cstring(char *jsonbx, int len);
static size_t checkStringLen(size_t len);
static void jsonbx_in_object_start(void *pstate);
static void jsonbx_in_object_end(void *pstate);
static void jsonbx_in_array_start(void *pstate);
static void jsonbx_in_array_end(void *pstate);
static void jsonbx_in_object_field_start(void *pstate, char *fname, bool isnull);
static void jsonbx_put_escaped_value(StringInfo out, JsonbXValue *scalarVal);
static void jsonbx_in_scalar(void *pstate, char *token, JsonTokenType tokentype);
static void jsonbx_categorize_type(Oid typoid,
					  JsonbXTypeCategory *tcategory,
					  Oid *outfuncoid);
static void composite_to_jsonbx(Datum composite, JsonbXInState *result);
static void array_dim_to_jsonbx(JsonbXInState *result, int dim, int ndims, int *dims,
				   Datum *vals, bool *nulls, int *valcount,
				   JsonbXTypeCategory tcategory, Oid outfuncoid);
static void array_to_jsonbx_internal(Datum array, JsonbXInState *result);
static void datum_to_jsonbx(Datum val, bool is_null, JsonbXInState *result,
			   JsonbXTypeCategory tcategory, Oid outfuncoid,
			   bool key_scalar);
static void add_jsonbx(Datum val, bool is_null, JsonbXInState *result,
		  Oid val_type, bool key_scalar);
static JsonbXParseState *clone_parse_state(JsonbXParseState *state);
static char *JsonbXToCStringWorker(StringInfo out, JsonbXContainer *in, int estimated_len, bool indent);
static void add_indent(StringInfo out, bool indent, int level);

/*
 * jsonbx type input function
 */
Datum
jsonbx_in(PG_FUNCTION_ARGS)
{
	char	   *jsonbx = PG_GETARG_CSTRING(0);

	return jsonbx_from_cstring(jsonbx, strlen(jsonbx));
}

/*
 * jsonbx type recv function
 *
 * The type is sent as text in binary mode, so this is almost the same
 * as the input function, but it's prefixed with a version number so we
 * can change the binary format sent in future if necessary. For now,
 * only version 1 is supported.
 */
Datum
jsonbx_recv(PG_FUNCTION_ARGS)
{
	StringInfo	buf = (StringInfo) PG_GETARG_POINTER(0);
	int			version = pq_getmsgint(buf, 1);
	char	   *str;
	int			nbytes;

	if (version == 1)
		str = pq_getmsgtext(buf, buf->len - buf->cursor, &nbytes);
	else
		elog(ERROR, "unsupported jsonbx version number %d", version);

	return jsonbx_from_cstring(str, nbytes);
}

/*
 * jsonbx type output function
 */
Datum
jsonbx_out(PG_FUNCTION_ARGS)
{
	JsonbX	   *jbx = PG_GETARG_JSONBX_P(0);
	char	   *out;

	out = JsonbXToCString(NULL, &jbx->root, VARSIZE(jbx));

	PG_RETURN_CSTRING(out);
}

/*
 * jsonbx type send function
 *
 * Just send jsonbx as a version number, then a string of text
 */
Datum
jsonbx_send(PG_FUNCTION_ARGS)
{
	JsonbX	   *jbx = PG_GETARG_JSONBX_P(0);
	StringInfoData buf;
	StringInfo	jtext = makeStringInfo();
	int			version = 1;

	(void) JsonbXToCString(jtext, &jbx->root, VARSIZE(jbx));

	pq_begintypsend(&buf);
	pq_sendint8(&buf, version);
	pq_sendtext(&buf, jtext->data, jtext->len);
	pfree(jtext->data);
	pfree(jtext);

	PG_RETURN_BYTEA_P(pq_endtypsend(&buf));
}

/*
 * SQL function jsonbx_typeof(jsonbx) -> text
 *
 * This function is here because the analog json function is in json.c, since
 * it uses the json parser internals not exposed elsewhere.
 */
Datum
jsonbx_typeof(PG_FUNCTION_ARGS)
{
	JsonbX	   *in = PG_GETARG_JSONBX_P(0);
	JsonbXIterator *it;
	JsonbXValue	v;
	char	   *result;

	if (JBX_ROOT_IS_OBJECT(in))
		result = "object";
	else if (JBX_ROOT_IS_ARRAY(in) && !JBX_ROOT_IS_SCALAR(in))
		result = "array";
	else
	{
		Assert(JBX_ROOT_IS_SCALAR(in));

		it = JsonbXIteratorInit(&in->root);

		/*
		 * A root scalar is stored as an array of one element, so we get the
		 * array and then its first (and only) member.
		 */
		(void) JsonbXIteratorNext(&it, &v, true);
		Assert(v.type == jbvXArray);
		(void) JsonbXIteratorNext(&it, &v, true);
		switch (v.type)
		{
			case jbvXNull:
				result = "null";
				break;
			case jbvXString:
				result = "string";
				break;
			case jbvXNumeric:
				result = "number";
				break;
			case jbvXBool:
				result = "boolean";
				break;
			default:
				elog(ERROR, "unknown jsonbx scalar type");
		}
	}

	PG_RETURN_TEXT_P(cstring_to_text(result));
}

/*
 * jsonbx_from_cstring
 *
 * Turns json string into a jsonbx Datum.
 *
 * Uses the json parser (with hooks) to construct a jsonbx.
 */
static inline Datum
jsonbx_from_cstring(char *json, int len)
{
	JsonLexContext *lex;
	JsonbXInState state;
	JsonSemAction sem;

	memset(&state, 0, sizeof(state));
	memset(&sem, 0, sizeof(sem));
	lex = makeJsonLexContextCstringLen(json, len, true);

	sem.semstate = (void *) &state;

	sem.object_start = jsonbx_in_object_start;
	sem.array_start = jsonbx_in_array_start;
	sem.object_end = jsonbx_in_object_end;
	sem.array_end = jsonbx_in_array_end;
	sem.scalar = jsonbx_in_scalar;
	sem.object_field_start = jsonbx_in_object_field_start;

	pg_parse_json(lex, &sem);

	/* after parsing, the item member has the composed jsonbx structure */
	PG_RETURN_POINTER(JsonbXValueToJsonbX(state.res));
}

static size_t
checkStringLen(size_t len)
{
	if (len > JENTRY_OFFLENMASK)
		ereport(ERROR,
				(errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
				 errmsg("string too long to represent as jsonbx string"),
				 errdetail("Due to an implementation restriction, jsonbx strings cannot exceed %d bytes.",
						   JENTRY_OFFLENMASK)));

	return len;
}

static void
jsonbx_in_object_start(void *pstate)
{
	JsonbXInState *_state = (JsonbXInState *) pstate;

	_state->res = pushJsonbXValue(&_state->parseState, WJBX_BEGIN_OBJECT, NULL);
}

static void
jsonbx_in_object_end(void *pstate)
{
	JsonbXInState *_state = (JsonbXInState *) pstate;

	_state->res = pushJsonbXValue(&_state->parseState, WJBX_END_OBJECT, NULL);
}

static void
jsonbx_in_array_start(void *pstate)
{
	JsonbXInState *_state = (JsonbXInState *) pstate;

	_state->res = pushJsonbXValue(&_state->parseState, WJBX_BEGIN_ARRAY, NULL);
}

static void
jsonbx_in_array_end(void *pstate)
{
	JsonbXInState *_state = (JsonbXInState *) pstate;

	_state->res = pushJsonbXValue(&_state->parseState, WJBX_END_ARRAY, NULL);
}

static void
jsonbx_in_object_field_start(void *pstate, char *fname, bool isnull)
{
	JsonbXInState *_state = (JsonbXInState *) pstate;
	JsonbXValue	v;

	Assert(fname != NULL);
	v.type = jbvXString;
	v.val.string.len = checkStringLen(strlen(fname));
	v.val.string.val = fname;

	_state->res = pushJsonbXValue(&_state->parseState, WJBX_KEY, &v);
}

static void
jsonbx_put_escaped_value(StringInfo out, JsonbXValue *scalarVal)
{
	switch (scalarVal->type)
	{
		case jbvXNull:
			appendBinaryStringInfo(out, "null", 4);
			break;
		case jbvXString:
			escape_json(out, pnstrdup(scalarVal->val.string.val, scalarVal->val.string.len));
			break;
		case jbvXNumeric:
			appendStringInfoString(out,
								   DatumGetCString(DirectFunctionCall1(numeric_out,
																	   PointerGetDatum(scalarVal->val.numeric))));
			break;
		case jbvXBool:
			if (scalarVal->val.boolean)
				appendBinaryStringInfo(out, "true", 4);
			else
				appendBinaryStringInfo(out, "false", 5);
			break;
		default:
			elog(ERROR, "unknown jsonbx scalar type");
	}
}

/*
 * For jsonbx we always want the de-escaped value - that's what's in token
 */
static void
jsonbx_in_scalar(void *pstate, char *token, JsonTokenType tokentype)
{
	JsonbXInState *_state = (JsonbXInState *) pstate;
	JsonbXValue	v;
	Datum		numd;

	switch (tokentype)
	{

		case JSON_TOKEN_STRING:
			Assert(token != NULL);
			v.type = jbvXString;
			v.val.string.len = checkStringLen(strlen(token));
			v.val.string.val = token;
			break;
		case JSON_TOKEN_NUMBER:

			/*
			 * No need to check size of numeric values, because maximum
			 * numeric size is well below the JsonbXValue restriction
			 */
			Assert(token != NULL);
			v.type = jbvXNumeric;
			numd = DirectFunctionCall3(numeric_in,
									   CStringGetDatum(token),
									   ObjectIdGetDatum(InvalidOid),
									   Int32GetDatum(-1));
			v.val.numeric = DatumGetNumeric(numd);
			break;
		case JSON_TOKEN_TRUE:
			v.type = jbvXBool;
			v.val.boolean = true;
			break;
		case JSON_TOKEN_FALSE:
			v.type = jbvXBool;
			v.val.boolean = false;
			break;
		case JSON_TOKEN_NULL:
			v.type = jbvXNull;
			break;
		default:
			/* should not be possible */
			elog(ERROR, "invalid json token type");
			break;
	}

	if (_state->parseState == NULL)
	{
		/* single scalar */
		JsonbXValue	va;

		va.type = jbvXArray;
		va.val.array.rawScalar = true;
		va.val.array.nElems = 1;

		_state->res = pushJsonbXValue(&_state->parseState, WJBX_BEGIN_ARRAY, &va);
		_state->res = pushJsonbXValue(&_state->parseState, WJBX_ELEM, &v);
		_state->res = pushJsonbXValue(&_state->parseState, WJBX_END_ARRAY, NULL);
	}
	else
	{
		JsonbXValue *o = &_state->parseState->contVal;

		switch (o->type)
		{
			case jbvXArray:
				_state->res = pushJsonbXValue(&_state->parseState, WJBX_ELEM, &v);
				break;
			case jbvXObject:
				_state->res = pushJsonbXValue(&_state->parseState, WJBX_VALUE, &v);
				break;
			default:
				elog(ERROR, "unexpected parent of nested structure");
		}
	}
}

/*
 * JsonbXToCString
 *	   Converts jsonbx value to a C-string.
 *
 * If 'out' argument is non-null, the resulting C-string is stored inside the
 * StringBuffer.  The resulting string is always returned.
 *
 * A typical case for passing the StringInfo in rather than NULL is where the
 * caller wants access to the len attribute without having to call strlen, e.g.
 * if they are converting it to a text* object.
 */
char *
JsonbXToCString(StringInfo out, JsonbXContainer *in, int estimated_len)
{
	return JsonbXToCStringWorker(out, in, estimated_len, false);
}

/*
 * same thing but with indentation turned on
 */
char *
JsonbXToCStringIndent(StringInfo out, JsonbXContainer *in, int estimated_len)
{
	return JsonbXToCStringWorker(out, in, estimated_len, true);
}

/*
 * common worker for above two functions
 */
static char *
JsonbXToCStringWorker(StringInfo out, JsonbXContainer *in, int estimated_len, bool indent)
{
	bool		first = true;
	JsonbXIterator *it;
	JsonbXValue	v;
	JsonbXIteratorToken type = WJBX_DONE;
	int			level = 0;
	bool		redo_switch = false;

	/* If we are indenting, don't add a space after a comma */
	int			ispaces = indent ? 1 : 2;

	/*
	 * Don't indent the very first item. This gets set to the indent flag at
	 * the bottom of the loop.
	 */
	bool		use_indent = false;
	bool		raw_scalar = false;
	bool		last_was_key = false;

	if (out == NULL)
		out = makeStringInfo();

	enlargeStringInfo(out, (estimated_len >= 0) ? estimated_len : 64);

	it = JsonbXIteratorInit(in);

	while (redo_switch ||
		   ((type = JsonbXIteratorNext(&it, &v, false)) != WJBX_DONE))
	{
		redo_switch = false;
		switch (type)
		{
			case WJBX_BEGIN_ARRAY:
				if (!first)
					appendBinaryStringInfo(out, ", ", ispaces);

				if (!v.val.array.rawScalar)
				{
					add_indent(out, use_indent && !last_was_key, level);
					appendStringInfoCharMacro(out, '[');
				}
				else
					raw_scalar = true;

				first = true;
				level++;
				break;
			case WJBX_BEGIN_OBJECT:
				if (!first)
					appendBinaryStringInfo(out, ", ", ispaces);

				add_indent(out, use_indent && !last_was_key, level);
				appendStringInfoCharMacro(out, '{');

				first = true;
				level++;
				break;
			case WJBX_KEY:
				if (!first)
					appendBinaryStringInfo(out, ", ", ispaces);
				first = true;

				add_indent(out, use_indent, level);

				/* json rules guarantee this is a string */
				jsonbx_put_escaped_value(out, &v);
				appendBinaryStringInfo(out, ": ", 2);

				type = JsonbXIteratorNext(&it, &v, false);
				if (type == WJBX_VALUE)
				{
					first = false;
					jsonbx_put_escaped_value(out, &v);
				}
				else
				{
					Assert(type == WJBX_BEGIN_OBJECT || type == WJBX_BEGIN_ARRAY);

					/*
					 * We need to rerun the current switch() since we need to
					 * output the object which we just got from the iterator
					 * before calling the iterator again.
					 */
					redo_switch = true;
				}
				break;
			case WJBX_ELEM:
				if (!first)
					appendBinaryStringInfo(out, ", ", ispaces);
				first = false;

				if (!raw_scalar)
					add_indent(out, use_indent, level);
				jsonbx_put_escaped_value(out, &v);
				break;
			case WJBX_END_ARRAY:
				level--;
				if (!raw_scalar)
				{
					add_indent(out, use_indent, level);
					appendStringInfoCharMacro(out, ']');
				}
				first = false;
				break;
			case WJBX_END_OBJECT:
				level--;
				add_indent(out, use_indent, level);
				appendStringInfoCharMacro(out, '}');
				first = false;
				break;
			default:
				elog(ERROR, "unknown jsonbx iterator token type");
		}
		use_indent = indent;
		last_was_key = redo_switch;
	}

	Assert(level == 0);

	return out->data;
}

static void
add_indent(StringInfo out, bool indent, int level)
{
	if (indent)
	{
		int			i;

		appendStringInfoCharMacro(out, '\n');
		for (i = 0; i < level; i++)
			appendBinaryStringInfo(out, "    ", 4);
	}
}


/*
 * Determine how we want to render values of a given type in datum_to_jsonbx.
 *
 * Given the datatype OID, return its JsonbXTypeCategory, as well as the type's
 * output function OID.  If the returned category is JSONBXTYPE_JSONCAST,
 * we return the OID of the relevant cast function instead.
 */
static void
jsonbx_categorize_type(Oid typoid,
					  JsonbXTypeCategory *tcategory,
					  Oid *outfuncoid)
{
	bool		typisvarlena;

	/* Look through any domain */
	typoid = getBaseType(typoid);

	*outfuncoid = InvalidOid;

	/*
	 * We need to get the output function for everything except date and
	 * timestamp types, booleans, array and composite types, json and jsonb,
	 * and non-builtin types where there's a cast to json. In this last case
	 * we return the oid of the cast function instead.
	 */

	switch (typoid)
	{
		case BOOLOID:
			*tcategory = JSONBXTYPE_BOOL;
			break;

		case INT2OID:
		case INT4OID:
		case INT8OID:
		case FLOAT4OID:
		case FLOAT8OID:
		case NUMERICOID:
			getTypeOutputInfo(typoid, outfuncoid, &typisvarlena);
			*tcategory = JSONBXTYPE_NUMERIC;
			break;

		case DATEOID:
			*tcategory = JSONBXTYPE_DATE;
			break;

		case TIMESTAMPOID:
			*tcategory = JSONBXTYPE_TIMESTAMP;
			break;

		case TIMESTAMPTZOID:
			*tcategory = JSONBXTYPE_TIMESTAMPTZ;
			break;

		case JSONBOID:
			*tcategory = JSONBXTYPE_JSONB;
			break;

		case JSONOID:
			*tcategory = JSONBXTYPE_JSON;
			break;

		default:
			/* Check for arrays and composites */
			if (OidIsValid(get_element_type(typoid)) || typoid == ANYARRAYOID
				|| typoid == RECORDARRAYOID)
				*tcategory = JSONBXTYPE_ARRAY;
			else if (type_is_rowtype(typoid))	/* includes RECORDOID */
				*tcategory = JSONBXTYPE_COMPOSITE;
			else
			{
				/* It's probably the general case ... */
				*tcategory = JSONBXTYPE_OTHER;

				/*
				 * but first let's look for a cast to json (note: not to
				 * jsonb) if it's not built-in.
				 */
				if (typoid >= FirstNormalObjectId)
				{
					Oid			castfunc;
					CoercionPathType ctype;

					ctype = find_coercion_pathway(JSONOID, typoid,
												  COERCION_EXPLICIT, &castfunc);
					if (ctype == COERCION_PATH_FUNC && OidIsValid(castfunc))
					{
						*tcategory = JSONBXTYPE_JSONCAST;
						*outfuncoid = castfunc;
					}
					else
					{
						/* not a cast type, so just get the usual output func */
						getTypeOutputInfo(typoid, outfuncoid, &typisvarlena);
					}
				}
				else
				{
					/* any other builtin type */
					getTypeOutputInfo(typoid, outfuncoid, &typisvarlena);
				}
				break;
			}
	}
}

/*
 * Turn a Datum into jsonbx, adding it to the result JsonbXInState.
 *
 * tcategory and outfuncoid are from a previous call to json_categorize_type,
 * except that if is_null is true then they can be invalid.
 *
 * If key_scalar is true, the value is stored as a key, so insist
 * it's of an acceptable type, and force it to be a jbvXString.
 */
static void
datum_to_jsonbx(Datum val, bool is_null, JsonbXInState *result,
			   JsonbXTypeCategory tcategory, Oid outfuncoid,
			   bool key_scalar)
{
	char	   *outputstr;
	bool		numeric_error;
	JsonbXValue	jb;
	bool		scalar_jsonbx = false;

	check_stack_depth();

	/* Convert val to a JsonbXValue in jb (in most cases) */
	if (is_null)
	{
		Assert(!key_scalar);
		jb.type = jbvXNull;
	}
	else if (key_scalar &&
			 (tcategory == JSONBXTYPE_ARRAY ||
			  tcategory == JSONBXTYPE_COMPOSITE ||
			  tcategory == JSONBXTYPE_JSON ||
			  tcategory == JSONBXTYPE_JSONB ||
			  tcategory == JSONBXTYPE_JSONCAST))
	{
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("key value must be scalar, not array, composite, or json")));
	}
	else
	{
		if (tcategory == JSONBXTYPE_JSONCAST)
			val = OidFunctionCall1(outfuncoid, val);

		switch (tcategory)
		{
			case JSONBXTYPE_ARRAY:
				array_to_jsonbx_internal(val, result);
				break;
			case JSONBXTYPE_COMPOSITE:
				composite_to_jsonbx(val, result);
				break;
			case JSONBXTYPE_BOOL:
				if (key_scalar)
				{
					outputstr = DatumGetBool(val) ? "true" : "false";
					jb.type = jbvXString;
					jb.val.string.len = strlen(outputstr);
					jb.val.string.val = outputstr;
				}
				else
				{
					jb.type = jbvXBool;
					jb.val.boolean = DatumGetBool(val);
				}
				break;
			case JSONBXTYPE_NUMERIC:
				outputstr = OidOutputFunctionCall(outfuncoid, val);
				if (key_scalar)
				{
					/* always quote keys */
					jb.type = jbvXString;
					jb.val.string.len = strlen(outputstr);
					jb.val.string.val = outputstr;
				}
				else
				{
					/*
					 * Make it numeric if it's a valid JSON number, otherwise
					 * a string. Invalid numeric output will always have an
					 * 'N' or 'n' in it (I think).
					 */
					numeric_error = (strchr(outputstr, 'N') != NULL ||
									 strchr(outputstr, 'n') != NULL);
					if (!numeric_error)
					{
						Datum		numd;

						jb.type = jbvXNumeric;
						numd = DirectFunctionCall3(numeric_in,
												   CStringGetDatum(outputstr),
												   ObjectIdGetDatum(InvalidOid),
												   Int32GetDatum(-1));
						jb.val.numeric = DatumGetNumeric(numd);
						pfree(outputstr);
					}
					else
					{
						jb.type = jbvXString;
						jb.val.string.len = strlen(outputstr);
						jb.val.string.val = outputstr;
					}
				}
				break;
			case JSONBXTYPE_DATE:
				jb.type = jbvXString;
				jb.val.string.val = JsonEncodeDateTime(NULL, val, DATEOID);
				jb.val.string.len = strlen(jb.val.string.val);
				break;
			case JSONBXTYPE_TIMESTAMP:
				jb.type = jbvXString;
				jb.val.string.val = JsonEncodeDateTime(NULL, val, TIMESTAMPOID);
				jb.val.string.len = strlen(jb.val.string.val);
				break;
			case JSONBXTYPE_TIMESTAMPTZ:
				jb.type = jbvXString;
				jb.val.string.val = JsonEncodeDateTime(NULL, val, TIMESTAMPTZOID);
				jb.val.string.len = strlen(jb.val.string.val);
				break;
			case JSONBXTYPE_JSONCAST:
			case JSONBXTYPE_JSON:
				{
					/* parse the json right into the existing result object */
					JsonLexContext *lex;
					JsonSemAction sem;
					text	   *json = DatumGetTextPP(val);

					lex = makeJsonLexContext(json, true);

					memset(&sem, 0, sizeof(sem));

					sem.semstate = (void *) result;

					sem.object_start = jsonbx_in_object_start;
					sem.array_start = jsonbx_in_array_start;
					sem.object_end = jsonbx_in_object_end;
					sem.array_end = jsonbx_in_array_end;
					sem.scalar = jsonbx_in_scalar;
					sem.object_field_start = jsonbx_in_object_field_start;

					pg_parse_json(lex, &sem);

				}
				break;
			case JSONBXTYPE_JSONB:
				{
					JsonbX	   *jsonbX = DatumGetJsonbXP(val);
					JsonbXIterator *it;

					it = JsonbXIteratorInit(&jsonbX->root);

					if (JBX_ROOT_IS_SCALAR(jsonbX))
					{
						(void) JsonbXIteratorNext(&it, &jb, true);
						Assert(jb.type == jbvXArray);
						(void) JsonbXIteratorNext(&it, &jb, true);
						scalar_jsonbx = true;
					}
					else
					{
						JsonbXIteratorToken type;

						while ((type = JsonbXIteratorNext(&it, &jb, false))
							   != WJBX_DONE)
						{
							if (type == WJBX_END_ARRAY || type == WJBX_END_OBJECT ||
								type == WJBX_BEGIN_ARRAY || type == WJBX_BEGIN_OBJECT)
								result->res = pushJsonbXValue(&result->parseState,
															 type, NULL);
							else
								result->res = pushJsonbXValue(&result->parseState,
															 type, &jb);
						}
					}
				}
				break;
			default:
				outputstr = OidOutputFunctionCall(outfuncoid, val);
				jb.type = jbvXString;
				jb.val.string.len = checkStringLen(strlen(outputstr));
				jb.val.string.val = outputstr;
				break;
		}
	}

	/* Now insert jb into result, unless we did it recursively */
	if (!is_null && !scalar_jsonbx &&
		tcategory >= JSONBXTYPE_JSON && tcategory <= JSONBXTYPE_JSONCAST)
	{
		/* work has been done recursively */
		return;
	}
	else if (result->parseState == NULL)
	{
		/* single root scalar */
		JsonbXValue	va;

		va.type = jbvXArray;
		va.val.array.rawScalar = true;
		va.val.array.nElems = 1;

		result->res = pushJsonbXValue(&result->parseState, WJBX_BEGIN_ARRAY, &va);
		result->res = pushJsonbXValue(&result->parseState, WJBX_ELEM, &jb);
		result->res = pushJsonbXValue(&result->parseState, WJBX_END_ARRAY, NULL);
	}
	else
	{
		JsonbXValue *o = &result->parseState->contVal;

		switch (o->type)
		{
			case jbvXArray:
				result->res = pushJsonbXValue(&result->parseState, WJBX_ELEM, &jb);
				break;
			case jbvXObject:
				result->res = pushJsonbXValue(&result->parseState,
											 key_scalar ? WJBX_KEY : WJBX_VALUE,
											 &jb);
				break;
			default:
				elog(ERROR, "unexpected parent of nested structure");
		}
	}
}

/*
 * Process a single dimension of an array.
 * If it's the innermost dimension, output the values, otherwise call
 * ourselves recursively to process the next dimension.
 */
static void
array_dim_to_jsonbx(JsonbXInState *result, int dim, int ndims, int *dims, Datum *vals,
				   bool *nulls, int *valcount, JsonbXTypeCategory tcategory,
				   Oid outfuncoid)
{
	int			i;

	Assert(dim < ndims);

	result->res = pushJsonbXValue(&result->parseState, WJBX_BEGIN_ARRAY, NULL);

	for (i = 1; i <= dims[dim]; i++)
	{
		if (dim + 1 == ndims)
		{
			datum_to_jsonbx(vals[*valcount], nulls[*valcount], result, tcategory,
						   outfuncoid, false);
			(*valcount)++;
		}
		else
		{
			array_dim_to_jsonbx(result, dim + 1, ndims, dims, vals, nulls,
							   valcount, tcategory, outfuncoid);
		}
	}

	result->res = pushJsonbXValue(&result->parseState, WJBX_END_ARRAY, NULL);
}

/*
 * Turn an array into JSON.
 */
static void
array_to_jsonbx_internal(Datum array, JsonbXInState *result)
{
	ArrayType  *v = DatumGetArrayTypeP(array);
	Oid			element_type = ARR_ELEMTYPE(v);
	int		   *dim;
	int			ndim;
	int			nitems;
	int			count = 0;
	Datum	   *elements;
	bool	   *nulls;
	int16		typlen;
	bool		typbyval;
	char		typalign;
	JsonbXTypeCategory tcategory;
	Oid			outfuncoid;

	ndim = ARR_NDIM(v);
	dim = ARR_DIMS(v);
	nitems = ArrayGetNItems(ndim, dim);

	if (nitems <= 0)
	{
		result->res = pushJsonbXValue(&result->parseState, WJBX_BEGIN_ARRAY, NULL);
		result->res = pushJsonbXValue(&result->parseState, WJBX_END_ARRAY, NULL);
		return;
	}

	get_typlenbyvalalign(element_type,
						 &typlen, &typbyval, &typalign);

	jsonbx_categorize_type(element_type,
						  &tcategory, &outfuncoid);

	deconstruct_array(v, element_type, typlen, typbyval,
					  typalign, &elements, &nulls,
					  &nitems);

	array_dim_to_jsonbx(result, 0, ndim, dim, elements, nulls, &count, tcategory,
					   outfuncoid);

	pfree(elements);
	pfree(nulls);
}

/*
 * Turn a composite / record into JSON.
 */
static void
composite_to_jsonbx(Datum composite, JsonbXInState *result)
{
	HeapTupleHeader td;
	Oid			tupType;
	int32		tupTypmod;
	TupleDesc	tupdesc;
	HeapTupleData tmptup,
			   *tuple;
	int			i;

	td = DatumGetHeapTupleHeader(composite);

	/* Extract rowtype info and find a tupdesc */
	tupType = HeapTupleHeaderGetTypeId(td);
	tupTypmod = HeapTupleHeaderGetTypMod(td);
	tupdesc = lookup_rowtype_tupdesc(tupType, tupTypmod);

	/* Build a temporary HeapTuple control structure */
	tmptup.t_len = HeapTupleHeaderGetDatumLength(td);
	tmptup.t_data = td;
	tuple = &tmptup;

	result->res = pushJsonbXValue(&result->parseState, WJBX_BEGIN_OBJECT, NULL);

	for (i = 0; i < tupdesc->natts; i++)
	{
		Datum		val;
		bool		isnull;
		char	   *attname;
		JsonbXTypeCategory tcategory;
		Oid			outfuncoid;
		JsonbXValue	v;
		Form_pg_attribute att = TupleDescAttr(tupdesc, i);

		if (att->attisdropped)
			continue;

		attname = NameStr(att->attname);

		v.type = jbvXString;
		/* don't need checkStringLen here - can't exceed maximum name length */
		v.val.string.len = strlen(attname);
		v.val.string.val = attname;

		result->res = pushJsonbXValue(&result->parseState, WJBX_KEY, &v);

		val = heap_getattr(tuple, i + 1, tupdesc, &isnull);

		if (isnull)
		{
			tcategory = JSONBXTYPE_NULL;
			outfuncoid = InvalidOid;
		}
		else
			jsonbx_categorize_type(att->atttypid, &tcategory, &outfuncoid);

		datum_to_jsonbx(val, isnull, result, tcategory, outfuncoid, false);
	}

	result->res = pushJsonbXValue(&result->parseState, WJBX_END_OBJECT, NULL);
	ReleaseTupleDesc(tupdesc);
}

/*
 * Append JSON text for "val" to "result".
 *
 * This is just a thin wrapper around datum_to_jsonbx.  If the same type will be
 * printed many times, avoid using this; better to do the jsonbx_categorize_type
 * lookups only once.
 */
static void
add_jsonbx(Datum val, bool is_null, JsonbXInState *result,
		  Oid val_type, bool key_scalar)
{
	JsonbXTypeCategory tcategory;
	Oid			outfuncoid;

	if (val_type == InvalidOid)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("could not determine input data type")));

	if (is_null)
	{
		tcategory = JSONBXTYPE_NULL;
		outfuncoid = InvalidOid;
	}
	else
		jsonbx_categorize_type(val_type,
							  &tcategory, &outfuncoid);

	datum_to_jsonbx(val, is_null, result, tcategory, outfuncoid, key_scalar);
}

/*
 * SQL function to_jsonbx(anyvalue)
 */
Datum
to_jsonbx(PG_FUNCTION_ARGS)
{
	Datum		val = PG_GETARG_DATUM(0);
	Oid			val_type = get_fn_expr_argtype(fcinfo->flinfo, 0);
	JsonbXInState result;
	JsonbXTypeCategory tcategory;
	Oid			outfuncoid;

	if (val_type == InvalidOid)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("could not determine input data type")));

	jsonbx_categorize_type(val_type,
						  &tcategory, &outfuncoid);

	memset(&result, 0, sizeof(JsonbXInState));

	datum_to_jsonbx(val, false, &result, tcategory, outfuncoid, false);

	PG_RETURN_POINTER(JsonbXValueToJsonbX(result.res));
}

/*
 * SQL function jsonbx_build_object(variadic "any")
 */
Datum
jsonbx_build_object(PG_FUNCTION_ARGS)
{
	int			nargs;
	int			i;
	JsonbXInState result;
	Datum	   *args;
	bool	   *nulls;
	Oid		   *types;

	/* build argument values to build the object */
	nargs = extract_variadic_args(fcinfo, 0, true, &args, &types, &nulls);

	if (nargs < 0)
		PG_RETURN_NULL();

	if (nargs % 2 != 0)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("argument list must have even number of elements"),
				 errhint("The arguments of jsonbx_build_object() must consist of alternating keys and values.")));

	memset(&result, 0, sizeof(JsonbXInState));

	result.res = pushJsonbXValue(&result.parseState, WJBX_BEGIN_OBJECT, NULL);

	for (i = 0; i < nargs; i += 2)
	{
		/* process key */
		if (nulls[i])
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("argument %d: key must not be null", i + 1)));

		add_jsonbx(args[i], false, &result, types[i], true);

		/* process value */
		add_jsonbx(args[i + 1], nulls[i + 1], &result, types[i + 1], false);
	}

	result.res = pushJsonbXValue(&result.parseState, WJBX_END_OBJECT, NULL);

	PG_RETURN_POINTER(JsonbXValueToJsonbX(result.res));
}

/*
 * degenerate case of jsonbx_build_object where it gets 0 arguments.
 */
Datum
jsonbx_build_object_noargs(PG_FUNCTION_ARGS)
{
	JsonbXInState result;

	memset(&result, 0, sizeof(JsonbXInState));

	(void) pushJsonbXValue(&result.parseState, WJBX_BEGIN_OBJECT, NULL);
	result.res = pushJsonbXValue(&result.parseState, WJBX_END_OBJECT, NULL);

	PG_RETURN_POINTER(JsonbXValueToJsonbX(result.res));
}

/*
 * SQL function jsonbx_build_array(variadic "any")
 */
Datum
jsonbx_build_array(PG_FUNCTION_ARGS)
{
	int			nargs;
	int			i;
	JsonbXInState result;
	Datum	   *args;
	bool	   *nulls;
	Oid		   *types;

	/* build argument values to build the array */
	nargs = extract_variadic_args(fcinfo, 0, true, &args, &types, &nulls);

	if (nargs < 0)
		PG_RETURN_NULL();

	memset(&result, 0, sizeof(JsonbXInState));

	result.res = pushJsonbXValue(&result.parseState, WJBX_BEGIN_ARRAY, NULL);

	for (i = 0; i < nargs; i++)
		add_jsonbx(args[i], nulls[i], &result, types[i], false);

	result.res = pushJsonbXValue(&result.parseState, WJBX_END_ARRAY, NULL);

	PG_RETURN_POINTER(JsonbXValueToJsonbX(result.res));
}

/*
 * degenerate case of jsonbx_build_array where it gets 0 arguments.
 */
Datum
jsonbx_build_array_noargs(PG_FUNCTION_ARGS)
{
	JsonbXInState result;

	memset(&result, 0, sizeof(JsonbXInState));

	(void) pushJsonbXValue(&result.parseState, WJBX_BEGIN_ARRAY, NULL);
	result.res = pushJsonbXValue(&result.parseState, WJBX_END_ARRAY, NULL);

	PG_RETURN_POINTER(JsonbXValueToJsonbX(result.res));
}

/*
 * SQL function jsonbx_object(text[])
 *
 * take a one or two dimensional array of text as name value pairs
 * for a jsonbx object.
 *
 */
Datum
jsonbx_object(PG_FUNCTION_ARGS)
{
	ArrayType  *in_array = PG_GETARG_ARRAYTYPE_P(0);
	int			ndims = ARR_NDIM(in_array);
	Datum	   *in_datums;
	bool	   *in_nulls;
	int			in_count,
				count,
				i;
	JsonbXInState result;

	memset(&result, 0, sizeof(JsonbXInState));

	(void) pushJsonbXValue(&result.parseState, WJBX_BEGIN_OBJECT, NULL);

	switch (ndims)
	{
		case 0:
			goto close_object;
			break;

		case 1:
			if ((ARR_DIMS(in_array)[0]) % 2)
				ereport(ERROR,
						(errcode(ERRCODE_ARRAY_SUBSCRIPT_ERROR),
						 errmsg("array must have even number of elements")));
			break;

		case 2:
			if ((ARR_DIMS(in_array)[1]) != 2)
				ereport(ERROR,
						(errcode(ERRCODE_ARRAY_SUBSCRIPT_ERROR),
						 errmsg("array must have two columns")));
			break;

		default:
			ereport(ERROR,
					(errcode(ERRCODE_ARRAY_SUBSCRIPT_ERROR),
					 errmsg("wrong number of array subscripts")));
	}

	deconstruct_array(in_array,
					  TEXTOID, -1, false, 'i',
					  &in_datums, &in_nulls, &in_count);

	count = in_count / 2;

	for (i = 0; i < count; ++i)
	{
		JsonbXValue	v;
		char	   *str;
		int			len;

		if (in_nulls[i * 2])
			ereport(ERROR,
					(errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
					 errmsg("null value not allowed for object key")));

		str = TextDatumGetCString(in_datums[i * 2]);
		len = strlen(str);

		v.type = jbvXString;

		v.val.string.len = len;
		v.val.string.val = str;

		(void) pushJsonbXValue(&result.parseState, WJBX_KEY, &v);

		if (in_nulls[i * 2 + 1])
		{
			v.type = jbvXNull;
		}
		else
		{
			str = TextDatumGetCString(in_datums[i * 2 + 1]);
			len = strlen(str);

			v.type = jbvXString;

			v.val.string.len = len;
			v.val.string.val = str;
		}

		(void) pushJsonbXValue(&result.parseState, WJBX_VALUE, &v);
	}

	pfree(in_datums);
	pfree(in_nulls);

close_object:
	result.res = pushJsonbXValue(&result.parseState, WJBX_END_OBJECT, NULL);

	PG_RETURN_POINTER(JsonbXValueToJsonbX(result.res));
}

/*
 * SQL function jsonbx_object(text[], text[])
 *
 * take separate name and value arrays of text to construct a jsonbx object
 * pairwise.
 */
Datum
jsonbx_object_two_arg(PG_FUNCTION_ARGS)
{
	ArrayType  *key_array = PG_GETARG_ARRAYTYPE_P(0);
	ArrayType  *val_array = PG_GETARG_ARRAYTYPE_P(1);
	int			nkdims = ARR_NDIM(key_array);
	int			nvdims = ARR_NDIM(val_array);
	Datum	   *key_datums,
			   *val_datums;
	bool	   *key_nulls,
			   *val_nulls;
	int			key_count,
				val_count,
				i;
	JsonbXInState result;

	memset(&result, 0, sizeof(JsonbXInState));

	(void) pushJsonbXValue(&result.parseState, WJBX_BEGIN_OBJECT, NULL);

	if (nkdims > 1 || nkdims != nvdims)
		ereport(ERROR,
				(errcode(ERRCODE_ARRAY_SUBSCRIPT_ERROR),
				 errmsg("wrong number of array subscripts")));

	if (nkdims == 0)
		goto close_object;

	deconstruct_array(key_array,
					  TEXTOID, -1, false, 'i',
					  &key_datums, &key_nulls, &key_count);

	deconstruct_array(val_array,
					  TEXTOID, -1, false, 'i',
					  &val_datums, &val_nulls, &val_count);

	if (key_count != val_count)
		ereport(ERROR,
				(errcode(ERRCODE_ARRAY_SUBSCRIPT_ERROR),
				 errmsg("mismatched array dimensions")));

	for (i = 0; i < key_count; ++i)
	{
		JsonbXValue	v;
		char	   *str;
		int			len;

		if (key_nulls[i])
			ereport(ERROR,
					(errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
					 errmsg("null value not allowed for object key")));

		str = TextDatumGetCString(key_datums[i]);
		len = strlen(str);

		v.type = jbvXString;

		v.val.string.len = len;
		v.val.string.val = str;

		(void) pushJsonbXValue(&result.parseState, WJBX_KEY, &v);

		if (val_nulls[i])
		{
			v.type = jbvXNull;
		}
		else
		{
			str = TextDatumGetCString(val_datums[i]);
			len = strlen(str);

			v.type = jbvXString;

			v.val.string.len = len;
			v.val.string.val = str;
		}

		(void) pushJsonbXValue(&result.parseState, WJBX_VALUE, &v);
	}

	pfree(key_datums);
	pfree(key_nulls);
	pfree(val_datums);
	pfree(val_nulls);

close_object:
	result.res = pushJsonbXValue(&result.parseState, WJBX_END_OBJECT, NULL);

	PG_RETURN_POINTER(JsonbXValueToJsonbX(result.res));
}

/*
 * shallow clone of a parse state, suitable for use in aggregate
 * final functions that will only append to the values rather than
 * change them.
 */
static JsonbXParseState *
clone_parse_state(JsonbXParseState *state)
{
	JsonbXParseState *result,
			   *icursor,
			   *ocursor;

	if (state == NULL)
		return NULL;

	result = palloc(sizeof(JsonbXParseState));
	icursor = state;
	ocursor = result;
	for (;;)
	{
		ocursor->contVal = icursor->contVal;
		ocursor->size = icursor->size;
		icursor = icursor->next;
		if (icursor == NULL)
			break;
		ocursor->next = palloc(sizeof(JsonbXParseState));
		ocursor = ocursor->next;
	}
	ocursor->next = NULL;

	return result;
}

/*
 * jsonbx_agg aggregate function
 */
Datum
jsonbx_agg_transfn(PG_FUNCTION_ARGS)
{
	MemoryContext oldcontext,
				aggcontext;
	JsonbXAggState *state;
	JsonbXInState elem;
	Datum		val;
	JsonbXInState *result;
	bool		single_scalar = false;
	JsonbXIterator *it;
	JsonbX	   *jbelem;
	JsonbXValue	v;
	JsonbXIteratorToken type;

	if (!AggCheckCallContext(fcinfo, &aggcontext))
	{
		/* cannot be called directly because of internal-type argument */
		elog(ERROR, "jsonbx_agg_transfn called in non-aggregate context");
	}

	/* set up the accumulator on the first go round */

	if (PG_ARGISNULL(0))
	{
		Oid			arg_type = get_fn_expr_argtype(fcinfo->flinfo, 1);

		if (arg_type == InvalidOid)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("could not determine input data type")));

		oldcontext = MemoryContextSwitchTo(aggcontext);
		state = palloc(sizeof(JsonbXAggState));
		result = palloc0(sizeof(JsonbXInState));
		state->res = result;
		result->res = pushJsonbXValue(&result->parseState,
									 WJBX_BEGIN_ARRAY, NULL);
		MemoryContextSwitchTo(oldcontext);

		jsonbx_categorize_type(arg_type, &state->val_category,
							  &state->val_output_func);
	}
	else
	{
		state = (JsonbXAggState *) PG_GETARG_POINTER(0);
		result = state->res;
	}

	/* turn the argument into jsonbx in the normal function context */

	val = PG_ARGISNULL(1) ? (Datum) 0 : PG_GETARG_DATUM(1);

	memset(&elem, 0, sizeof(JsonbXInState));

	datum_to_jsonbx(val, PG_ARGISNULL(1), &elem, state->val_category,
				   state->val_output_func, false);

	jbelem = JsonbXValueToJsonbX(elem.res);

	/* switch to the aggregate context for accumulation operations */

	oldcontext = MemoryContextSwitchTo(aggcontext);

	it = JsonbXIteratorInit(&jbelem->root);

	while ((type = JsonbXIteratorNext(&it, &v, false)) != WJBX_DONE)
	{
		switch (type)
		{
			case WJBX_BEGIN_ARRAY:
				if (v.val.array.rawScalar)
					single_scalar = true;
				else
					result->res = pushJsonbXValue(&result->parseState,
												 type, NULL);
				break;
			case WJBX_END_ARRAY:
				if (!single_scalar)
					result->res = pushJsonbXValue(&result->parseState,
												 type, NULL);
				break;
			case WJBX_BEGIN_OBJECT:
			case WJBX_END_OBJECT:
				result->res = pushJsonbXValue(&result->parseState,
											 type, NULL);
				break;
			case WJBX_ELEM:
			case WJBX_KEY:
			case WJBX_VALUE:
				if (v.type == jbvXString)
				{
					/* copy string values in the aggregate context */
					char	   *buf = palloc(v.val.string.len + 1);

					snprintf(buf, v.val.string.len + 1, "%s", v.val.string.val);
					v.val.string.val = buf;
				}
				else if (v.type == jbvXNumeric)
				{
					/* same for numeric */
					v.val.numeric =
						DatumGetNumeric(DirectFunctionCall1(numeric_uplus,
															NumericGetDatum(v.val.numeric)));
				}
				result->res = pushJsonbXValue(&result->parseState,
											 type, &v);
				break;
			default:
				elog(ERROR, "unknown jsonbx iterator token type");
		}
	}

	MemoryContextSwitchTo(oldcontext);

	PG_RETURN_POINTER(state);
}

Datum
jsonbx_agg_finalfn(PG_FUNCTION_ARGS)
{
	JsonbXAggState *arg;
	JsonbXInState result;
	JsonbX	   *out;

	/* cannot be called directly because of internal-type argument */
	Assert(AggCheckCallContext(fcinfo, NULL));

	if (PG_ARGISNULL(0))
		PG_RETURN_NULL();		/* returns null iff no input values */

	arg = (JsonbXAggState *) PG_GETARG_POINTER(0);

	/*
	 * We need to do a shallow clone of the argument in case the final
	 * function is called more than once, so we avoid changing the argument. A
	 * shallow clone is sufficient as we aren't going to change any of the
	 * values, just add the final array end marker.
	 */

	result.parseState = clone_parse_state(arg->res->parseState);

	result.res = pushJsonbXValue(&result.parseState,
								WJBX_END_ARRAY, NULL);

	out = JsonbXValueToJsonbX(result.res);

	PG_RETURN_POINTER(out);
}

/*
 * jsonbx_object_agg aggregate function
 */
Datum
jsonbx_object_agg_transfn(PG_FUNCTION_ARGS)
{
	MemoryContext oldcontext,
				aggcontext;
	JsonbXInState elem;
	JsonbXAggState *state;
	Datum		val;
	JsonbXInState *result;
	bool		single_scalar;
	JsonbXIterator *it;
	JsonbX	   *jbkey,
			   *jbval;
	JsonbXValue	v;
	JsonbXIteratorToken type;

	if (!AggCheckCallContext(fcinfo, &aggcontext))
	{
		/* cannot be called directly because of internal-type argument */
		elog(ERROR, "jsonbx_object_agg_transfn called in non-aggregate context");
	}

	/* set up the accumulator on the first go round */

	if (PG_ARGISNULL(0))
	{
		Oid			arg_type;

		oldcontext = MemoryContextSwitchTo(aggcontext);
		state = palloc(sizeof(JsonbXAggState));
		result = palloc0(sizeof(JsonbXInState));
		state->res = result;
		result->res = pushJsonbXValue(&result->parseState,
									 WJBX_BEGIN_OBJECT, NULL);
		MemoryContextSwitchTo(oldcontext);

		arg_type = get_fn_expr_argtype(fcinfo->flinfo, 1);

		if (arg_type == InvalidOid)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("could not determine input data type")));

		jsonbx_categorize_type(arg_type, &state->key_category,
							  &state->key_output_func);

		arg_type = get_fn_expr_argtype(fcinfo->flinfo, 2);

		if (arg_type == InvalidOid)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("could not determine input data type")));

		jsonbx_categorize_type(arg_type, &state->val_category,
							  &state->val_output_func);
	}
	else
	{
		state = (JsonbXAggState *) PG_GETARG_POINTER(0);
		result = state->res;
	}

	/* turn the argument into jsonbx in the normal function context */

	if (PG_ARGISNULL(1))
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("field name must not be null")));

	val = PG_GETARG_DATUM(1);

	memset(&elem, 0, sizeof(JsonbXInState));

	datum_to_jsonbx(val, false, &elem, state->key_category,
				   state->key_output_func, true);

	jbkey = JsonbXValueToJsonbX(elem.res);

	val = PG_ARGISNULL(2) ? (Datum) 0 : PG_GETARG_DATUM(2);

	memset(&elem, 0, sizeof(JsonbXInState));

	datum_to_jsonbx(val, PG_ARGISNULL(2), &elem, state->val_category,
				   state->val_output_func, false);

	jbval = JsonbXValueToJsonbX(elem.res);

	it = JsonbXIteratorInit(&jbkey->root);

	/* switch to the aggregate context for accumulation operations */

	oldcontext = MemoryContextSwitchTo(aggcontext);

	/*
	 * keys should be scalar, and we should have already checked for that
	 * above when calling datum_to_jsonbx, so we only need to look for these
	 * things.
	 */

	while ((type = JsonbXIteratorNext(&it, &v, false)) != WJBX_DONE)
	{
		switch (type)
		{
			case WJBX_BEGIN_ARRAY:
				if (!v.val.array.rawScalar)
					elog(ERROR, "unexpected structure for key");
				break;
			case WJBX_ELEM:
				if (v.type == jbvXString)
				{
					/* copy string values in the aggregate context */
					char	   *buf = palloc(v.val.string.len + 1);

					snprintf(buf, v.val.string.len + 1, "%s", v.val.string.val);
					v.val.string.val = buf;
				}
				else
				{
					ereport(ERROR,
							(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
							 errmsg("object keys must be strings")));
				}
				result->res = pushJsonbXValue(&result->parseState,
											 WJBX_KEY, &v);
				break;
			case WJBX_END_ARRAY:
				break;
			default:
				elog(ERROR, "unexpected structure for key");
				break;
		}
	}

	it = JsonbXIteratorInit(&jbval->root);

	single_scalar = false;

	/*
	 * values can be anything, including structured and null, so we treat them
	 * as in json_agg_transfn, except that single scalars are always pushed as
	 * WJBX_VALUE items.
	 */

	while ((type = JsonbXIteratorNext(&it, &v, false)) != WJBX_DONE)
	{
		switch (type)
		{
			case WJBX_BEGIN_ARRAY:
				if (v.val.array.rawScalar)
					single_scalar = true;
				else
					result->res = pushJsonbXValue(&result->parseState,
												 type, NULL);
				break;
			case WJBX_END_ARRAY:
				if (!single_scalar)
					result->res = pushJsonbXValue(&result->parseState,
												 type, NULL);
				break;
			case WJBX_BEGIN_OBJECT:
			case WJBX_END_OBJECT:
				result->res = pushJsonbXValue(&result->parseState,
											 type, NULL);
				break;
			case WJBX_ELEM:
			case WJBX_KEY:
			case WJBX_VALUE:
				if (v.type == jbvXString)
				{
					/* copy string values in the aggregate context */
					char	   *buf = palloc(v.val.string.len + 1);

					snprintf(buf, v.val.string.len + 1, "%s", v.val.string.val);
					v.val.string.val = buf;
				}
				else if (v.type == jbvXNumeric)
				{
					/* same for numeric */
					v.val.numeric =
						DatumGetNumeric(DirectFunctionCall1(numeric_uplus,
															NumericGetDatum(v.val.numeric)));
				}
				result->res = pushJsonbXValue(&result->parseState,
											 single_scalar ? WJBX_VALUE : type,
											 &v);
				break;
			default:
				elog(ERROR, "unknown jsonbx iterator token type");
		}
	}

	MemoryContextSwitchTo(oldcontext);

	PG_RETURN_POINTER(state);
}

Datum
jsonbx_object_agg_finalfn(PG_FUNCTION_ARGS)
{
	JsonbXAggState *arg;
	JsonbXInState result;
	JsonbX	   *out;

	/* cannot be called directly because of internal-type argument */
	Assert(AggCheckCallContext(fcinfo, NULL));

	if (PG_ARGISNULL(0))
		PG_RETURN_NULL();		/* returns null iff no input values */

	arg = (JsonbXAggState *) PG_GETARG_POINTER(0);

	/*
	 * We need to do a shallow clone of the argument's res field in case the
	 * final function is called more than once, so we avoid changing the
	 * aggregate state value.  A shallow clone is sufficient as we aren't
	 * going to change any of the values, just add the final object end
	 * marker.
	 */

	result.parseState = clone_parse_state(arg->res->parseState);

	result.res = pushJsonbXValue(&result.parseState,
								WJBX_END_OBJECT, NULL);

	out = JsonbXValueToJsonbX(result.res);

	PG_RETURN_POINTER(out);
}

/*
 * Extract scalar value from raw-scalar pseudo-array jsonbx.
 */
static bool
JsonbXExtractScalar(JsonbXContainer *jbc, JsonbXValue *res)
{
	JsonbXIterator *it;
	JsonbXIteratorToken tok PG_USED_FOR_ASSERTS_ONLY;
	JsonbXValue	tmp;

	if (!JsonbXContainerIsArray(jbc) || !JsonbXContainerIsScalar(jbc))
	{
		/* inform caller about actual type of container */
		res->type = (JsonbXContainerIsArray(jbc)) ? jbvXArray : jbvXObject;
		return false;
	}

	/*
	 * A root scalar is stored as an array of one element, so we get the array
	 * and then its first (and only) member.
	 */
	it = JsonbXIteratorInit(jbc);

	tok = JsonbXIteratorNext(&it, &tmp, true);
	Assert(tok == WJBX_BEGIN_ARRAY);
	Assert(tmp.val.array.nElems == 1 && tmp.val.array.rawScalar);

	tok = JsonbXIteratorNext(&it, res, true);
	Assert(tok == WJBX_ELEM);
	Assert(IsAJsonbXScalar(res));

	tok = JsonbXIteratorNext(&it, &tmp, true);
	Assert(tok == WJBX_END_ARRAY);

	tok = JsonbXIteratorNext(&it, &tmp, true);
	Assert(tok == WJBX_DONE);

	return true;
}

/*
 * Emit correct, translatable cast error message
 */
static void
cannotCastJsonbXValue(enum jbvXType type, const char *sqltype)
{
	static const struct
	{
		enum jbvXType type;
		const char *msg;
	}
				messages[] =
	{
		{jbvXNull, gettext_noop("cannot cast jsonbx null to type %s")},
		{jbvXString, gettext_noop("cannot cast jsonbx string to type %s")},
		{jbvXNumeric, gettext_noop("cannot cast jsonbx numeric to type %s")},
		{jbvXBool, gettext_noop("cannot cast jsonbx boolean to type %s")},
		{jbvXArray, gettext_noop("cannot cast jsonbx array to type %s")},
		{jbvXObject, gettext_noop("cannot cast jsonbx object to type %s")},
		{jbvXBinary, gettext_noop("cannot cast jsonbx array or object to type %s")}
	};
	int			i;

	for (i = 0; i < lengthof(messages); i++)
		if (messages[i].type == type)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg(messages[i].msg, sqltype)));

	/* should be unreachable */
	elog(ERROR, "unknown jsonbx type: %d", (int) type);
}

Datum
jsonbx_bool(PG_FUNCTION_ARGS)
{
	JsonbX	   *in = PG_GETARG_JSONBX_P(0);
	JsonbXValue	v;

	if (!JsonbXExtractScalar(&in->root, &v) || v.type != jbvXBool)
		cannotCastJsonbXValue(v.type, "boolean");

	PG_FREE_IF_COPY(in, 0);

	PG_RETURN_BOOL(v.val.boolean);
}

Datum
jsonbx_numeric(PG_FUNCTION_ARGS)
{
	JsonbX	   *in = PG_GETARG_JSONBX_P(0);
	JsonbXValue	v;
	Numeric		retValue;

	if (!JsonbXExtractScalar(&in->root, &v) || v.type != jbvXNumeric)
		cannotCastJsonbXValue(v.type, "numeric");

	/*
	 * v.val.numeric points into jsonbx body, so we need to make a copy to
	 * return
	 */
	retValue = DatumGetNumericCopy(NumericGetDatum(v.val.numeric));

	PG_FREE_IF_COPY(in, 0);

	PG_RETURN_NUMERIC(retValue);
}

Datum
jsonbx_int2(PG_FUNCTION_ARGS)
{
	JsonbX	   *in = PG_GETARG_JSONBX_P(0);
	JsonbXValue	v;
	Datum		retValue;

	if (!JsonbXExtractScalar(&in->root, &v) || v.type != jbvXNumeric)
		cannotCastJsonbXValue(v.type, "smallint");

	retValue = DirectFunctionCall1(numeric_int2,
								   NumericGetDatum(v.val.numeric));

	PG_FREE_IF_COPY(in, 0);

	PG_RETURN_DATUM(retValue);
}

Datum
jsonbx_int4(PG_FUNCTION_ARGS)
{
	JsonbX	   *in = PG_GETARG_JSONBX_P(0);
	JsonbXValue	v;
	Datum		retValue;

	if (!JsonbXExtractScalar(&in->root, &v) || v.type != jbvXNumeric)
		cannotCastJsonbXValue(v.type, "integer");

	retValue = DirectFunctionCall1(numeric_int4,
								   NumericGetDatum(v.val.numeric));

	PG_FREE_IF_COPY(in, 0);

	PG_RETURN_DATUM(retValue);
}

Datum
jsonbx_int8(PG_FUNCTION_ARGS)
{
	JsonbX	   *in = PG_GETARG_JSONBX_P(0);
	JsonbXValue	v;
	Datum		retValue;

	if (!JsonbXExtractScalar(&in->root, &v) || v.type != jbvXNumeric)
		cannotCastJsonbXValue(v.type, "bigint");

	retValue = DirectFunctionCall1(numeric_int8,
								   NumericGetDatum(v.val.numeric));

	PG_FREE_IF_COPY(in, 0);

	PG_RETURN_DATUM(retValue);
}

Datum
jsonbx_float4(PG_FUNCTION_ARGS)
{
	JsonbX	   *in = PG_GETARG_JSONBX_P(0);
	JsonbXValue	v;
	Datum		retValue;

	if (!JsonbXExtractScalar(&in->root, &v) || v.type != jbvXNumeric)
		cannotCastJsonbXValue(v.type, "real");

	retValue = DirectFunctionCall1(numeric_float4,
								   NumericGetDatum(v.val.numeric));

	PG_FREE_IF_COPY(in, 0);

	PG_RETURN_DATUM(retValue);
}

Datum
jsonbx_float8(PG_FUNCTION_ARGS)
{
	JsonbX	   *in = PG_GETARG_JSONBX_P(0);
	JsonbXValue	v;
	Datum		retValue;

	if (!JsonbXExtractScalar(&in->root, &v) || v.type != jbvXNumeric)
		cannotCastJsonbXValue(v.type, "double precision");

	retValue = DirectFunctionCall1(numeric_float8,
								   NumericGetDatum(v.val.numeric));

	PG_FREE_IF_COPY(in, 0);

	PG_RETURN_DATUM(retValue);
}
