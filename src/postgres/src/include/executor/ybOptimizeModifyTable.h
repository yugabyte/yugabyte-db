/*-------------------------------------------------------------------------
 *
 * ybOptimizeModifyTable.h
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 *use this file except in compliance with the License.  You may obtain a copy of
 *the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 *distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 *WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 *License for the specific language governing permissions and limitations under
 *the License.
 *
 * Portions Copyright (c) 1996-2018, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/executor/ybOptimizeModifyTable.h
 *
 *-------------------------------------------------------------------------
 */

#pragma once

#include "nodes/plannodes.h"

extern void YbComputeModifiedColumnsAndSkippableEntities(
	ModifyTable *plan, EState *estate, HeapTuple oldtuple,
	HeapTuple newtuple, Bitmapset **updatedCols,
	bool beforeRowUpdateTriggerFired);

extern bool YbIsPrimaryKeyUpdated(Relation rel, const Bitmapset *updated_cols);
