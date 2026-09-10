/* ----------
 * pg_yb_conn_mgr_protocol.h
 *
 * Constants shared between Postgres and YB Connection Manager
 * This file is also included by YSQL Connection Manager
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
 * src/common/pg_yb_conn_mgr_protocol.h
 * ----------
 */

#define YB_PARAM_STATUS_REPORT_ENABLED					(1 << 0)
#define YB_PARAM_STATUS_SOURCE_STARTUP					(1 << 1)
#define YB_PARAM_STATUS_USERSET_OR_SUSET_SOURCE_SESSION (1 << 2)
#define YB_PARAM_STATUS_DEFAULT_VAL_RESET				(1 << 3)
#define YB_PARAM_STATUS_SESSION_VAL_RESET				(1 << 4)

/* Constants used by Connection Manager and also required in Postgres */

#define YB_LOGICAL_CLIENT_VERSION_STR "yb_logical_client_version"

/* Values of the type field of the YbParse packet */

typedef enum YbParseType
{
	YB_PARSE_NORMAL,
	YB_PARSE_FORCE,
	YB_PARSE_REDEPLOY,
} YbParseType;

/*
 * Startup parameters in this namespace are set by YSQL Connection Manager.
 * External clients connecting through the connection manager must not be
 * allowed to provide them.
 */
#define YB_YCM_INTERNAL_STARTUP_PARAMETER_PREFIX "yb_ycm_internal_"

#define YB_YCM_USE_TSERVER_KEY_AUTH \
	YB_YCM_INTERNAL_STARTUP_PARAMETER_PREFIX "use_tserver_key_auth"
#define YB_YCM_IS_CLIENT_YSQLCONNMGR \
	YB_YCM_INTERNAL_STARTUP_PARAMETER_PREFIX "is_client_ysqlconnmgr"
#define YB_YCM_AUTHONLY \
	YB_YCM_INTERNAL_STARTUP_PARAMETER_PREFIX "authonly"
#define YB_YCM_IS_CONTROL_CONN \
	YB_YCM_INTERNAL_STARTUP_PARAMETER_PREFIX "is_control_conn"
#define YB_YCM_AUTH_REMOTE_HOST \
	YB_YCM_INTERNAL_STARTUP_PARAMETER_PREFIX "auth_remote_host"
#define YB_YCM_LOGICAL_CONN_TYPE \
	YB_YCM_INTERNAL_STARTUP_PARAMETER_PREFIX "logical_conn_type"

/*
 * YB: These GUCs are written by YSQL Connection Manager on every logical
 * client attach, to update pg_stat_activity with the logical client's
 * address/port.
 */
#define YB_YCM_CLIENT_ADDR \
	YB_YCM_INTERNAL_STARTUP_PARAMETER_PREFIX "client_addr"
#define YB_YCM_CLIENT_PORT \
	YB_YCM_INTERNAL_STARTUP_PARAMETER_PREFIX "client_port"
