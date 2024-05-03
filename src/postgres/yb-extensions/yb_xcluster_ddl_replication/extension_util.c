// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not
// use this file except in compliance with the License.  You may obtain a copy
// of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
// License for the specific language governing permissions and limitations under
// the License.

#include "extension_util.h"
#include "access/genam.h"
#include "access/heapam.h"
#include "access/htup_details.h"
#include "catalog/indexing.h"
#include "catalog/pg_extension.h"
#include "catalog/pg_extension_d.h"
#include "utils/fmgroids.h"
#include "utils/relcache.h"

const char *kManualReplicationErrorMsg =
    "To manually replicate, run DDL with "
    "SET yb_xcluster_ddl_replication.enable_manual_ddl_replication = true";

int64
GetInt64FromVariable(const char *var, const char *var_name)
{
	if (!var || strcmp(var, "") == 0)
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						errmsg("Error parsing %s: %s", var_name, var)));

	char *endp = NULL;
	int64 ret = strtoll(var, &endp, 10);
	if (*endp != '\0')
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						errmsg("Error parsing %s: %s", var_name, var)));

	return ret;
}

static Oid CachedExtensionOwnerOid = 0; /* Cached for a pg connection. */
Oid
XClusterExtensionOwner(void)
{
	if (CachedExtensionOwnerOid > 0)
		return CachedExtensionOwnerOid;

	Relation extensionRelation =
		table_open(ExtensionRelationId, AccessShareLock);

	ScanKeyData entry[1];
	ScanKeyInit(&entry[0], Anum_pg_extension_extname, BTEqualStrategyNumber,
				F_NAMEEQ, CStringGetDatum(EXTENSION_NAME));

	SysScanDesc scanDescriptor = systable_beginscan(
		extensionRelation, ExtensionNameIndexId, true, NULL, 1, entry);

	HeapTuple extensionTuple = systable_getnext(scanDescriptor);
	if (!HeapTupleIsValid(extensionTuple))
	{
		ereport(ERROR, (errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
						errmsg("%s extension is not loaded", EXTENSION_NAME)));
	}

	Form_pg_extension extensionForm =
		(Form_pg_extension) GETSTRUCT(extensionTuple);
	Oid extensionOwner = extensionForm->extowner;

	systable_endscan(scanDescriptor);
	table_close(extensionRelation, AccessShareLock);

	// Cache this value for future calls.
	CachedExtensionOwnerOid = extensionOwner;
	return extensionOwner;
}