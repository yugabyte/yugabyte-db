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

#include "yb/integration-tests/cluster_verifier.h"

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <boost/range/iterator_range.hpp>
#include <gtest/gtest.h>

#include "yb/client/client.h"
#include "yb/client/table_handle.h"

#include "yb/gutil/strings/substitute.h"

#include "yb/integration-tests/mini_cluster_base.h"

#include "yb/tools/ysck_remote.h"

#include "yb/util/monotime.h"
#include "yb/util/result.h"
#include "yb/util/test_macros.h"

using std::string;
using std::vector;

namespace yb {

using strings::Substitute;
using tools::Ysck;
using tools::YsckCluster;
using tools::YsckMaster;
using tools::RemoteYsckMaster;
using client::YBTableName;

ClusterVerifier::ClusterVerifier(MiniClusterBase* cluster)
  : cluster_(cluster),
    checksum_options_(ChecksumOptions()) {
}

ClusterVerifier::~ClusterVerifier() {
}

void ClusterVerifier::SetVerificationTimeout(const MonoDelta& timeout) {
  checksum_options_.timeout = timeout;
}

void ClusterVerifier::SetScanConcurrency(int concurrency) {
  checksum_options_.scan_concurrency = concurrency;
}

void ClusterVerifier::CheckCluster() {
  MonoTime deadline = MonoTime::Now();
  deadline.AddDelta(checksum_options_.timeout);

  Status s;
  double sleep_time = 0.1;
  while (MonoTime::Now().ComesBefore(deadline)) {
    s = DoYsck();
    if (s.ok()) {
      break;
    }

    LOG(INFO) << "Check not successful yet, sleeping and retrying: " + s.ToString();
    sleep_time *= 1.5;
    if (sleep_time > 1) { sleep_time = 1; }
    SleepFor(MonoDelta::FromSeconds(sleep_time));
  }
  ASSERT_OK(s);
}

Status ClusterVerifier::DoYsck() {
  auto addr = VERIFY_RESULT(cluster_->GetLeaderMasterBoundRpcAddr());

  std::shared_ptr<YsckMaster> master;
  RETURN_NOT_OK(RemoteYsckMaster::Build(addr, &master));
  std::shared_ptr<YsckCluster> cluster(new YsckCluster(master));
  std::shared_ptr<Ysck> ysck(new Ysck(cluster));

  // This is required for everything below.
  RETURN_NOT_OK(ysck->CheckMasterRunning());
  RETURN_NOT_OK(ysck->FetchTableAndTabletInfo());
  RETURN_NOT_OK(ysck->CheckTabletServersRunning());
  RETURN_NOT_OK(ysck->CheckTablesConsistency());

  vector<string> tables;
  vector<string> tablets;
  RETURN_NOT_OK(ysck->ChecksumData(tables, tablets, checksum_options_));
  return Status::OK();
}

void ClusterVerifier::CheckRowCount(const YBTableName& table_name,
                                    ComparisonMode mode,
                                    size_t expected_row_count,
                                    YBConsistencyLevel consistency) {
  ASSERT_OK(DoCheckRowCount(table_name, mode, expected_row_count, consistency));
}

Status ClusterVerifier::DoCheckRowCount(const YBTableName& table_name,
                                        ComparisonMode mode,
                                        size_t expected_row_count,
                                        YBConsistencyLevel consistency) {
  auto client = VERIFY_RESULT_PREPEND(
      cluster_->CreateClient(), "Unable to connect to cluster");

  client::TableHandle table;
  RETURN_NOT_OK_PREPEND(table.Open(table_name, client.get()), "Unable to open table");
  client::TableIteratorOptions options;
  options.consistency = consistency;
  size_t count = boost::size(client::TableRange(table, options));

  if (mode == AT_LEAST && count < expected_row_count) {
    return STATUS(Corruption, Substitute("row count $0 is not at least expected value $1",
                                         count, expected_row_count));
  } else if (mode == EXACTLY && count != expected_row_count) {
    return STATUS(Corruption, Substitute("row count $0 is not exactly expected value $1",
                                         count, expected_row_count));
  }
  return Status::OK();
}

void ClusterVerifier::CheckRowCountWithRetries(const YBTableName& table_name,
                                               ComparisonMode mode,
                                               size_t expected_row_count,
                                               const MonoDelta& timeout) {
  MonoTime deadline = MonoTime::Now();
  deadline.AddDelta(timeout);
  Status s;
  while (true) {
    s = DoCheckRowCount(table_name, mode, expected_row_count, YBConsistencyLevel::STRONG);
    if (s.ok() || deadline.ComesBefore(MonoTime::Now())) break;
    LOG(WARNING) << "CheckRowCount() has not succeeded yet: " << s.ToString()
                 << "... will retry";
    SleepFor(MonoDelta::FromMilliseconds(100));
  }

  ASSERT_OK(s);
}

}  // namespace yb
