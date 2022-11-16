// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

#pragma once

#include <memory>

#include "yb/util/strongly_typed_bool.h"

namespace yb {
namespace ql {

class ColumnArg;
class ColumnDesc;
class ColumnOp;
class FuncOp;
class IdxPredicateState;
class IfExprState;
class JsonColumnArg;
class JsonColumnOp;
class PTAllColumns;
class PTAlterKeyspace;
class PTAlterColumnDefinition;
class PTAlterRole;
class PTAlterTable;
class PTBaseType;
class PTBcall;
class PTBindVar;
class PTCollectionExpr;
class PTColumnDefinition;
class PTCommit;
class PTCreateIndex;
class PTCreateKeyspace;
class PTCreateRole;
class PTCreateTable;
class PTCreateType;
class PTDeleteStmt;
class PTDmlStmt;
class PTDmlUsingClause;
class PTDmlUsingClauseElement;
class PTDropStmt;
class PTExplainStmt;
class PTExpr;
class PTGrantRevokePermission;
class PTGrantRevokeRole;
class PTIndexColumn;
class PTInsertJsonClause;
class PTInsertStmt;
class PTJsonColumnWithOperators;
class PTJsonOperator;
class PTListNode;
class PTLiteralString;
class PTLogicExpr;
class PTOperatorExpr;
class PTQualifiedName;
class PTRef;
class PTRelationExpr;
class PTSelectStmt;
class PTStartTransaction;
class PTSubscriptedColumn;
class PTTablePropertyListNode;
class PTTruncateStmt;
class PTTypeField;
class PTUpdateStmt;
class PTUseKeyspace;
class ParseTree;
class PartitionKeyOp;
class ProcessContextBase;
class SelectScanInfo;
class SemContext;
class SemState;
class SubscriptedColumnArg;
class SubscriptedColumnOp;
class TreeNode;
class WhereExprState;
class YBLocation;

template<typename NodeType = TreeNode>
class TreeListNode;

using ParseTreePtr = std::unique_ptr<ParseTree>;
using PTBaseTypePtr = std::shared_ptr<PTBaseType>;
using PTBcallPtr = std::shared_ptr<PTBcall>;
using PTDmlUsingClauseElementPtr = std::shared_ptr<PTDmlUsingClauseElement>;
using PTDmlUsingClausePtr = std::shared_ptr<PTDmlUsingClause>;
using PTExprListNode = TreeListNode<PTExpr>;
using PTExprListNodePtr = std::shared_ptr<PTExprListNode>;
using PTExprPtr = std::shared_ptr<PTExpr>;
using PTIndexColumnPtr = std::shared_ptr<PTIndexColumn>;
using PTListNodePtr = std::shared_ptr<TreeListNode<>>;
using PTJsonOperatorPtr = std::shared_ptr<PTJsonOperator>;
using PTQualifiedNamePtr = std::shared_ptr<PTQualifiedName>;
using PTTablePropertyListNodePtr = std::shared_ptr<PTTablePropertyListNode>;
using TreeNodePtr = std::shared_ptr<TreeNode>;
using YBLocationPtr = std::shared_ptr<YBLocation>;

enum class ObjectType : int;
enum class TreeNodeOpcode;

YB_STRONGLY_TYPED_BOOL(NullIsAllowed);

}  // namespace ql
}  // namespace yb
