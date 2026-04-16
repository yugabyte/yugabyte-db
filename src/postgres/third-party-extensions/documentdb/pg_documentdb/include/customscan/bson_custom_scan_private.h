/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/customscan/bson_custom_scan_private.h
 *
 *  Implementation of a custom scan plan.
 *
 *-------------------------------------------------------------------------
 */

#ifndef BSON_CUSTOM_SCAN_PRIVATE_H
#define BSON_CUSTOM_SCAN_PRIVATE_H
#include <nodes/readfuncs.h>


/* These macros are copied from Postgres for I/O of custom nodes */
#define nullable_string(token, length) \
	((length) == 0 ? NULL : debackslash(token, length))

#define booltostr(x) ((x) ? "true" : "false")

#define strtobool(x) ((*(x) == 't') ? true : false)

/* OID fields */
#define WRITE_OID_FIELD(fldname) \
	appendStringInfo(str, " :" CppAsString(fldname) " %u", node->fldname)
#define READ_OID_FIELD(fldname) \
	token = pg_strtok(&length);     /* skip :fldname */ \
	token = pg_strtok(&length);     /* Retrieve specified field value */ \
	local_node->fldname = atooid(token)

/* String fields */
#define WRITE_STRING_FIELD_VALUE(fldname, value) \
	(appendStringInfoString(str, " :" CppAsString(fldname) " "), \
	 outToken(str, value))

#define WRITE_STRING_FIELD(fldname) \
	(appendStringInfoString(str, " :" CppAsString(fldname) " "), \
	 outToken(str, node->fldname))

#define READ_STRING_FIELD(fldname) \
	token = pg_strtok(&length);     /* skip :fldname */ \
	token = pg_strtok(&length);     /* Retrieve specified field value */ \
	local_node->fldname = nullable_string(token, length)

#define READ_STRING_FIELD_VALUE(fldValue) \
	token = pg_strtok(&length);     /* skip :fldname */ \
	token = pg_strtok(&length);     /* Retrieve specified field value */ \
	fldValue = nullable_string(token, length)

/* Bool fields */
#define WRITE_BOOL_FIELD(fldname) \
	appendStringInfo(str, " :" CppAsString(fldname) " %s", \
					 booltostr(node->fldname))

#define READ_BOOL_FIELD(fldname) \
	token = pg_strtok(&length);     /* skip :fldname */ \
	token = pg_strtok(&length);     /* Retrieve specified field value */ \
	local_node->fldname = strtobool(token)

#endif
