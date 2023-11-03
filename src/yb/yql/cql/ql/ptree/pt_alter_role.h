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
// Tree node definitions for ALTER ROLE statement.
//--------------------------------------------------------------------------------------------------

#pragma once

#include <boost/optional.hpp>

#include "yb/yql/cql/ql/ptree/tree_node.h"
#include "yb/yql/cql/ql/ptree/pt_name.h"
#include "yb/yql/cql/ql/ptree/pt_create_role.h"
#include "yb/util/crypt.h"

namespace yb {
namespace ql {
using yb::util::kBcryptHashSize;

//--------------------------------------------------------------------------------------------------
// ALTER ROLE statement.

class PTAlterRole : public TreeNode {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTAlterRole> SharedPtr;
  typedef MCSharedPtr<const PTAlterRole> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTAlterRole(MemoryContext* memctx,
              YBLocationPtr loc,
              const MCSharedPtr<MCString>& name,
              const PTRoleOptionListNode::SharedPtr& roleOptions);

  virtual ~PTAlterRole();

  // Node type.
  virtual TreeNodeOpcode opcode() const override {
    return TreeNodeOpcode::kPTAlterRole;
  }

  // Support for shared_ptr.
  template<typename... TypeArgs>
  inline static PTAlterRole::SharedPtr MakeShared(MemoryContext* memctx, TypeArgs&&... args) {
    return MCMakeShared<PTAlterRole>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual Status Analyze(SemContext* sem_context) override;
  void PrintSemanticAnalysisResult(SemContext* sem_context);

  // Role name.
  const char* role_name() const {
    return name_->c_str();
  }

  boost::optional<std::string> salted_hash() const {
    boost::optional<std::string> ret_salted_hash;
    // Empty salted hash denotes no password, salted_hash can contain null characters.
    if (salted_hash_ != nullptr) {
      ret_salted_hash = std::string(salted_hash_->c_str(), kBcryptHashSize);
    }
    return ret_salted_hash;
  }

  boost::optional<bool> superuser() const {
    return superuser_;
  }

  boost::optional<bool> login() const {
    return login_;
  }

 private:
  const MCSharedPtr<MCString>  name_;
  PTRoleOptionListNode::SharedPtr roleOptions_;
  MCSharedPtr<MCString> salted_hash_ = nullptr;
  boost::optional<bool> login_;
  boost::optional<bool> superuser_;
};

}  // namespace ql
}  // namespace yb
