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
#include "yb/util/format.h"
#include "yb/util/logging.h"

namespace yb {

namespace cdc {

using VWALRecordType = CDCSDKUniqueRecordID::VWALRecordType;

CDCSDKUniqueRecordID::CDCSDKUniqueRecordID(
    RowMessage_Op op, uint64_t commit_time, std::string& docdb_txn_id, uint64_t record_time,
    uint32_t write_id, std::string& table_id, std::string& primary_key)
    : op_(op),
      vwal_record_type_(GetVWALRecordTypeFromOp(op)),
      commit_time_(commit_time),
      docdb_txn_id_(docdb_txn_id),
      record_time_(record_time),
      write_id_(write_id),
      table_id_(table_id),
      primary_key_(primary_key) {}

CDCSDKUniqueRecordID::CDCSDKUniqueRecordID(const std::shared_ptr<CDCSDKProtoRecordPB>& record) {
  this->op_ = record->row_message().op();
  this->vwal_record_type_ = GetVWALRecordTypeFromOp(this->op_);
  this->commit_time_ = record->row_message().commit_time();
  if (record->row_message().has_transaction_id()) {
    this->docdb_txn_id_ = record->row_message().transaction_id();
  } else {
    this->docdb_txn_id_ = "";
  }
  switch (this->op_) {
    case RowMessage_Op_DDL: FALLTHROUGH_INTENDED;
    case RowMessage_Op_BEGIN:
      this->record_time_ = 0;
      this->write_id_ = 0;
      this->table_id_ = "";
      this->primary_key_ = "";
      break;
    case RowMessage_Op_SAFEPOINT: FALLTHROUGH_INTENDED;
    case RowMessage_Op_COMMIT:
      this->record_time_ = std::numeric_limits<uint64_t>::max();
      this->write_id_ = std::numeric_limits<uint32_t>::max();
      this->table_id_ = "";
      this->primary_key_ = "";
      break;
    case RowMessage_Op_INSERT: FALLTHROUGH_INTENDED;
    case RowMessage_Op_DELETE: FALLTHROUGH_INTENDED;
    case RowMessage_Op_UPDATE:
      this->record_time_ = record->row_message().record_time();
      this->write_id_ = record->cdc_sdk_op_id().write_id();
      this->table_id_ = record->row_message().table_id();
      this->primary_key_ = record->row_message().primary_key();
      break;
    case RowMessage_Op_UNKNOWN: FALLTHROUGH_INTENDED;
    case RowMessage_Op_TRUNCATE: FALLTHROUGH_INTENDED;
    case RowMessage_Op_READ:
      // This should never happen as we only invoke this constructor after ensuring that the value
      // is not one of these.
      LOG(FATAL) << "Unexpected record received: " << record->DebugString();
  }
}

uint64_t CDCSDKUniqueRecordID::GetCommitTime() const { return commit_time_; }

VWALRecordType CDCSDKUniqueRecordID::GetVWALRecordTypeFromOp(const RowMessage_Op op) {
  switch (op) {
    case RowMessage_Op_BEGIN:
      return VWALRecordType::BEGIN;
      break;
    case RowMessage_Op_INSERT: FALLTHROUGH_INTENDED;
    case RowMessage_Op_DELETE: FALLTHROUGH_INTENDED;
    case RowMessage_Op_UPDATE:
      return VWALRecordType::DML;
      break;
    case RowMessage_Op_COMMIT:
      return VWALRecordType::COMMIT;
      break;
    case RowMessage_Op_DDL:
      return VWALRecordType::DDL;
      break;
    case RowMessage_Op_SAFEPOINT:
      return VWALRecordType::SAFEPOINT;
      break;
    case RowMessage_Op_TRUNCATE: FALLTHROUGH_INTENDED;
    case RowMessage_Op_READ: FALLTHROUGH_INTENDED;
    case RowMessage_Op_UNKNOWN:
      // This should never happen as we only invoke the constructor that calls this method after
      // ensuring that the value is not one of these.
      LOG(FATAL) << "Unexpected RowMessage Op received to get VWAL record type: " << op;
      break;
  }

  // flow should never reach this point.
  return VWALRecordType::UNKNOWN;
}

bool CDCSDKUniqueRecordID::CanFormUniqueRecordId(
    const std::shared_ptr<CDCSDKProtoRecordPB>& record) {
  RowMessage_Op op = record->row_message().op();
  switch (op) {
    case RowMessage_Op_DDL:
      return record->row_message().has_commit_time() && record->row_message().has_table_id();
      break;
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
          record->cdc_sdk_op_id().has_write_id() && record->row_message().has_table_id() &&
          record->row_message().has_primary_key());
      break;
    case RowMessage_Op_TRUNCATE: FALLTHROUGH_INTENDED;
    case RowMessage_Op_READ: FALLTHROUGH_INTENDED;
    case RowMessage_Op_UNKNOWN:
      return false;
      break;
  }

