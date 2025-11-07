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

#ifndef YB_XCLUSTER_DDL_REPLICATION_UTIL
#define YB_XCLUSTER_DDL_REPLICATION_UTIL

#include "postgres.h"

#include "tcop/deparse_utility.h"
#include "utils/memutils.h"

#define EXTENSION_NAME			   "yb_xcluster_ddl_replication"
#define DDL_QUEUE_TABLE_NAME	   "ddl_queue"
#define REPLICATED_DDLS_TABLE_NAME "replicated_ddls"

#define INIT_MEM_CONTEXT_AND_SPI_CONNECT(desc) \
	do \
	{ \
		context_new = AllocSetContextCreate(CurrentMemoryContext, desc, \
											ALLOCSET_DEFAULT_SIZES); \
		context_old = MemoryContextSwitchTo(context_new); \
		GetUserIdAndSecContext(&save_userid, &save_sec_context); \
		SetUserIdAndSecContext(XClusterExtensionOwner(), \
							   SECURITY_RESTRICTED_OPERATION); \
		if (SPI_connect() != SPI_OK_CONNECT) \
			elog(ERROR, "SPI_connect failed"); \
	} while (false)

#define CLOSE_MEM_CONTEXT_AND_SPI \
	do \
	{ \
		if (SPI_finish() != SPI_OK_FINISH) \
			elog(ERROR, "SPI_finish() failed"); \
		SetUserIdAndSecContext(save_userid, save_sec_context); \
		MemoryContextSwitchTo(context_old); \
		MemoryContextDelete(context_new); \
	} while (false)

/* Handle old PG11 and newer PG15 code. */
#if (PG_VERSION_NUM < 120000)
#define table_open(r, l)  heap_open(r, l)
#define table_close(r, l) heap_close(r, l)
#endif

/* Global variables. */
extern const char *kManualReplicationErrorMsg;

/* Get int64 value from string extension variable. */
extern int64 GetInt64FromVariable(const char *var, const char *var_name);

/*
 * XClusterExtensionOwner returns the oid of the user that owns the extension.
 * This is used in INIT_MEM_CONTEXT_AND_SPI_CONNECT to allow the extension to
 * update its objects.
 */
extern Oid	XClusterExtensionOwner(void);

extern Oid	SPI_GetOid(HeapTuple spi_tuple, int column_id);

/* Returns InvalidOid (0) if value doesn't exist/is null. */
extern Oid	SPI_GetOidIfExists(HeapTuple spi_tuple, int column_id);

extern char *SPI_GetText(HeapTuple spi_tuple, int column_id);

extern bool SPI_GetBool(HeapTuple spi_tuple, int column_id);

extern CollectedCommand *GetCollectedCommand(HeapTuple spi_tuple, int column_id);

extern char *SPI_TextArrayGetElement(HeapTuple spi_tuple, int column_id,
									 int element_index);

/* If true, any object in this schema is a temporary object. */
extern bool IsTempSchema(const char *schema_name);

extern bool IsTemporaryPolicy(Oid policy_oid);

extern bool IsTemporaryTrigger(Oid trigger_oid);

extern bool IsTemporaryRule(Oid rule_oid);

/* Returns the relation's colocation id or InvalidOid (0) if not colocated. */
extern Oid	GetColocationIdFromRelation(Relation *rel, bool is_table_rewrite);

extern char *get_typname(Oid pg_type_oid);

extern bool IsExtensionDdl(CommandTag command_tag);

#endif
