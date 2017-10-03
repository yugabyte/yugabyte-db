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

#ifndef KUDU_RPC_AUTH_STORE_H
#define KUDU_RPC_AUTH_STORE_H

#include <unordered_map>
#include <string>

#include "kudu/gutil/macros.h"

namespace kudu {

class Status;

namespace rpc {

using std::string;
using std::unordered_map;

// This class stores username / password pairs in memory for use in PLAIN SASL auth.
// Add() is NOT thread safe.
// Authenticate() is safe to call from multiple threads.
class AuthStore {
 public:
  AuthStore();
  virtual ~AuthStore();

  // Add user to the auth store.
  virtual Status Add(const string& user, const string& password);

  // Validate whether user/password combination exists in auth store.
  // Returns OK if the user has valid credentials.
  // Returns NotFound if the user is not found.
  // Returns NotAuthorized if the password is incorrect.
  virtual Status Authenticate(const string& user, const string& password) const;

 private:
  unordered_map<string, string> user_cred_map_;

  DISALLOW_COPY_AND_ASSIGN(AuthStore);
};

// This class simply allows anybody through.
class DummyAuthStore : public AuthStore {
 public:
  DummyAuthStore();
  virtual ~DummyAuthStore();

  // Always returns OK
  virtual Status Authenticate(const string& user, const string& password) const OVERRIDE;
};

} // namespace rpc
} // namespace kudu

#endif // KUDU_RPC_AUTH_STORE_H
