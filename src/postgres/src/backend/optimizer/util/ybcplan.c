/*--------------------------------------------------------------------------------------------------
 *
 * ybcplan.c
 *	  Utilities for YugaByte scan.
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
 * src/backend/executor/ybcplan.c
 *
 *--------------------------------------------------------------------------------------------------
 */


#include "postgres.h"

#include "nodes/nodes.h"
#include "nodes/plannodes.h"
#include "nodes/print.h"

#include "yb/yql/pggate/ybc_pggate.h"
#include "pg_yb_utils.h"

/*
 * Theoretically, any expression without side-effects.
 * Currently restrict this to only constants or bind markers.
 */
static bool IsSupportedWriteExpr(Expr *expr)
{
	switch (nodeTag(expr))
	{
		case T_Const:
			return true;
		case T_Param:
		{
			Param *param = (Param *) expr;
			return param->paramkind == PARAM_EXTERN;
		}
		case T_RelabelType:
		{
			/*
			 * RelabelType is a "dummy" type coercion between two binary-
			 * compatible datatypes so we just recurse into its argument.
			 */
			RelabelType *rt = (RelabelType *) expr;
			return IsSupportedWriteExpr(rt->arg);
		}
		default:
			break;
	}

	return false;
}

/*
 * Analyze the plan and add/set any YugaByte-specific fields.
 *
 * Currently only handles ModifyTable plans and sets ybIsSingleRowWrite to true
 * if the following are all true:
 *  - is insert command.
 *  - only one target table.
 *  - there are no ON CONFLICT or WITH clauses.
 *  - source data is a VALUES clause with one value set.
 *  - all values are either constants or bind markers.
 *
 *  Additionally, during execution we will also check.
 *  - not in transaction block.
 *  - is a single-plan execution
 *  - target table has no triggers.
 *  - target table has no indexes.
 *  And if all are true we will execute this op as a single-row transaction
 *  rather than a distributed transaction.
 */
void ybcAnalyzePlan(Plan *plan)
{
	switch (nodeTag(plan))
	{
		case T_ModifyTable:
		{
			ModifyTable *modifyTable = (ModifyTable *) plan;
			modifyTable->ybIsSingleRowWrite = false; /* default */

			/* Only support inserts for now */
			if (modifyTable->operation != CMD_INSERT)
				return;

			/* Multi-relation implies multi-shard. */
			if (list_length(modifyTable->resultRelations) != 1)
				return;

			/* ON CONFLICT clause is not supported here yet. */
			if (modifyTable->onConflictAction != ONCONFLICT_NONE)
				return;

			/* WITH clause is not supported here yet. */
			if (modifyTable->plan.initPlan != NIL)
				return;

			/* Check the data source, only allow a values clause right now */
			if (list_length(modifyTable->plans) != 1)
				return;

			switch nodeTag(linitial(modifyTable->plans))
			{
				case T_Result:
				{
					/* Simple values clause: one valueset (single row) */
					Result *values = (Result *)linitial(modifyTable->plans);
					ListCell *lc;
					foreach(lc, values->plan.targetlist)
					{
						TargetEntry *target = (TargetEntry *) lfirst(lc);
						if (!IsSupportedWriteExpr(target->expr))
							return;

					}
					break;
				}
				case T_ValuesScan:
				{
					/*
					 * Simple values clause: multiple valueset (multi-row).
					 * TODO Eventually we could inspect hash key values to check
					 * if single shard and optimize that.
					 */
					return;

				}
				default:
					/* Do not support any other data sources. */
					return;
			}

			/* If all our checks passed return true */
			modifyTable->ybIsSingleRowWrite = true;
			return;
		}
		default:
			return;
	}
}

bool ybcIsSingleRowModify(PlannedStmt *pstmt)
{
	if (pstmt->planTree && IsA(pstmt->planTree, ModifyTable))
	{
		ModifyTable *node = castNode(ModifyTable, pstmt->planTree);
		return node->ybIsSingleRowWrite;
	}

	return false;
}
