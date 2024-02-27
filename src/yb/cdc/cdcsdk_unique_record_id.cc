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

#include "yb/cdc/cdcsdk_unique_record_id.h"
#include "yb/util/logging.h"

namespace yb {

namespace cdc {

CDCSDKUniqueRecordID::CDCSDKUniqueRecordID(
    RowMessage_Op op, uint64_t commit_time, uint64_t record_time, std::string& tablet_id,
    uint32_t write_id)
    : op_(op),
      commit_time_(commit_time),
      record_time_(record_time),
      tablet_id_(tablet_id),
      write_id_(write_id) {}

CDCSDKUniqueRecordID::CDCSDKUniqueRecordID(
    const TabletId& tablet_id, const std::shared_ptr<CDCSDKProtoRecordPB>& record) {
  this->op_ = record->row_message().op();
  this->commit_time_ = record->row_message().commit_time();
  switch (this->op_) {
    case RowMessage_Op_DDL: FALLTHROUGH_INTENDED;
    case RowMessage_Op_BEGIN:
      this->record_time_ = 0;
      this->write_id_ = 0;
      this->tablet_id_ = "";
      break;
    case RowMessage_Op_SAFEPOINT: FALLTHROUGH_INTENDED;
    case RowMessage_Op_COMMIT:
      this->record_time_ = std::numeric_limits<uint64_t>::max();
      this->write_id_ = std::numeric_limits<uint32_t>::max();
      this->tablet_id_ = "";
      break;
    case RowMessage_Op_INSERT: FALLTHROUGH_INTENDED;
    case RowMessage_Op_DELETE: FALLTHROUGH_INTENDED;
    case RowMessage_Op_UPDATE:
      this->record_time_ = record->row_message().record_time();
      this->write_id_ = record->cdc_sdk_op_id().write_id();
      this->tablet_id_ = tablet_id;
      break;
    case RowMessage_Op_UNKNOWN: FALLTHROUGH_INTENDED;
    case RowMessage_Op_TRUNCATE: FALLTHROUGH_INTENDED;
    case RowMessage_Op_READ:
      // This should never happen as we only invoke this constructor after ensuring that the value
      // is not one of these.
      LOG(FATAL) << "Unexpected record received in Tablet Queue for tablet_id: " << tablet_id
                 << "Record:" << record->DebugString();
  }
}

uint64_t CDCSDKUniqueRecordID::GetCommitTime() const { return commit_time_; }

bool CDCSDKUniqueRecordID::CanFormUniqueRecordId(
    const std::shared_ptr<CDCSDKProtoRecordPB>& record) {
  RowMessage_Op op = record->row_message().op();
  switch (op) {
    case RowMessage_Op_DDL: FALLTHROUGH_INTENDED;
    case RowMessage_Op_BEGIN: FALLTHROUGH_INTENDED;
    case RowMessage_Op_COMMIT: FALLTHROUGH_INTENDED;
    case RowMessage_Op_SAFEPOINT:
      return record->row_message().has_commit_time();
      break;
    case RowMessage_Op_INSERT: FALLTHROUGH_INTENDED;
    case RowMessage_Op_DELETE: FALLTHROUGH_INTENDED;
    case RowMessage_Op_UPDATE:
      return (
          record->row_message().has_commit_time() && record->row_message().has_record_time() &&
          record->cdc_sdk_op_id().has_write_id());
      break;
    case RowMessage_Op_TRUNCATE: FALLTHROUGH_INTENDED;
    case RowMessage_Op_READ: FALLTHROUGH_INTENDED;
    case RowMessage_Op_UNKNOWN:
      return false;
      break;
  }

  return false;
}

bool CDCSDKUniqueRecordID::lessThan(const std::shared_ptr<CDCSDKUniqueRecordID>& record) {
  if (this->commit_time_ != record->commit_time_) {
    return this->commit_time_ < record->commit_time_;
  }
  if (this->record_time_ != record->record_time_) {
    return this->record_time_ < record->record_time_;
  }
  if (this->write_id_ != record->write_id_) {
    return this->write_id_ < record->write_id_;
  }
  return this->tablet_id_ < record->tablet_id_;
}

}  // namespace cdc
}  // namespace yb
