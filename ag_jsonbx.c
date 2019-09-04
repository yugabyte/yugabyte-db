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
#include "catalog/pg_type.h"
#include "parser/parse_coerce.h"
#include "utils/builtins.h"
#include "utils/int8.h"
#include "utils/lsyscache.h"
#include "utils/json.h"
#include "utils/typcache.h"

#include "ag_jsonapi.h"
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

static inline Datum jsonbx_from_cstring(char *json, int len);
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
static char *JsonbXToCStringWorker(StringInfo out, JsonbXContainer *in, int estimated_len, bool indent);
static void add_indent(StringInfo out, bool indent, int level);

PG_FUNCTION_INFO_V1(jsonbx_in);

/*
 * jsonbx type input function
 */
Datum
jsonbx_in(PG_FUNCTION_ARGS)
{
	char	   *json = PG_GETARG_CSTRING(0);

	return jsonbx_from_cstring(json, strlen(json));
}

PG_FUNCTION_INFO_V1(jsonbx_out);

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
	lex = ag_makeJsonLexContextCstringLen(json, len, true);

	sem.semstate = (void *) &state;

	sem.object_start = jsonbx_in_object_start;
	sem.array_start = jsonbx_in_array_start;
	sem.object_end = jsonbx_in_object_end;
	sem.array_end = jsonbx_in_array_end;
	sem.scalar = jsonbx_in_scalar;
	sem.object_field_start = jsonbx_in_object_field_start;

	ag_parse_json(lex, &sem);

	/* after parsing, the item member has the composed jsonbx structure */
	PG_RETURN_POINTER(JsonbXValueToJsonbX(state.res));
}

static size_t
checkStringLen(size_t len)
{
	if (len > JXENTRY_OFFLENMASK)
		ereport(ERROR,
				(errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
				 errmsg("string too long to represent as jsonbx string"),
				 errdetail("Due to an implementation restriction, jsonbx strings cannot exceed %d bytes.",
						   JXENTRY_OFFLENMASK)));

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

		case jbvXInteger8:
			appendStringInfoString(out,
								   DatumGetCString(DirectFunctionCall1(int8out,
																	   PointerGetDatum(scalarVal->val.integer8))));
			break;

		case jbvXFloat8:
			//appendStringInfoString(out,
			//					   DatumGetCString(DirectFunctionCall1(float8out,
			//														   PointerGetDatum(scalarVal->val.float8))));
			appendStringInfoString(out, DatumGetCString(float8out_internal(scalarVal->val.float8)));
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
		case JSON_TOKEN_INTEGER8:
			Assert(token != NULL);
			v.type = jbvXInteger8;
			scanint8(token, false, &v.val.integer8);
			break;
		case JSON_TOKEN_FLOAT8:
			Assert(token != NULL);
			v.type = jbvXFloat8;
			v.val.float8 = float8in_internal(token, NULL, "double precision", token);
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
				jb.val.string.val = ag_JsonEncodeDateTime(NULL, val, DATEOID);
				jb.val.string.len = strlen(jb.val.string.val);
				break;
			case JSONBXTYPE_TIMESTAMP:
				jb.type = jbvXString;
				jb.val.string.val = ag_JsonEncodeDateTime(NULL, val, TIMESTAMPOID);
				jb.val.string.len = strlen(jb.val.string.val);
				break;
			case JSONBXTYPE_TIMESTAMPTZ:
				jb.type = jbvXString;
				jb.val.string.val = ag_JsonEncodeDateTime(NULL, val, TIMESTAMPTZOID);
				jb.val.string.len = strlen(jb.val.string.val);
				break;
			case JSONBXTYPE_JSONCAST:
			case JSONBXTYPE_JSON:
				{
					/* parse the json right into the existing result object */
					JsonLexContext *lex;
					JsonSemAction sem;
					text	   *json = DatumGetTextPP(val);

					lex = ag_makeJsonLexContext(json, true);

					memset(&sem, 0, sizeof(sem));

					sem.semstate = (void *) result;

					sem.object_start = jsonbx_in_object_start;
					sem.array_start = jsonbx_in_array_start;
					sem.object_end = jsonbx_in_object_end;
					sem.array_end = jsonbx_in_array_end;
					sem.scalar = jsonbx_in_scalar;
					sem.object_field_start = jsonbx_in_object_field_start;

					ag_parse_json(lex, &sem);

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
