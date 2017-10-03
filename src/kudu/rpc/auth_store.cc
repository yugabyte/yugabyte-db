// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include "kudu/rpc/auth_store.h"

#include <string>
#include <unordered_map>

#include "kudu/util/status.h"

namespace kudu {
namespace rpc {

AuthStore::AuthStore() {
}

AuthStore::~AuthStore() {
}

Status AuthStore::Add(const string& user, const string& pass) {
  user_cred_map_[user] = pass;
  return Status::OK();
}

Status AuthStore::Authenticate(const string& user, const string& pass) const {
  auto it = user_cred_map_.find(user);
  if (it == user_cred_map_.end()) {
    return Status::NotFound("Unknown user", user);
  }
  if (it->second != pass) {
    return Status::NotAuthorized("Invalid credentials for user", user);
  }
  return Status::OK();
}

DummyAuthStore::DummyAuthStore() {
}

DummyAuthStore::~DummyAuthStore() {
}

Status DummyAuthStore::Authenticate(const string& user, const string& password) const {
  return Status::OK();
}

} // namespace rpc
} // namespace kudu
