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
#include "nodes/relation.h"
#include "utils/rel.h"

#include "yb/yql/pggate/ybc_pggate.h"
#include "pg_yb_utils.h"

/*
 * Theoretically, any expression without side-effects.
 * Currently restrict this to only constants or bind markers.
 */
bool YBCIsSupportedSingleRowModifyWriteExpr(Expr *expr)
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
			return YBCIsSupportedSingleRowModifyWriteExpr(rt->arg);
		}
		default:
			break;
	}

	return false;
}

/*
 * Returns true if the following are all true:
 *  - is insert, update, or delete command.
 *  - only one target table.
 *  - there are no ON CONFLICT or WITH clauses.
 *  - source data is a VALUES clause with one value set.
 *  - all values are either constants or bind markers.
 *
 *  Additionally, during execution we will also check:
 *  - not in transaction block.
 *  - is a single-plan execution.
 *  - target table has no triggers.
 *  - target table has no indexes.
 *  And if all are true we will execute this op as a single-row transaction
 *  rather than a distributed transaction.
 */
static bool ModifyTableIsSingleRowWrite(ModifyTable *modifyTable)
{
	/* Support INSERT, UPDATE, and DELETE. */
	if (modifyTable->operation != CMD_INSERT &&
		modifyTable->operation != CMD_UPDATE &&
		modifyTable->operation != CMD_DELETE)
		return false;

	/* Multi-relation implies multi-shard. */
	if (list_length(modifyTable->resultRelations) != 1)
		return false;

	/* ON CONFLICT clause is not supported here yet. */
	if (modifyTable->onConflictAction != ONCONFLICT_NONE)
		return false;

	/* WITH clause is not supported here yet. */
	if (modifyTable->plan.initPlan != NIL)
		return false;

	/* Check the data source, only allow a values clause right now */
	if (list_length(modifyTable->plans) != 1)
		return false;

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
				if (!YBCIsSupportedSingleRowModifyWriteExpr(target->expr))
					return false;

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
			return false;

		}
		default:
			/* Do not support any other data sources. */
			return false;
	}

	/* If all our checks passed return true */
	return true;
}

bool YBCIsSingleRowModify(PlannedStmt *pstmt)
{
	if (pstmt->planTree && IsA(pstmt->planTree, ModifyTable))
	{
		ModifyTable *node = castNode(ModifyTable, pstmt->planTree);
		return ModifyTableIsSingleRowWrite(node);
	}

	return false;
}

/*
 * Returns true if the following are all true:
 *  - is update or delete command.
 *  - source data is a Result node (meaning we are skipping scan and thus
 *    are single row).
 */
bool YBCIsSingleRowUpdateOrDelete(ModifyTable *modifyTable)
{
	/* Support UPDATE and DELETE. */
	if (modifyTable->operation != CMD_UPDATE &&
		modifyTable->operation != CMD_DELETE)
		return false;

	/* Should only have one data source. */
	if (list_length(modifyTable->plans) != 1)
		return false;

	/* Verify the single data source is a Result node. */
	if (!IsA(linitial(modifyTable->plans), Result))
		return false;

	return true;
}

/*
 * Returns true if provided Bitmapset of attribute numbers
 * matches the primary key attribute numbers of the relation.
 */
bool YBCAllPrimaryKeysProvided(Oid relid, Bitmapset *attrs)
{
	if (bms_is_empty(attrs))
	{
		/*
		 * If we don't explicitly check for empty attributes it is possible
		 * for this function to improperly return true. This is because in the
		 * case where a table does not have any primary key attributes we will
		 * use a hidden RowId column which is not exposed to the PG side, so
		 * both the YB primary key attributes and the input attributes would
		 * appear empty and would be equal, even though this is incorrect as
		 * the YB table has the hidden RowId primary key column.
		 */
		return false;
	}

	Relation        rel                = RelationIdGetRelation(relid);
	Oid             dboid              = YBCGetDatabaseOid(rel);
	AttrNumber      natts              = RelationGetNumberOfAttributes(rel);
	YBCPgTableDesc  ybc_tabledesc      = NULL;
	Bitmapset      *primary_key_attrs  = NULL;

	/* Get primary key columns from YB table desc. */
	HandleYBStatus(YBCPgGetTableDesc(ybc_pg_session, dboid, relid, &ybc_tabledesc));
	for (AttrNumber attnum = 1; attnum <= natts; attnum++)
	{
		bool is_primary = false;
		bool is_hash    = false;
		HandleYBTableDescStatus(YBCPgGetColumnInfo(ybc_tabledesc,
		                                           attnum,
		                                           &is_primary,
		                                           &is_hash), ybc_tabledesc);

		if (is_primary)
		{
			primary_key_attrs = bms_add_member(primary_key_attrs, attnum);
		}
	}
	HandleYBStatus(YBCPgDeleteTableDesc(ybc_tabledesc));

	RelationClose(rel);

	/* Verify the sets are the same. */
	return bms_equal(attrs, primary_key_attrs);
}
