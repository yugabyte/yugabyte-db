/*-----------------------------------------------------------------------------
 * Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License.  You may obtain a copy
 * of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations under
 * the License.
 *
 *-----------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/genam.h"
#include "access/heapam.h"
#include "access/htup_details.h"
#include "catalog/indexing.h"
#include "catalog/pg_extension.h"
#include "catalog/pg_extension_d.h"
#include "executor/spi.h"
#include "extension_util.h"
#include "utils/builtins.h"
#include "utils/fmgroids.h"
#include "utils/lsyscache.h"
#include "utils/relcache.h"

const char *kManualReplicationErrorMsg =
"To manually replicate, run DDL on the source followed by the target with "
"SET yb_xcluster_ddl_replication.enable_manual_ddl_replication = true";

int64
GetInt64FromVariable(const char *var, const char *var_name)
{
	if (!var || strcmp(var, "") == 0)
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						errmsg("error parsing %s: %s", var_name, var)));

	char	   *endp = NULL;
	int64		ret = strtoll(var, &endp, 10);

	if (*endp != '\0')
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						errmsg("error parsing %s: %s", var_name, var)));

	return ret;
}

static Oid	CachedExtensionOwnerOid = InvalidOid;	/* Cached for a pg connection. */
Oid
XClusterExtensionOwner(void)
{
	if (CachedExtensionOwnerOid > InvalidOid)
		return CachedExtensionOwnerOid;

	Relation	extensionRelation = table_open(ExtensionRelationId,
											   AccessShareLock);

	ScanKeyData entry[1];

	ScanKeyInit(&entry[0], Anum_pg_extension_extname, BTEqualStrategyNumber,
				F_NAMEEQ, CStringGetDatum(EXTENSION_NAME));

	SysScanDesc scanDescriptor = systable_beginscan(extensionRelation,
													ExtensionNameIndexId, true,
													NULL, 1, entry);

	HeapTuple	extensionTuple = systable_getnext(scanDescriptor);

	if (!HeapTupleIsValid(extensionTuple))
	{
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("%s extension is not loaded", EXTENSION_NAME)));
	}

	Form_pg_extension extensionForm = (Form_pg_extension) GETSTRUCT(extensionTuple);
	Oid			extensionOwner = extensionForm->extowner;

	systable_endscan(scanDescriptor);
	table_close(extensionRelation, AccessShareLock);

	/* Cache this value for future calls. */
	CachedExtensionOwnerOid = extensionOwner;
	return extensionOwner;
}

Oid
SPI_GetOid(HeapTuple spi_tuple, int column_id)
{
	bool		is_null;
	Oid			oid = DatumGetObjectId(SPI_getbinval(spi_tuple, SPI_tuptable->tupdesc,
													 column_id, &is_null));

	if (is_null)
		elog(ERROR, "Found NULL value when parsing oid (column %d)", column_id);
	return oid;
}

Oid
SPI_GetOidIfExists(HeapTuple spi_tuple, int column_id)
{
	bool		is_null;
	Oid			oid = DatumGetObjectId(SPI_getbinval(spi_tuple, SPI_tuptable->tupdesc,
													 column_id, &is_null));
	if (is_null)
		return InvalidOid;
	return oid;
}

char *
SPI_GetText(HeapTuple spi_tuple, int column_id)
{
	return SPI_getvalue(spi_tuple, SPI_tuptable->tupdesc, column_id);
}

bool
SPI_GetBool(HeapTuple spi_tuple, int column_id)
{
	bool		is_null;
	bool		val = DatumGetBool(SPI_getbinval(spi_tuple, SPI_tuptable->tupdesc,
												 column_id, &is_null));

	if (is_null)
		elog(ERROR, "Found NULL value when parsing bool (column %d)", column_id);
	return val;
}

CollectedCommand *
GetCollectedCommand(HeapTuple spi_tuple, int column_id)
{
	bool		isnull;
	Pointer		command_datum = DatumGetPointer(SPI_getbinval(spi_tuple,
															  SPI_tuptable->tupdesc, column_id,
															  &isnull));

	if (isnull)
		elog(ERROR, "Found NULL value when parsing command (column %d)", column_id);
	return (CollectedCommand *) command_datum;
}

/*
 * elog's an ERROR if column column_id of spi_tuple does not hold a (possibly
 * NULL) text[] value.  Otherwise, examines that value and
 *
 *   - returns NULL if the the value does not have an element at index
 *     element_index
 *   - otherwise returns the element of the array at that index as a palloc'ed
 *     C string.
 */
char *
SPI_TextArrayGetElement(HeapTuple spi_tuple, int column_id, int element_index)
{
	bool		is_null;
	Datum		array_datum;
	ArrayType  *array;
	Oid			element_type;
	int16		typlen;
	bool		typbyval;
	char		typalign;
	Datum	   *elements;
	int			num_elements;
	char	   *result = NULL;

	array_datum = SPI_getbinval(spi_tuple, SPI_tuptable->tupdesc, column_id, &is_null);
	if (is_null)
		return NULL;

	array = DatumGetArrayTypeP(array_datum);
	element_type = ARR_ELEMTYPE(array);
	if (element_type != TEXTOID)
		elog(ERROR, "Expected text[] but found different type %u",
			 element_type);

	get_typlenbyvalalign(element_type, &typlen, &typbyval, &typalign);
	deconstruct_array(array, element_type, typlen, typbyval, typalign,
					  &elements, /* elog on NULL values */ NULL,
					  &num_elements);

	if (element_index >= 0 && element_index < num_elements)
	{
		result = pstrdup(TextDatumGetCString(elements[element_index]));
	}
	pfree(elements);

	return result;
}

bool
IsTempSchema(const char *schema_name)
{
	return schema_name && !strcmp(schema_name, "pg_temp");
}

Oid
GetColocationIdFromRelation(Relation *rel)
{
  YbcTableProperties table_props = YbTryGetTableProperties(*rel);
	if (!table_props || !table_props->is_colocated)
		return InvalidOid;

	return table_props->colocation_id;
}
