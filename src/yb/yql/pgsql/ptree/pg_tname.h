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

#ifndef YB_YQL_PGSQL_PTREE_PG_TNAME_H_
#define YB_YQL_PGSQL_PTREE_PG_TNAME_H_

#include "yb/client/yb_table_name.h"
#include "yb/yql/pgsql/ptree/tree_node.h"
#include "yb/yql/pgsql/ptree/list_node.h"

namespace yb {
namespace pgsql {

// This class represents a name node.
class PgTName : public TreeNode {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PgTName> SharedPtr;
  typedef MCSharedPtr<const PgTName> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  explicit PgTName(MemoryContext *memctx = nullptr,
                   PgTLocation::SharedPtr loc = nullptr,
                   const MCSharedPtr<MCString>& name = nullptr,
                   ObjectType object_type = OBJECT_DEFAULT);
  virtual ~PgTName();

  template<typename... TypeArgs>
  inline static PgTName::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PgTName>(memctx, std::forward<TypeArgs>(args)...);
  }

  CHECKED_STATUS SetupPrimaryKey(PgCompileContext *compile_context);

  const MCString& name() const {
    return *name_;
  }

  const MCSharedPtr<MCString>& name_ptr() {
    return name_;
  }

  virtual string QLName() const {
    return name_->c_str();
  }

  void set_object_type(ObjectType object_type) {
    object_type_ = object_type;
  }

  virtual CHECKED_STATUS Analyze(PgCompileContext *compile_context) override;
  static bool IsLegalName(const string& user_defined_name);

 protected:
  MCSharedPtr<MCString> name_;
  ObjectType object_type_;
};

// This class represents '*' (i.e. all columns of a table) in SQL statement.
class PgTNameAll : public PgTName {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PgTNameAll> SharedPtr;
  typedef MCSharedPtr<const PgTNameAll> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PgTNameAll(MemoryContext *memctx, PgTLocation::SharedPtr loc);
  virtual ~PgTNameAll();

  template<typename... TypeArgs>
  inline static PgTNameAll::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PgTNameAll>(memctx, std::forward<TypeArgs>(args)...);
  }

  virtual string QLName() const override {
    // We should not get here as '*' should have been converted into a list of column name before
    // the selected tuple is constructed and described.
    LOG(INFO) << "Calling QLName before '*' is converted into a list of column name";
    return "*";
  }
};

// This class represents a qualified name (e.g. "a.m.t").
class PgTQualifiedName : public PgTName {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PgTQualifiedName> SharedPtr;
  typedef MCSharedPtr<const PgTQualifiedName> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PgTQualifiedName(MemoryContext *mctx,
                  PgTLocation::SharedPtr loc,
                  const PgTName::SharedPtr& ptname);
  PgTQualifiedName(MemoryContext *mctx,
                  PgTLocation::SharedPtr loc,
                  const MCSharedPtr<MCString>& name);
  virtual ~PgTQualifiedName();

  template<typename... TypeArgs>
  inline static PgTQualifiedName::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PgTQualifiedName>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Forming qualified name by appending.
  void Append(const PgTName::SharedPtr& ptname);

  // Forming qualified name by prepending.
  void Prepend(const PgTName::SharedPtr& ptname);

  // Node semantics analysis.
  virtual CHECKED_STATUS Analyze(PgCompileContext *compile_context) override;

  const MCString& first_name() const {
    return ptnames_.front()->name();
  }

  const MCString& last_name() const {
    return ptnames_.back()->name();
  }

  bool IsSimpleName() const {
    return ptnames_.size() == 1;
  }

  client::YBTableName ToTableName() const {
    return client::YBTableName(first_name().c_str(), last_name().c_str());
  }

  virtual string QLName() const override {
    string full_name;
    for (auto name : ptnames_) {
      if (!full_name.empty()) {
        full_name += '.';
      }
      full_name += name->QLName();
    }
    return full_name;
  }

 private:
  MCList<PgTName::SharedPtr> ptnames_;
};

using PgTQualifiedNameListNode = TreeListNode<PgTQualifiedName>;

}  // namespace pgsql
}  // namespace yb

#endif  // YB_YQL_PGSQL_PTREE_PG_TNAME_H_
