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

#include <gtest/gtest.h>

#include "yb/cdc/cdc_service.pb.h"
#include "yb/cdc/cdc_service.trace.h"

#include "yb/util/test_macros.h"

namespace yb::cdc {

// cdc_service.proto is a plain-protobuf proto; its stream and tablet ids reach the span through
// the generated .trace functions alone.
TEST(CdcTraceAttrsTest, StreamIdsAreTagged) {
  using Attrs = std::vector<std::pair<std::string, std::string>>;

  GetChangesRequestPB get_changes;
  get_changes.set_stream_id("stream-1");
  get_changes.set_tablet_id("tablet-1");
  get_changes.set_db_stream_id("db-stream-1");
  ASSERT_EQ(
      TracingAttributes(get_changes),
      (Attrs{{"req.db_stream_id", "db-stream-1"},
             {"req.stream_id", "stream-1"},
             {"req.tablet_id", "tablet-1"}}));

  BootstrapProducerRequestPB bootstrap;
  bootstrap.add_xrepl_stream_ids("stream-2");
  bootstrap.add_xrepl_stream_ids("stream-1");
  bootstrap.add_xrepl_stream_ids("stream-2");
  ASSERT_EQ(
      TracingAttributes(bootstrap), (Attrs{{"req.xrepl_stream_ids.*", "stream-1, stream-2"}}));

  ASSERT_TRUE(TracingAttributes(GetChangesRequestPB()).empty());
}

}  // namespace yb::cdc
