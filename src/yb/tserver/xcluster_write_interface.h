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

#pragma once

#include <memory>
#include <string>

#include "yb/cdc/cdc_types.h"

namespace yb {
namespace cdc {

class CDCRecordPB;

}
namespace tserver {

class WriteRequestPB;

struct ProcessRecordInfo {
  TabletId tablet_id;

  // Map of producer-consumer schema versions for the record.
  const cdc::XClusterSchemaVersionMap schema_versions_map;
};

class XClusterWriteInterface {
 public:
  virtual ~XClusterWriteInterface() {}
  virtual std::unique_ptr<WriteRequestPB> FetchNextRequest() = 0;
  virtual Status ProcessRecord(
      const ProcessRecordInfo& process_record_info, const cdc::CDCRecordPB& record) = 0;
};

void ResetWriteInterface(std::unique_ptr<XClusterWriteInterface>* write_strategy);

}  // namespace tserver
}  // namespace yb
