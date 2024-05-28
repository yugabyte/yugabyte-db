/*--------------------------------------------------------------------------------------------------
 *
 * ybcplan.h
 *	  prototypes for ybcScan.c
 *
 * Copyright (c) YugaByte, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License
 * is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied.  See the License for the specific language governing permissions and limitations
 * under the License.
 *
 * src/include/executor/ybcplan.h
 *
 *--------------------------------------------------------------------------------------------------
 */

#pragma once

#include "postgres.h"
#include "access/relation.h"
#include "nodes/plannodes.h"
#include "utils/rel.h"
#include "commands/explain.h"

bool YBCIsSingleRowModify(PlannedStmt *pstmt);

bool YbCanSkipFetchingTargetTupleForModifyTable(ModifyTable *modifyTable);

bool YBCAllPrimaryKeysProvided(Relation rel, Bitmapset *attrs);

bool is_index_only_refs(List *colrefs, IndexOptInfo *indexinfo, bool bitmapindex);

void extract_pushdown_clauses(List *restrictinfo_list,
							  IndexOptInfo *indexinfo,
							  bool is_bitmap_index_scan,
							  List **local_quals,
							  List **rel_remote_quals,
							  List **rel_colrefs,
							  List **idx_remote_quals,
							  List **idx_colrefs);
