// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
#pragma once

#include "yb/util/status.h"

namespace yb {

class ExternalMiniCluster;

namespace client {

class YBClient;

} // namespace client

namespace itest {

class RetainDeleteMarkersValidator {
 public:
  virtual ~RetainDeleteMarkersValidator();
  RetainDeleteMarkersValidator(
      ExternalMiniCluster* cluster, client::YBClient* client, const std::string& namespace_name);

  client::YBClient& client() {
    return client_;
  }

  ExternalMiniCluster& cluster() {
    return cluster_;
  }

  const std::string& namespace_name() const {
    return namespace_name_;
  }

  void Test();
  void TestRecovery(bool use_multiple_requests);

 protected:
  virtual Status RestartCluster();

 private:
  virtual Status CreateIndex(const std::string& index_name, const std::string& table_name) = 0;
  virtual Status CreateTable(const std::string& table_name) = 0;

  ExternalMiniCluster& cluster_;
  client::YBClient& client_;
  const std::string namespace_name_;
};

} // namespace itest
} // namespace yb
