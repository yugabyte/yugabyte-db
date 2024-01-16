/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/oss_backend/commands/administration/get_parameter.c
 *
 * Implementation of the getParameter command.
 * For referrence: https://www.mongodb.com/docs/upcoming/reference/command/getParameter/#getparameter
 *-------------------------------------------------------------------------
 */
#include <postgres.h>
#include <funcapi.h>
#include <utils/array.h>
#include <utils/builtins.h>
#include <utils/guc.h>

#include "io/helio_bson_core.h"
#include "infrastructure/helio_external_configs.h"
#include "utils/query_utils.h"
#include "utils/list_utils.h"

#define SETTABLE_AT_RUNTIME_STRING_LENGTH 17
#define SETTABLE_AT_STARTUP_STRING_LENGTH 17

static const char *SETTABLE_AT_RUNTIME_STRING = "settableAtRuntime";
static const char *SETTABLE_AT_STARTUP_STRING = "settableAtStartup";
static const char *NO_OPTION_FOUND = "No option found for get";


/* --------------------------------------------------------- */
/* Data-types */
/* --------------------------------------------------------- */

/*
 * Parameter information structure.
 */
typedef struct
{
	char *name;
	bool canBeSetAtRuntime;
	bool canBeSetAtStartup;
} ExtensionParameterInfo;

static void WriteConfigurationInWriter(pgbson_writer *writer,
									   const ExtensionExternalConfigInfo *configInfo,
									   bool showParameterDetails);
static pgbson * GetNoOptionFoundResponse(void);

/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */

PG_FUNCTION_INFO_V1(command_get_parameter);

/*
 * command_get_parameter implements getParameter mongodb command for user facing configuration
 * parameters.
 *
 * There are multiple syntaxes for getPrameter command:
 *      1) { getParameter: 1, <parameter>: 1, ...} => This only returns the value of the parameter specified.
 *      2) { getParameter: { showDetails: true }, <parameter>: 1, ...} => This returns the value of the parameter and other details.
 *      3) { getParameter: "*", <parameter>: 1, ...} => This returns the value of all parameters,
 *         other parameters are ignored for processing but still included in complete list because of `*`.
 *      4) { getParameter: { allParameters: true, showDetails: true }} => This returns the value of all parameters and other details.
 */
Datum
command_get_parameter(PG_FUNCTION_ARGS)
{
	bool getAllParameters = PG_GETARG_BOOL(0);
	bool showParameterDetails = PG_GETARG_BOOL(1);
	ArrayType *parametersArray = PG_GETARG_ARRAYTYPE_P(2);

	bool **parameterListNulls = NULL;
	Datum *parameterList = NULL;
	int nParameterList = 0;
	ArrayExtractDatums(parametersArray, TEXTOID, &parameterList,
					   parameterListNulls, &nParameterList);

	pgbson_writer writer;
	PgbsonWriterInit(&writer);
	if (getAllParameters)
	{
		/* If all parameters are requested, then we ignore the list of parameters passed in the command */
		int totalExternalConfigs = GetExtensionExternalConfigCount();
		for (int i = 0; i < totalExternalConfigs; i++)
		{
			const ExtensionExternalConfigInfo *configInfo =
				GetExtensionExternalConfigByIndex(
					i);
			WriteConfigurationInWriter(&writer, configInfo, showParameterDetails);
		}
	}
	else
	{
		if (nParameterList == 0)
		{
			/* No parameters were passed in the command */
			PG_RETURN_POINTER(GetNoOptionFoundResponse());
		}

		/*
		 * sort the given parameter list so that response has parameters in sorted order.
		 */
		List *parameterStringList = NIL;
		for (int i = 0; i < nParameterList; i++)
		{
			parameterStringList = lappend(parameterStringList,
										  TextDatumGetCString(parameterList[i]));
		}
		SortStringList(parameterStringList);
		bool anyConfigFound = false;

		ListCell *stringListCell = NULL;
		foreach(stringListCell, parameterStringList)
		{
			const ExtensionExternalConfigInfo *configInfo =
				GetExtensionExternalConfigByName((char *) lfirst(stringListCell));
			if (configInfo != NULL)
			{
				anyConfigFound = true;
				WriteConfigurationInWriter(&writer, configInfo, showParameterDetails);
			}
		}
		if (!anyConfigFound)
		{
			/* No parameters were found for the request */
			PG_RETURN_POINTER(GetNoOptionFoundResponse());
		}
	}
	PgbsonWriterAppendInt32(&writer, "ok", 2, 1);
	PG_RETURN_POINTER(PgbsonWriterGetPgbson(&writer));
}


/*
 * Returns a pgbson response with `ok` set to false and `errmsg` set to `No option found for get`.
 */
static pgbson *
GetNoOptionFoundResponse(void)
{
	pgbson_writer writer;
	PgbsonWriterInit(&writer);
	PgbsonWriterAppendInt32(&writer, "ok", strlen("ok"), false);
	PgbsonWriterAppendUtf8(&writer, "errmsg", strlen("errmsg"), NO_OPTION_FOUND);
	return PgbsonWriterGetPgbson(&writer);
}


/*
 * Writes the config value in the response writer based on the type of the value.
 * and whether more parameter details are requested.
 */
static void
WriteConfigurationInWriter(pgbson_writer *writer,
						   const ExtensionExternalConfigInfo *configInfo,
						   bool showParameterDetails)
{
	Assert(writer != NULL && configInfo != NULL);

	bson_value_t typedValue = { 0 };

	/* Find the type and set the value accordingly. */
	if (configInfo->values.valueType == ConfigValueType_Bool)
	{
		typedValue.value_type = BSON_TYPE_BOOL;
		typedValue.value.v_bool = GetBoolValueFromConfig(configInfo);
	}
	else if (configInfo->values.valueType == ConfigValueType_Integer)
	{
		typedValue.value_type = BSON_TYPE_INT32;
		typedValue.value.v_int32 = GetIntegerValueFromConfig(configInfo);
	}
	else if (configInfo->values.valueType == ConfigValueType_Double)
	{
		typedValue.value_type = BSON_TYPE_DOUBLE;
		typedValue.value.v_double = GetDoubleValueFromConfig(configInfo);
	}
	else if (configInfo->values.valueType == ConfigValueType_String)
	{
		typedValue.value_type = BSON_TYPE_UTF8;
		char *value = GetStringValueFromConfig(configInfo);
		typedValue.value.v_utf8.str = value;
		typedValue.value.v_utf8.len = strlen(value);
	}

	if (showParameterDetails)
	{
		pgbson_writer moreDetailsWriter;
		PgbsonWriterStartDocument(writer, configInfo->name, strlen(configInfo->name),
								  &moreDetailsWriter);
		PgbsonWriterAppendValue(&moreDetailsWriter, "value", 5, &typedValue);
		PgbsonWriterAppendBool(&moreDetailsWriter, SETTABLE_AT_RUNTIME_STRING,
							   SETTABLE_AT_RUNTIME_STRING_LENGTH,
							   configInfo->settableProperties.isSettableAtRuntime);
		PgbsonWriterAppendBool(&moreDetailsWriter, SETTABLE_AT_STARTUP_STRING,
							   SETTABLE_AT_STARTUP_STRING_LENGTH,
							   configInfo->settableProperties.isSettableAtStartup);
		PgbsonWriterEndDocument(writer, &moreDetailsWriter);
	}
	else
	{
		PgbsonWriterAppendValue(writer, configInfo->name, strlen(configInfo->name),
								&typedValue);
	}
}
