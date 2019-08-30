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

#include <gtest/gtest.h>

#include "yb/util/cdc_test_util.h"

namespace yb {
namespace cdc {

void AssertIntKey(const google::protobuf::RepeatedPtrField<cdc::KeyValuePairPB>& key,
                  int32_t value) {
  ASSERT_EQ(key.size(), 1);
  ASSERT_EQ(key[0].key(), "key");
  ASSERT_EQ(key[0].value().int32_value(), value);
}

void CreateCDCStream(const std::unique_ptr<CDCServiceProxy>& cdc_proxy,
                     const TableId& table_id,
                     boost::optional<uint32_t> wal_retention_secs,
                     CDCStreamId* stream_id) {
  CreateCDCStreamRequestPB req;
  CreateCDCStreamResponsePB resp;
  req.set_table_id(table_id);
  if (wal_retention_secs) {
    req.set_retention_sec(*wal_retention_secs);
  }

  rpc::RpcController rpc;
  cdc_proxy->CreateCDCStream(req, &resp, &rpc);
  ASSERT_FALSE(resp.has_error());

  if (stream_id) {
    *stream_id = resp.stream_id();
  }
}

} // namespace cdc
} // namespace yb
