/*-------------------------------------------------------------------------
 *
 * yb_auth_passthrough.h
 * Utilities for Ysql Connection Manager/Yugabyte (Postgres layer) integration
 * that have to be defined on the Ysql Connection Manager (Odyssey) side.
 *
 * Copyright (c) YugaByteDB, Inc.
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
 * IDENTIFICATION
 *	  sources/yb_auth_passthrough.h
 *
 *-------------------------------------------------------------------------
 */

#ifndef YB_AUTH_PASSTHROUGH_H
#define YB_AUTH_PASSTHROUGH_H

#include <kiwi.h>
#include <argp.h>

extern int yb_auth_frontend_passthrough(od_client_t *, od_server_t *);

extern void yb_handle_fatalforlogicalconnection_pkt(od_client_t*, od_server_t*);

#endif /* YB_AUTH_PASSTHROUGH_H */
