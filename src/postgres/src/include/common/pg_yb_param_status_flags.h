/* ----------
 * pg_yb_param_status_flags.h
 *
 * Values of flag bits that are sent by Postgres to YB Connection Manager
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
 * src/common/pg_yb_param_status_flags.h
 * ----------
 */

#define YB_PARAM_STATUS_REPORT_ENABLED					(1 << 0)
#define YB_PARAM_STATUS_SOURCE_STARTUP					(1 << 1)
#define YB_PARAM_STATUS_USERSET_OR_SUSET_SOURCE_SESSION (1 << 2)
#define YB_PARAM_STATUS_DEFAULT_VAL_RESET				(1 << 3)
#define YB_PARAM_STATUS_SESSION_VAL_RESET				(1 << 4)
