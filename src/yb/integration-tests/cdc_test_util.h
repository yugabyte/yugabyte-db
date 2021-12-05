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

#ifndef YB_INTEGRATION_TESTS_CDC_TEST_UTIL_H
#define YB_INTEGRATION_TESTS_CDC_TEST_UTIL_H

#include "yb/cdc/cdc_service.pb.h"
#include "yb/cdc/cdc_service.proxy.h"

#include "yb/integration-tests/mini_cluster.h"

namespace yb {
namespace cdc {

void AssertIntKey(const google::protobuf::RepeatedPtrField<cdc::KeyValuePairPB>& key,
                  int32_t value);

void CreateCDCStream(const std::unique_ptr<CDCServiceProxy>& cdc_proxy,
                     const TableId& table_id,
                     CDCStreamId* stream_id);

// For any tablet that belongs to a table whose name starts with 'table_name_start', this method
// will verify that its WAL retention time matches the provided time.
// It will also verify that at least one WAL retention time was checked.
void VerifyWalRetentionTime(yb::MiniCluster* cluster,
                            const std::string& table_name_start,
                            uint32_t expected_wal_retention_secs);

} // namespace cdc
} // namespace yb

#endif // YB_INTEGRATION_TESTS_CDC_TEST_UTIL_H
