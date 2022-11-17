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
// Tree node definitions for database object names such as table, column, or index names.
//--------------------------------------------------------------------------------------------------

#pragma once

#include "yb/client/yb_table_name.h"
#include "yb/yql/cql/ql/ptree/tree_node.h"
#include "yb/yql/cql/ql/ptree/list_node.h"

namespace yb {
namespace ql {

// This class represents a name node.
class PTName : public TreeNode {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTName> SharedPtr;
  typedef MCSharedPtr<const PTName> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  explicit PTName(MemoryContext *memctx = nullptr,
                  YBLocationPtr loc = nullptr,
                  const MCSharedPtr<MCString>& name = nullptr);
  virtual ~PTName();

  // Node type.
  virtual TreeNodeOpcode opcode() const override {
    return TreeNodeOpcode::kPTName;
  }

  template<typename... TypeArgs>
  inline static PTName::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTName>(memctx, std::forward<TypeArgs>(args)...);
  }

  virtual Status Analyze(SemContext *sem_context) override {
    return Status::OK();
  }

  const MCString& name() const {
    return *name_;
  }

  const MCSharedPtr<MCString>& name_ptr() {
    return name_;
  }

  virtual std::string QLName() const {
    return name_->c_str();
  }

 protected:
  MCSharedPtr<MCString> name_;
};

// This class represents '*' (i.e. all columns of a table) in SQL statement.
class PTNameAll : public PTName {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTNameAll> SharedPtr;
  typedef MCSharedPtr<const PTNameAll> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTNameAll(MemoryContext *memctx, YBLocationPtr loc);
  virtual ~PTNameAll();

  template<typename... TypeArgs>
  inline static PTNameAll::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTNameAll>(memctx, std::forward<TypeArgs>(args)...);
  }

  virtual std::string QLName() const override {
    // We should not get here as '*' should have been converted into a list of column name before
    // the selected tuple is constructed and described.
    LOG(INFO) << "Calling QLName before '*' is converted into a list of column name";
    return "*";
  }
};

// This class represents a qualified name (e.g. "a.m.t").
class PTQualifiedName : public PTName {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTQualifiedName> SharedPtr;
  typedef MCSharedPtr<const PTQualifiedName> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTQualifiedName(MemoryContext *mctx,
                  YBLocationPtr loc,
                  const PTName::SharedPtr& ptname);
  PTQualifiedName(MemoryContext *mctx,
                  YBLocationPtr loc,
                  const MCSharedPtr<MCString>& name);
  virtual ~PTQualifiedName();

  template<typename... TypeArgs>
  inline static PTQualifiedName::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTQualifiedName>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Forming qualified name by appending.
  void Append(const PTName::SharedPtr& ptname);

  // Forming qualified name by prepending.
  void Prepend(const PTName::SharedPtr& ptname);

  // Node semantics analysis.
  virtual Status Analyze(SemContext *sem_context) override;

  const MCString& first_name() const {
    return ptnames_.front()->name();
  }

  const MCString& last_name() const {
    return ptnames_.back()->name();
  }

  bool IsSimpleName() const {
    return ptnames_.size() == 1;
  }

  // Column name should be the last name.
  const MCSharedPtr<MCString>& column_name() {
    return ptnames_.back()->name_ptr();
  }

  // Construct bind variable name from this name.
  const MCSharedPtr<MCString>& bindvar_name() {
    return ptnames_.back()->name_ptr();
  }

  // Analyze this qualified name as an object name.
  Status AnalyzeName(SemContext *sem_context, ObjectType object_type);

  client::YBTableName ToTableName() const {
    return client::YBTableName(YQL_DATABASE_CQL, first_name().c_str(), last_name().c_str());
  }

  virtual std::string QLName() const override {
    std::string full_name;
    for (auto name : ptnames_) {
      if (!full_name.empty()) {
        full_name += '.';
      }
      full_name += name->QLName();
    }
    return full_name;
  }

 private:
  MCList<PTName::SharedPtr> ptnames_;
};

using PTQualifiedNameListNode = TreeListNode<PTQualifiedName>;

}  // namespace ql
}  // namespace yb
