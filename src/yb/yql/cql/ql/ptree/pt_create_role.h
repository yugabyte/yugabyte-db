//--------------------------------------------------------------------------------------------------
// Copyright (c) YugabyteDB, Inc.
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
// Tree node definitions for CREATE ROLE statement.
//--------------------------------------------------------------------------------------------------

#pragma once

#include "yb/yql/cql/ql/ptree/tree_node.h"
#include "yb/yql/cql/ql/ptree/pt_name.h"
#include "yb/util/crypt.h"

namespace yb {
namespace ql {
using yb::util::kBcryptHashSize;
using yb::util::kBcryptHashStrLen;
using yb::util::kBcryptMaxWorkFactor;
using yb::util::kBcryptMinWorkFactor;

class SemContext;

//--------------------------------------------------------------------------------------------------
// Roles.

enum class PTRoleOptionType {
  kLogin,
  kPassword,
  kSuperuser,
};

class PTRoleOption : public TreeNode {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTRoleOption> SharedPtr;
  typedef MCSharedPtr<const PTRoleOption> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  explicit PTRoleOption(MemoryContext* memctx = nullptr, YBLocationPtr loc = nullptr)
      : TreeNode(memctx, loc) {
  }
  virtual ~PTRoleOption() {
  }

  // Node type.
  virtual TreeNodeOpcode opcode() const override {
    return TreeNodeOpcode::kPTRoleOption;
  }

  virtual PTRoleOptionType option_type() = 0;
};

using PTRoleOptionListNode = TreeListNode<PTRoleOption>;

class PTRolePassword : public PTRoleOption {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTRolePassword> SharedPtr;
  typedef MCSharedPtr<const PTRolePassword> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.

  PTRolePassword(MemoryContext* memctx,
                 YBLocationPtr loc,
                 const MCSharedPtr<MCString>& password,
                 bool is_hashed = false);

  virtual ~PTRolePassword();

  virtual PTRoleOptionType option_type() override {
    return PTRoleOptionType::kPassword;
  }

  template<typename... TypeArgs>
  inline static PTRolePassword::SharedPtr MakeShared(MemoryContext* memctx, TypeArgs&&... args) {
    return MCMakeShared<PTRolePassword>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual Status Analyze(SemContext* sem_context) override {
    return Status::OK();
  }

  const char* password() const {
    return password_->c_str();
  }

  // True when password() is a client-supplied bcrypt hash (HASHED PASSWORD) that must be stored
  // verbatim, false for plaintext that Analyze still has to bcrypt.
  bool is_hashed() const {
    return is_hashed_;
  }

  // Fills *salted_hash with this option's value as a kBcryptHashSize-byte, zero-padded buffer:
  // bcrypts a plaintext PASSWORD, or validates a client-supplied HASHED PASSWORD and stores it
  // verbatim. The zero padding keeps the fixed-width contract of salted_hash(), bcrypt_checkpw and
  // the roles vtable while leaving clean NULs rather than stack bytes after the hash (#27523).
  // Raises a semantic error on this node if a supplied hash is not one bcrypt could verify.
  // Shared by CREATE ROLE and ALTER ROLE.
  Status BuildSaltedHash(SemContext* sem_context, MCSharedPtr<MCString>* salted_hash) const;

  // True if `hash` is a bcrypt hash of the shape Cassandra's hash_password emits *and* one the
  // underlying crypt_blowfish can actually verify: $2[abxy]$<2-digit cost within
  // [kBcryptMinWorkFactor, kBcryptMaxWorkFactor]>$<22-char salt><31-char checksum>,
  // kBcryptHashStrLen characters in total.
  static bool IsValidBcryptHash(const char* hash);

 private:
  const MCSharedPtr<MCString> password_;
  const bool is_hashed_;

};

class PTRoleLogin : public PTRoleOption {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTRoleLogin> SharedPtr;
  typedef MCSharedPtr<const PTRoleLogin> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.

  PTRoleLogin(MemoryContext *memctx,
              YBLocationPtr loc,
              bool login);

  virtual ~PTRoleLogin();

  virtual PTRoleOptionType option_type() override {
    return PTRoleOptionType::kLogin;
  }

  template<typename... TypeArgs>
  inline static PTRoleLogin::SharedPtr MakeShared(MemoryContext* memctx, TypeArgs&&... args) {
    return MCMakeShared<PTRoleLogin>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual Status Analyze(SemContext* sem_context) override {
    return Status::OK();
  }

  bool login() const {
    return login_;
  }

 private:
  const bool login_;
};

class PTRoleSuperuser : public PTRoleOption {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTRoleSuperuser> SharedPtr;
  typedef MCSharedPtr<const PTRoleSuperuser> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.

  PTRoleSuperuser(MemoryContext *memctx,
                  YBLocationPtr loc,
                  bool superuser);

  virtual ~PTRoleSuperuser();

  virtual PTRoleOptionType option_type() override {
    return PTRoleOptionType::kSuperuser;
  }

  template<typename... TypeArgs>
  inline static PTRoleSuperuser::SharedPtr MakeShared(MemoryContext* memctx, TypeArgs&&... args) {
    return MCMakeShared<PTRoleSuperuser>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual Status Analyze(SemContext* sem_context) override {
    return Status::OK();
  }

  bool superuser() const {
    return superuser_;
  }

 private:
  const bool superuser_;
};

//--------------------------------------------------------------------------------------------------
// CREATE ROLE statement.

class PTCreateRole : public TreeNode {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTCreateRole> SharedPtr;
  typedef MCSharedPtr<const PTCreateRole> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTCreateRole(MemoryContext* memctx,
               YBLocationPtr loc,
               const MCSharedPtr<MCString>& name,
               const PTRoleOptionListNode::SharedPtr& roleOptions,
               bool create_if_not_exists);
  virtual ~PTCreateRole();

  // Node type.
  virtual TreeNodeOpcode opcode() const override {
    return TreeNodeOpcode::kPTCreateRole;
  }

  // Support for shared_ptr.
  template<typename... TypeArgs>
  inline static PTCreateRole::SharedPtr MakeShared(MemoryContext* memctx, TypeArgs&&... args) {
    return MCMakeShared<PTCreateRole>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual Status Analyze(SemContext* sem_context) override;
  void PrintSemanticAnalysisResult(SemContext* sem_context);

  // Role name.
  const char* role_name() const {
    return name_->c_str();
  }

  std::string salted_hash() const {
    // Empty salted hash denotes no password. salted_hash can contain null characters.
    return (salted_hash_ != nullptr) ?  std::string(salted_hash_->c_str(), kBcryptHashSize) : "";
  }

  bool superuser() const {
    return superuser_;
  }

  bool login() const {
    return login_;
  }

  bool create_if_not_exists() const {
    return create_if_not_exists_;
  }

 private:

  const MCSharedPtr<MCString>  name_;
  PTRoleOptionListNode::SharedPtr roleOptions_;
  MCSharedPtr<MCString> salted_hash_ = nullptr;
  bool login_ = false;
  bool superuser_ = false;
  const bool create_if_not_exists_;

};

}  // namespace ql
}  // namespace yb
