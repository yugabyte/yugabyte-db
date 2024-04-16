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

#include "yb/cdc/cdc_service.pb.h"
#include "yb/common/entity_ids_types.h"

namespace yb {
namespace cdc {

class CDCSDKUniqueRecordID {
 public:
  explicit CDCSDKUniqueRecordID(
      const bool& publication_refresh_record, const std::shared_ptr<CDCSDKProtoRecordPB>& record);

  explicit CDCSDKUniqueRecordID(
      bool publication_refresh_record, RowMessage_Op op, uint64_t commit_time,
      std::string& docdb_txn_id, uint64_t record_time, uint32_t write_id, std::string& table_id,
      std::string& primary_key);

  enum VWALRecordType {
    // These are arranged in the priority order with DDL having the highest priority.
    DDL = 0,
    BEGIN = 1,
    DML = 2,
    COMMIT = 3,
    PUBLICATION_REFRESH = 4,
    SAFEPOINT = 5,
    UNKNOWN = 6 // should never be encountered
  };

  CDCSDKUniqueRecordID::VWALRecordType GetVWALRecordTypeFromOp(
      const bool& is_publication_refresh, const RowMessage_Op& op);

  static bool CanFormUniqueRecordId(
      const bool& is_publication_refresh_record,
      const std::shared_ptr<CDCSDKProtoRecordPB>& record);

  bool CompareDDLOrder(const std::shared_ptr<CDCSDKUniqueRecordID>& other_unique_record_id);

  // This comparator will be used by the Virtual WAL's Priority queue to sort records in the PQ.
  // Returns true iff this "HasHigherPriorityThan" other <=> this < other.
  bool HasHigherPriorityThan(const std::shared_ptr<CDCSDKUniqueRecordID>& other_unique_record_id);

  // This comparator will be used by the LSN generator. Returns true iff this
  // "GreaterThanDistributedLSN" other <=> this > other.
  bool GreaterThanDistributedLSN(
      const std::shared_ptr<CDCSDKUniqueRecordID>& other_unique_record_id);

  uint64_t GetCommitTime() const;

  std::string ToString() const;

  bool IsPublicationRefreshRecord() const;

 private:
  RowMessage_Op op_;
  VWALRecordType vwal_record_type_;
  uint64_t commit_time_;
  std::string docdb_txn_id_;
  uint64_t record_time_;
  uint32_t write_id_;
  std::string table_id_;
  std::string primary_key_;
};

}  // namespace cdc
}  // namespace yb
