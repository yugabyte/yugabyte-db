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

#ifndef YB_XCLUSTER_DDL_REPLICATION_UTIL
#define YB_XCLUSTER_DDL_REPLICATION_UTIL

#include "postgres.h"
#include "utils/guc.h"
#include "utils/memutils.h"

#define EXTENSION_NAME			   "yb_xcluster_ddl_replication"
#define DDL_QUEUE_TABLE_NAME	   "ddl_queue"
#define REPLICATED_DDLS_TABLE_NAME "replicated_ddls"

#define INIT_MEM_CONTEXT_AND_SPI_CONNECT(desc) \
	do \
	{ \
		context_new = AllocSetContextCreate(GetCurrentMemoryContext(), desc, \
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

// Handle old PG11 and newer PG15 code.
#if (PG_VERSION_NUM < 120000)
#define table_open(r, l)  heap_open(r, l)
#define table_close(r, l) heap_close(r, l)
#endif

typedef enum ClusterReplicationRole
{
	REPLICATION_ROLE_DISABLED,
	REPLICATION_ROLE_SOURCE,
	REPLICATION_ROLE_TARGET,
	REPLICATION_ROLE_BIDIRECTIONAL,
} ClusterReplicationRole;

static const struct config_enum_entry replication_roles[] = {
	{"DISABLED", REPLICATION_ROLE_DISABLED, false},
	{"SOURCE", REPLICATION_ROLE_SOURCE, false},
	{"TARGET", REPLICATION_ROLE_TARGET, false},
	{"BIDIRECTIONAL", REPLICATION_ROLE_BIDIRECTIONAL, /* hidden */ true},
	{NULL, 0, false}};

// Get int64 value from string extension variable.
int64 GetInt64FromVariable(const char *var, const char *var_name);

/*
 * XClusterExtensionOwner returns the oid of the user that owns the extension.
 * This is used in INIT_MEM_CONTEXT_AND_SPI_CONNECT to allow the extension to
 * update its objects.
 */
Oid XClusterExtensionOwner(void);

#endif
