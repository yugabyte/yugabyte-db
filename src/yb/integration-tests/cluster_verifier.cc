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

#include <gtest/gtest.h>
#include <string>
#include <memory>
#include <vector>

#include "yb/client/client.h"
#include "yb/client/row_result.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/integration-tests/cluster_verifier.h"
#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/tools/ysck_remote.h"
#include "yb/util/monotime.h"
#include "yb/util/test_util.h"

using std::string;
using std::vector;

namespace yb {

using strings::Substitute;
using tools::Ysck;
using tools::YsckCluster;
using tools::YsckMaster;
using tools::RemoteYsckMaster;

ClusterVerifier::ClusterVerifier(ExternalMiniCluster* cluster)
  : cluster_(cluster),
    checksum_options_(ChecksumOptions()) {
  checksum_options_.use_snapshot = false;
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
  MonoTime deadline = MonoTime::Now(MonoTime::FINE);
  deadline.AddDelta(checksum_options_.timeout);

  Status s;
  double sleep_time = 0.1;
  while (MonoTime::Now(MonoTime::FINE).ComesBefore(deadline)) {
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
  Sockaddr addr = cluster_->leader_master()->bound_rpc_addr();

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

void ClusterVerifier::CheckRowCount(const std::string& table_name,
                                    ComparisonMode mode,
                                    int expected_row_count) {
  ASSERT_OK(DoCheckRowCount(table_name, mode, expected_row_count));
}

Status ClusterVerifier::DoCheckRowCount(const std::string& table_name,
                                        ComparisonMode mode,
                                        int expected_row_count) {
  client::sp::shared_ptr<client::YBClient> client;
  client::YBClientBuilder builder;
  RETURN_NOT_OK_PREPEND(cluster_->CreateClient(builder,
                                               &client),
                        "Unable to connect to cluster");
  client::sp::shared_ptr<client::YBTable> table;
  RETURN_NOT_OK_PREPEND(client->OpenTable(table_name, &table),
                        "Unable to open table");
  client::YBScanner scanner(table.get());
  CHECK_OK(scanner.SetProjectedColumns(vector<string>()));
  RETURN_NOT_OK_PREPEND(scanner.Open(), "Unable to open scanner");
  int count = 0;
  vector<client::YBRowResult> rows;
  while (scanner.HasMoreRows()) {
    RETURN_NOT_OK_PREPEND(scanner.NextBatch(&rows), "Unable to read from scanner");
    count += rows.size();
  }

  if (mode == AT_LEAST && count < expected_row_count) {
    return Status::Corruption(Substitute("row count $0 is not at least expected value $1",
                                         count, expected_row_count));
  } else if (mode == EXACTLY && count != expected_row_count) {
    return Status::Corruption(Substitute("row count $0 is not exactly expected value $1",
                                         count, expected_row_count));
  }
  return Status::OK();
}

void ClusterVerifier::CheckRowCountWithRetries(const std::string& table_name,
                                               ComparisonMode mode,
                                               int expected_row_count,
                                               const MonoDelta& timeout) {
  MonoTime deadline = MonoTime::Now(MonoTime::FINE);
  deadline.AddDelta(timeout);
  Status s;
  while (true) {
    s = DoCheckRowCount(table_name, mode, expected_row_count);
    if (s.ok() || deadline.ComesBefore(MonoTime::Now(MonoTime::FINE))) break;
    LOG(WARNING) << "CheckRowCount() has not succeeded yet: " << s.ToString()
                 << "... will retry";
    SleepFor(MonoDelta::FromMilliseconds(100));
  }

  ASSERT_OK(s);
}

} // namespace yb
