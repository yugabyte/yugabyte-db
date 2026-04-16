/* ----------
 * yb_ysql_conn_mgr_helper.h
 *
 * Utilities for Ysql Connection Manager/Yugabyte (Postgres layer) integration
 * that have to be defined on the PostgreSQL side.
 *
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
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 * src/include/yb_ysql_conn_mgr_helper.h
 * ----------
 */
#include "postgres.h"

#include <ctype.h>
#include <float.h>

#include "miscadmin.h"

#pragma once

/*
 * `yb_is_auth_backend` is used to identify if the backend is spawned just for
 * authentication purposes.
 */
extern bool yb_is_auth_backend;

/*
 * `yb_is_client_ysqlconnmgr` is used to identify that the current connection is
 * created by a Ysql Connection Manager.
 */
extern bool yb_is_client_ysqlconnmgr;

/*
 * `yb_is_parallel_worker` is used to identify that whether background worker is
 * of parallel worker type.
*/
extern bool yb_is_parallel_worker;

/* TODO (janand): Write a function to read/change yb_logical_client_shmem_key */
extern int	yb_logical_client_shmem_key;

/*
 * `ysql_conn_mgr_sticky_object_count` is the count of the database objects
 * that require a sticky connection.
 */
extern int	ysql_conn_mgr_sticky_object_count;

/*
 * `yb_ysql_conn_mgr_sticky_guc` is used to denote stickiness of a connection
 * due to the setting of GUC variables that cannot be directly supported
 * by Connection Manager.
 */
extern bool yb_ysql_conn_mgr_sticky_guc;

/*
 * `yb_ysql_conn_mgr_sticky_locks` is used to denote stickiness of a connection
 * due to the setting of session-scoped advisory locks that cannot be directly
 * supported by Connection Manager.
 */
extern bool yb_ysql_conn_mgr_sticky_locks;

/*
 * Check whether the connection is made from Ysql Connection Manager.
 */
extern bool YbIsClientYsqlConnMgr();

/*
 * Add/update the changed session parameters in the shared memory.
 */
extern void YbUpdateSharedMemory();

/*
 * Clean the local list of names of changed session parameters.
 */
extern void YbCleanChangedSessionParameters();

/*
 * Add a name of session parameter to list of changed session parameters.
 */
extern void YbAddToChangedSessionParametersList(const char *parameter_name);

/*
 * Process the `SET SESSION PARAMETER` packet.
 * NOTE: Input `yb_client_id` can not be 0.
 * If yb_client_id < 0 then delete the associated shared memory segment.
 * else load the context of the associated client from the shared memory.
 */
extern void YbHandleSetSessionParam(int yb_client_id);

/*
 * Create the shared memory segment and send the shmem key to the client
 * connection as a HINT.
 *
 * NOTE: This function is only to be called during the authentication of a
 *       logical connection via the YSQL Connection Manager.
 * The authentication can happen via the `AUTHENTICATION PASSTHROUGH REQUEST`
 * packet or the lightweight authentication backend.
 */
extern int  YbCreateClientId();
extern void YbCreateClientIdWithDatabaseOid(Oid database_oid);

extern void YbSetUserContext(const Oid roleid, const bool is_superuser, const char *rname);

extern bool yb_is_client_ysqlconnmgr_check_hook(bool *newval, void **extra,
												GucSource source);

extern void YbSendFatalForLogicalConnectionPacket();

extern bool YbGetNumYsqlConnMgrConnections(const Oid db_oid,
										   const Oid user_oid,
										   uint32_t *num_logical_conn,
										   uint32_t *num_physical_conn);

extern void yb_is_client_ysqlconnmgr_assign_hook(bool newval, void *extra);

extern void YbSendParameterStatusForConnectionManager(const char *name, const char *value);

extern int YbAuthFailedErrorLevel(const bool auth_passthrough);

/*
 * Check whether an Auth Passthrough authentication is in progress.
 * Usually expects MyProcPort as the passed arg in Auth Passthrough code flow.
 * Only returns true if all of the following are true:
 * 1) Connection Manager is active
 * 2) `port` is initialized
 * 3) `port->yb_is_auth_passthrough_req` is `true` (authentication is in
 *    progress)
 */
extern bool YbIsAuthPassthroughInProgress(struct Port *port);