  return false;
}

bool IsSafepointRecordType(const VWALRecordType vwal_record_type) {
  return vwal_record_type == VWALRecordType::SAFEPOINT;
}

bool IsBeginOrCommitRecordType(const VWALRecordType vwal_record_type) {
  return vwal_record_type == VWALRecordType::BEGIN || vwal_record_type == VWALRecordType::COMMIT;
}

// Return true iff, other_unique_record_id > this.unique_record_id
bool CDCSDKUniqueRecordID::HasHigherPriorityThan(
    const std::shared_ptr<CDCSDKUniqueRecordID>& other_unique_record_id) {
  if (this->commit_time_ != other_unique_record_id->commit_time_) {
    return this->commit_time_ < other_unique_record_id->commit_time_;
  }

  // Safepoint record should always get the lowest priority in PQ.
  if (IsSafepointRecordType(this->vwal_record_type_) ||
      IsSafepointRecordType(other_unique_record_id->vwal_record_type_)) {
    return !(IsSafepointRecordType(this->vwal_record_type_));
  }

  if (this->docdb_txn_id_ != other_unique_record_id->docdb_txn_id_) {
    return this->docdb_txn_id_ < other_unique_record_id->docdb_txn_id_;
  }

  if (this->record_time_ != other_unique_record_id->record_time_) {
    return this->record_time_ < other_unique_record_id->record_time_;
  }

  if (this->write_id_ != other_unique_record_id->write_id_) {
    return this->write_id_ < other_unique_record_id->write_id_;
  }

  if (this->table_id_ != other_unique_record_id->table_id_) {
    return this->table_id_ < other_unique_record_id->table_id_;
  }

  return this->primary_key_ < other_unique_record_id->primary_key_;
}

// Return true iff, this.unique_record_id > other_unique_record_id
bool CDCSDKUniqueRecordID::GreaterThanDistributedLSN(
    const std::shared_ptr<CDCSDKUniqueRecordID>& other_unique_record_id) {
  if (this->commit_time_ != other_unique_record_id->commit_time_) {
    return this->commit_time_ > other_unique_record_id->commit_time_;
  }

  // We have defined our priority order for record types if we tie on the commit_time. The below
  // check will also compare a SAFEPOINT record with another record even though the SAFEPOINT record
  // is never sent to LSN generator. We require this check here because we hold the commit record
  // for a txn until we are sure that all DMLs having the same commit_time are shipped. During this
  // process of holding the commit record, we peek the PQ and compare the held commit record's
  // unique id with the peeked entry that can be a SAFEPOINT record. Hence, we need this check so
  // that in the described case, we are able to ship the commit record.
  if (this->vwal_record_type_ != other_unique_record_id->vwal_record_type_) {
    return this->vwal_record_type_ > other_unique_record_id->vwal_record_type_;
  }

  // Skip comparing docdb_txn_id if either record is a BEGIN/COMMIT record since we want to
  // ship all txns with same commit_time in a single txn from VWAL. Therefore, from VWAL's
  // perspective, only 1 pair of BEGIN/COMMITs will be shipped for the pg_txn.
  if (!IsBeginOrCommitRecordType(this->vwal_record_type_) &&
      !IsBeginOrCommitRecordType(other_unique_record_id->vwal_record_type_)) {
    if (this->docdb_txn_id_ != other_unique_record_id->docdb_txn_id_) {
      return this->docdb_txn_id_ > other_unique_record_id->docdb_txn_id_;
    }
  }

  if (this->record_time_ != other_unique_record_id->record_time_) {
    return this->record_time_ > other_unique_record_id->record_time_;
  }

  if (this->write_id_ != other_unique_record_id->write_id_) {
    return this->write_id_ > other_unique_record_id->write_id_;
  }

  if (this->table_id_ != other_unique_record_id->table_id_) {
    return this->table_id_ > other_unique_record_id->table_id_;
  }

  return this->primary_key_ > other_unique_record_id->primary_key_;
}

std::string CDCSDKUniqueRecordID::ToString() const {
  std::string result = "";
  switch (op_) {
    case RowMessage_Op_DDL:
      result = Format("op: DDL");
      break;
    case RowMessage_Op_BEGIN:
      result = Format("op: BEGIN");
      break;
    case RowMessage_Op_COMMIT:
      result = Format("op: COMMIT");
      break;
    case RowMessage_Op_SAFEPOINT:
      result = Format("op: SAFEPOINT");
      break;
    case RowMessage_Op_INSERT:
      result = Format("op: INSERT");
      break;
    case RowMessage_Op_DELETE:
      result = Format("op: DELETE");
      break;
    case RowMessage_Op_UPDATE:
      result = Format("op: UPDATE");
      break;
    case RowMessage_Op_TRUNCATE:
      result = Format("op: TRUNCATE");
      break;
    case RowMessage_Op_READ:
      result = Format("op: READ");
      break;
    case RowMessage_Op_UNKNOWN:
      result = Format("op: UNKNOWN");
      break;
  }

  switch (vwal_record_type_) {
    case VWALRecordType::DDL:
      result = Format("RecordType: DDL");
      break;
    case VWALRecordType::BEGIN:
      result = Format("RecordType: BEGIN");
      break;
    case VWALRecordType::COMMIT:
      result = Format("RecordType: COMMIT");
      break;
    case VWALRecordType::DML:
      result = Format("RecordType: DML");
      break;
    case VWALRecordType::SAFEPOINT:
      result = Format("RecordType: SAFEPOINT");
      break;
    case UNKNOWN:
      // should never be encountered.
      result = Format("RecordType: UNKNOWN");
      break;
  }

  result += Format(", commit_time: $0", commit_time_);
  result += Format(", docdb_txn_id: $0", docdb_txn_id_);
  result += Format(", record_time: $0", record_time_);
  result += Format(", write_id: $0", write_id_);
  result += Format(", table_id: $0", table_id_);
  result += Format(", encoded_primary_key: $0", primary_key_);
  return result;
}

}  // namespace cdc
}  // namespace yb
