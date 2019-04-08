//--------------------------------------------------------------------------------------------------
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
//
// Column Definition Tree node implementation.
//--------------------------------------------------------------------------------------------------

#include "yb/yql/cql/ql/ptree/pt_column_definition.h"
#include "yb/yql/cql/ql/ptree/sem_context.h"

namespace yb {
namespace ql {

PTColumnDefinition::PTColumnDefinition(MemoryContext *memctx,
                                       YBLocation::SharedPtr loc,
                                       const MCSharedPtr<MCString>& name,
                                       const PTBaseType::SharedPtr& datatype,
                                       const PTListNode::SharedPtr& qualifiers)
    : TreeNode(memctx, loc),
      name_(name),
      datatype_(datatype),
      qualifiers_(qualifiers),
      is_primary_key_(false),
      is_hash_key_(false),
      is_static_(false),
      order_(-1),
      sorting_type_(ColumnSchema::SortingType::kNotSpecified) {
}

PTColumnDefinition::PTColumnDefinition(MemoryContext *memctx,
                                       const PTColumnDefinition& column,
                                       const PTExprListNode::SharedPtr& operators,
                                       const PTBaseType::SharedPtr& datatype)
    : TreeNode(memctx, column.loc_),
      name_(column.name_),
      datatype_(datatype == nullptr ? column.datatype_ : datatype),
      qualifiers_(column.qualifiers_),
      is_primary_key_(column.is_primary_key_),
      is_hash_key_(column.is_hash_key_),
      is_static_(column.is_static_),
      order_(column.order_),
      sorting_type_(column.sorting_type_),
      operators_(operators) {
}

PTColumnDefinition::~PTColumnDefinition() {
}

CHECKED_STATUS PTColumnDefinition::Analyze(SemContext *sem_context) {
  if (!sem_context->processing_column_definition()) {
    return Status::OK();
  }

  // Save context state, and set "this" as current column in the context.
  SymbolEntry cached_entry = *sem_context->current_processing_id();
  sem_context->set_current_column(this);

  // Analyze column qualifiers.
  RETURN_NOT_OK(sem_context->MapSymbol(*name_, this));
  if (qualifiers_ != nullptr) {
    RETURN_NOT_OK(qualifiers_->Analyze(sem_context));
  }

  // Add the analyzed column to table.
  PTCreateTable *table = sem_context->current_create_table_stmt();
  RETURN_NOT_OK(table->AppendColumn(sem_context, this));

  // Restore the context value as we are done with this colummn.
  sem_context->set_current_processing_id(cached_entry);
  return Status::OK();
}

}  // namespace ql
}  // namespace yb
