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
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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
#include <string>
#include <vector>

#include "yb/docdb/docdb_fwd.h"

#include "yb/tablet/tablet-harness.h"

#include "yb/util/test_util.h"

namespace yb {

class FsManager;

namespace docdb {
class DocRowwiseIterator;
}

namespace server {
class Clock;
}

namespace tablet {

class YBTabletTest : public YBTest {
 public:
  explicit YBTabletTest(const Schema& schema, TableType table_type = TableType::YQL_TABLE_TYPE);

  void SetUp() override;

  void CreateTestTablet(const std::string& root_dir = "");

  void SetUpTestTablet(const std::string& root_dir = "");

  void TabletReOpen(const std::string& root_dir = "") {
    SetUpTestTablet(root_dir);
  }

  const Schema &schema() const {
    return schema_;
  }

  const Schema &client_schema() const {
    return client_schema_;
  }

  server::Clock* clock() {
    return harness_->clock();
  }

  FsManager* fs_manager() {
    return harness_->fs_manager();
  }

  void AlterSchema(const Schema& schema);

  const TabletPtr& tablet() const {
    return harness_->tablet();
  }

  TabletHarness* harness() {
    return harness_.get();
  }

 protected:
  const Schema schema_;
  const Schema client_schema_;
  TableType table_type_;

  std::unique_ptr<TabletHarness> harness_;
};

Status IterateToStringList(
    docdb::YQLRowwiseIteratorIf* iter, const Schema& schema, std::vector<std::string>* out,
    int limit = INT_MAX);

// Dump all of the rows of the tablet into the given vector.
Status DumpTablet(const Tablet& tablet, std::vector<std::string>* out);

} // namespace tablet
} // namespace yb
