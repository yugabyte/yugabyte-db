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

#include "yb/cdc/cdc_util.h"

#include "yb/cdc/cdc_consumer.pb.h"
#include "yb/gutil/strings/stringpiece.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"

namespace yb::cdc {

std::string TabletStreamInfo::ToString() const {
  return Format("{ stream_id: $1 tablet_id: $2 }", stream_id, tablet_id);
}

void RepairOldSchemaVersionsPB(SchemaVersionsPB& pb) {
  const int producer_size = pb.old_producer_schema_versions_size();
  const int consumer_size = pb.old_consumer_schema_versions_size();
  if (producer_size == consumer_size) {
    return;
  }
  // Legacy data before 11482d6: one of the old_*_schema_version fields was 0 (proto3 default) and
  // got stripped from the wire while the other serialized as a single element. The only expected
  // shape is one side with 1 element and the other empty.
  if (producer_size + consumer_size != 1) {
    LOG(DFATAL)
        << "Unexpected SchemaVersionsPB old_*_schema_versions size mismatch: producer="
        << producer_size << ", consumer=" << consumer_size;
    return;
  }
  YB_LOG_EVERY_N_SECS(INFO, 60) << Format(
      "Fixing SchemaVersionsPB (producer=$0, consumer=$1) with implicit 0", producer_size,
      consumer_size);
  if (producer_size == 0) {
    pb.add_old_producer_schema_versions(0);
  } else {
    pb.add_old_consumer_schema_versions(0);
  }
}

}  // namespace yb::cdc
