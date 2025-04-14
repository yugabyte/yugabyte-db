/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/commands/data_table_utils.c
 *
 * Implementation of utility functions for data table.
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "metadata/metadata_cache.h"
#include "utils/query_utils.h"
#include "utils/data_table_utils.h"
#include "utils/version_utils_private.h"
#include "metadata/collection.h"
#include "api_hooks_def.h"

static inline bool IsUpdateForVersion(ExtensionVersion inputVersion,
									  ExtensionVersion expectedVersion);
PG_FUNCTION_INFO_V1(apply_extension_data_table_upgrade);


/*
 * apply_extension_data_table_upgrade - Alter the creation_time column of documents_<collection_id> table
 * to drop NOT NULL and DEFAULT constraints.
 */
Datum
apply_extension_data_table_upgrade(PG_FUNCTION_ARGS)
{
	int majorVersion = PG_GETARG_INT32(0);
	int minorVersion = PG_GETARG_INT32(1);
	int patch = PG_GETARG_INT32(2);

	ExtensionVersion inputVersion = { majorVersion, minorVersion, patch };
	ExtensionVersion expectedVersion = { 0, 102, 0 };

	if (ShouldUpgradeDataTables && IsUpdateForVersion(inputVersion, expectedVersion))
	{
		AlterCreationTime();
	}

	PG_RETURN_VOID();
}


/*
 * Gets the collection Ids where view_definition is NULL
 */
ArrayType *
GetCollectionIds()
{
	bool isNull = false;
	bool readOnly = true;
	StringInfo cmdStr = makeStringInfo();
	appendStringInfo(cmdStr,
					 "SELECT array_agg(DISTINCT collection_id)::bigint[] FROM %s.collections where view_definition IS NULL;",
					 ApiCatalogSchemaName);
	Datum versionDatum = ExtensionExecuteQueryViaSPI(cmdStr->data, readOnly,
													 SPI_OK_SELECT, &isNull);

	if (isNull)
	{
		return NULL;
	}

	return DatumGetArrayTypeP(versionDatum);
}


/*
 * core logic for alter the creation_time column of documents_<collection_id> table
 * to drop NOT NULL and DEFAULT constraints.
 */
void
AlterCreationTime()
{
	bool readOnly = false;
	bool isNull = false;

	ArrayType *arrayValue = GetCollectionIds();
	if (arrayValue == NULL)
	{
		return;
	}

	StringInfo cmdStr = makeStringInfo();
	Datum *elements = NULL;
	int numElements = 0;
	bool *val_is_null_marker;
	deconstruct_array(arrayValue, INT8OID, sizeof(int64), true, TYPALIGN_INT,
					  &elements, &val_is_null_marker, &numElements);

	for (int i = 0; i < numElements; i++)
	{
		int64_t collection_id = DatumGetInt64(elements[i]);
		resetStringInfo(cmdStr);
		appendStringInfo(cmdStr,
						 "ALTER TABLE IF EXISTS %s.documents_%ld ALTER COLUMN creation_time DROP NOT NULL, ALTER COLUMN creation_time DROP DEFAULT;",
						 ApiDataSchemaName, collection_id);
		ExtensionExecuteQueryViaSPI(cmdStr->data, readOnly, SPI_OK_UTILITY,
									&isNull);
	}
}


/*
 * Check if the input version is same as the expected version.
 */
static inline bool
IsUpdateForVersion(ExtensionVersion inputVersion,
				   ExtensionVersion expectedVersion)
{
	return (inputVersion.Major == expectedVersion.Major &&
			inputVersion.Minor == expectedVersion.Minor &&
			inputVersion.Patch == expectedVersion.Patch);
}
