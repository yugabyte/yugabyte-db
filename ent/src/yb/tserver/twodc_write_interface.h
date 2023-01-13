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

#include "yb/client/external_transaction.h"

namespace yb {
namespace cdc {

class CDCRecordPB;

}
namespace tserver {

class WriteRequestPB;

namespace enterprise {

struct ProcessRecordInfo {
  TabletId tablet_id;

  // Only used for intent records.
  bool enable_replicate_transaction_status_table;
  TabletId status_tablet_id;

  // last compatible consumer schema version
  SchemaVersion last_compatible_consumer_schema_version;
};

class TwoDCWriteInterface {
 public:
  virtual ~TwoDCWriteInterface() {}
  virtual std::unique_ptr<WriteRequestPB> GetNextWriteRequest() = 0;
  virtual Status ProcessRecord(
      const ProcessRecordInfo& process_record_info, const cdc::CDCRecordPB& record) = 0;
  virtual Status ProcessCreateRecord(
      const std::string& status_tablet,
      const cdc::CDCRecordPB& record) = 0;
  virtual Status ProcessCommitRecord(
      const std::string& status_tablet,
      const std::vector<std::string>& involved_target_tablet_ids,
      const cdc::CDCRecordPB& record) = 0;
  virtual std::vector<client::ExternalTransactionMetadata>& GetTransactionMetadatas() = 0;
};

void ResetWriteInterface(std::unique_ptr<TwoDCWriteInterface>* write_strategy);

} // namespace enterprise
} // namespace tserver
} // namespace yb
