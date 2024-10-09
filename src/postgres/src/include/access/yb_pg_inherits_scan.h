/*-------------------------------------------------------------------------
 *
 * yb_pg_inherits_scan.h
 *		This is an API used to abstract accesses to the pg_inherits catalog
 *		table.
 *
 * Copyright (c) YugaByte, Inc.
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
 *	  src/include/access/yb_access/yb_pg_inherits_scan.h
 *
 *-------------------------------------------------------------------------
 */

#ifndef YB_PG_INHERITS_SCAN_H
#define YB_PG_INHERITS_SCAN_H

#include "access/heapam.h"
#include "access/yb_sys_scan_base.h"

extern YbSysScanBase yb_pg_inherits_beginscan(
	Relation inhrelation, ScanKey key, int nkeys, Oid indexId);

#endif							/* YB_PG_INHERITS_SCAN_H */
