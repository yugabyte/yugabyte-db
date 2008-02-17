#include "postgres.h"
#include "funcapi.h"
#include "assert.h"
#include "orafunc.h"
#include "miscadmin.h"
#include "utils/acl.h"	
#include "utils/builtins.h"
#include "utils/syscache.h"
#include "catalog/namespace.h"

Datum dbms_assert_enquote_literal(PG_FUNCTION_ARGS);
Datum dbms_assert_enquote_name(PG_FUNCTION_ARGS);
Datum dbms_assert_noop(PG_FUNCTION_ARGS);
Datum dbms_assert_qualified_sql_name(PG_FUNCTION_ARGS);
Datum dbms_assert_schema_name(PG_FUNCTION_ARGS);
Datum dbms_assert_simple_sql_name(PG_FUNCTION_ARGS);
Datum dbms_assert_object_name(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(dbms_assert_enquote_literal);
PG_FUNCTION_INFO_V1(dbms_assert_enquote_name);
PG_FUNCTION_INFO_V1(dbms_assert_noop);
PG_FUNCTION_INFO_V1(dbms_assert_qualified_sql_name);
PG_FUNCTION_INFO_V1(dbms_assert_schema_name);
PG_FUNCTION_INFO_V1(dbms_assert_simple_sql_name);
PG_FUNCTION_INFO_V1(dbms_assert_object_name);


#define CUSTOM_EXCEPTION(code, msg) \
	ereport(ERROR, \
		(errcode(ERRCODE_ORA_PACKAGES_##code), \
		 errmsg(msg)))

#define INVALID_SCHEMA_NAME_EXCEPTION()	\
	CUSTOM_EXCEPTION(INVALID_SCHEMA_NAME, "invalid schema name")

#define INVALID_OBJECT_NAME_EXCEPTION() \
	CUSTOM_EXCEPTION(INVALID_OBJECT_NAME, "invalid object name")

#define ISNOT_SIMPLE_SQL_NAME_EXCEPTION() \
	CUSTOM_EXCEPTION(ISNOT_SIMPLE_SQL_NAME, "string is not simple SQL name")

#define ISNOT_QUALIFIED_SQL_NAME_EXCEPTION() \
	CUSTOM_EXCEPTION(ISNOT_QUALIFIED_SQL_NAME, "string is not qualified SQL name")

#define EMPTY_STR(str)		((VARSIZE(str) - VARHDRSZ) == 0)


/****************************************************************
 * DBMS_ASSERT.ENQUOTE_LITERAL
 *
 * Syntax:
 *   FUNCTION ENQUOTE_LITERAL(str varchar) RETURNS varchar;
 *
 * Purpouse:
 *   Add leading and trailing quotes, verify that all single quotes
 *   are paired with adjacent single quotes.   
 *
 ****************************************************************/

Datum 
dbms_assert_enquote_literal(PG_FUNCTION_ARGS)
{
	PG_RETURN_DATUM(DirectFunctionCall1(quote_literal, PG_GETARG_DATUM(0)));
}


/****************************************************************
 * DBMS_ASSERT.ENQUOTE_NAME
 *
 * Syntax:
 *   FUNCTION ENQUOTE_NAME(str varchar) RETURNS varchar;
 *   FUNCTION ENQUOTE_NAME(str varchar, loweralize boolean := true) 
 *      RETURNS varchar;
 * Purpouse:
 *   Enclose name in double quotes.  
 * Atention!:
 *   On Oracle is second parameter capitalize!
 *
 ****************************************************************/

Datum 
dbms_assert_enquote_name(PG_FUNCTION_ARGS)
{
	Datum name  = PG_GETARG_DATUM(0);
    	bool loweralize = PG_GETARG_BOOL(1);

	name = DirectFunctionCall1(quote_ident, name);

	if (loweralize)
		name = DirectFunctionCall1(lower, name);

	PG_RETURN_DATUM(name);
}


/****************************************************************
 * DBMS_ASSERT.NOOP
 *
 * Syntax:
 *   FUNCTION NOOP(str varchar) RETURNS varchar;
 *
 * Purpouse:
 *   Returns value without any checking.
 *
 ****************************************************************/

Datum 
dbms_assert_noop(PG_FUNCTION_ARGS)
{
	text *str = PG_GETARG_TEXT_P(0);

	PG_RETURN_TEXT_P(TextPCopy(str));
}


/****************************************************************
 * DBMS_ASSERT.QUALIFIED_SQL_NAME
 *
 * Syntax:
 *   FUNCTION QUALIFIED_SQL_NAME(str varchar) RETURNS varchar;
 *
 * Purpouse:
 *   This function verifies that the input string is qualified SQL
 *   name.
 * Exception: 44004 string is not a qualified SQL name
 *
 ****************************************************************/

Datum 
dbms_assert_qualified_sql_name(PG_FUNCTION_ARGS)
{
	PG_RETURN_NULL();
}


/****************************************************************
 * DBMS_ASSERT.SCHEMA_NAME
 *
 * Syntax:
 *   FUNCTION SCHEMA_NAME(str varchar) RETURNS varchar;
 *
 * Purpouse:
 *   Function verifies that input string is an existing schema
 *   name.
 * Exception: 44001 Invalid schema name
 *
 ****************************************************************/

Datum 
dbms_assert_schema_name(PG_FUNCTION_ARGS)
{
	Oid			namespaceId;
	AclResult	aclresult;
	text *sname;	
	char *nspname;
	List	*names;

	if (PG_ARGISNULL(0))
		INVALID_SCHEMA_NAME_EXCEPTION();
	
	sname = PG_GETARG_TEXT_P(0);
	if (EMPTY_STR(sname))
		INVALID_SCHEMA_NAME_EXCEPTION();

	nspname = TextPGetCString(sname);
	names = stringToQualifiedNameList(nspname);
	if (list_length(names) != 1)
		INVALID_SCHEMA_NAME_EXCEPTION();

	namespaceId = GetSysCacheOid(NAMESPACENAME,
							CStringGetDatum(strVal(linitial(names))),
							0, 0, 0);
	if (!OidIsValid(namespaceId))
		INVALID_SCHEMA_NAME_EXCEPTION();
	
	aclresult = pg_namespace_aclcheck(namespaceId, GetUserId(), ACL_USAGE);
	if (aclresult != ACLCHECK_OK)
		INVALID_SCHEMA_NAME_EXCEPTION();

	PG_RETURN_TEXT_P(sname);
}


/****************************************************************
 * DBMS_ASSERT.SIMPLE_SQL_NAME
 *
 * Syntax:
 *   FUNCTION SIMPLE_SQL_NAME(str varchar) RETURNS varchar;
 *
 * Purpouse:
 *   This function verifies that the input string is simple SQL
 *   name.
 * Exception: 44003 String is not a simple SQL name  
 *
 ****************************************************************/

Datum 
dbms_assert_simple_sql_name(PG_FUNCTION_ARGS)
{
	PG_RETURN_NULL();
}


/****************************************************************
 * DBMS_ASSERT.OBJECT_NAME
 *
 * Syntax:
 *   FUNCTION OBJECT_NAME(str varchar) RETURNS varchar;
 *
 * Purpouse:
 *   Verifies that input string is qualified SQL identifier of
 *   an existing SQL object.
 * Exception: 44002 Invalid object name
 *
 ****************************************************************/

Datum 
dbms_assert_object_name(PG_FUNCTION_ARGS)
{
	List	*names;
	text	*str;
	char	*object_name;
	Oid 		classId;

	if (PG_ARGISNULL(0))
		INVALID_OBJECT_NAME_EXCEPTION();
	
	str = PG_GETARG_TEXT_P(0);
	if (EMPTY_STR(str))
		INVALID_OBJECT_NAME_EXCEPTION();

	object_name = TextPGetCString(str);

	names = stringToQualifiedNameList(object_name);

	classId = RangeVarGetRelid(makeRangeVarFromNameList(names), true);
	if (!OidIsValid(classId))
		INVALID_OBJECT_NAME_EXCEPTION();

	PG_RETURN_TEXT_P(str);
}
