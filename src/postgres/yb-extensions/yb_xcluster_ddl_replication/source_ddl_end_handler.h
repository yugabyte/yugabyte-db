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

#ifndef YB_XCLUSTER_DDL_REPLICATION_SOURCE_DDL_END
#define YB_XCLUSTER_DDL_REPLICATION_SOURCE_DDL_END

#include "postgres.h"
#include "utils/jsonb.h"

/*
 * Iterate over pg_catalog.pg_event_trigger_ddl_commands() and process each base
 * command (a single query may be composed of multiple base commands).
 *
 * There are three return cases for this function:
 * 1. The DDL contains some unsupported command - This function throws an error
 *    which aborts the transaction.
 * 2. The DDL is valid but does not need to be replicated (eg all the objects in
 *    the DDL are temp) - This function returns false.
 * 3. The DDL is valid and should be replicated - This function returns true.
 */
bool ProcessSourceEventTriggerDDLCommands(JsonbParseState *state);

#endif
