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

#include <shared_mutex>
#include <unordered_set>

#include "yb/client/table_handle.h"

#include "yb/common/opid.h"

#include "yb/util/status.h"
#include "yb/gutil/thread_annotations.h"

namespace yb {

namespace client {
class YBClient;
}  // namespace client

namespace master {
class IsCreateTableDoneResponsePB;
class CreateTableRequestPB;
}  // namespace master

namespace cdc {
static const char* const kCdcStateTableName = "cdc_state";

YB_STRONGLY_TYPED_BOOL(SetActiveTimeToCurrent);

struct CDCStateTableKey {
  TabletId tablet_id;     // HashPrimaryKey
  xrepl::StreamId stream_id;  // PrimaryKey
  TableId colocated_table_id;  // PrimaryKey is StreamId_ColocatedTableId for colocated tables

  CDCStateTableKey(const TabletId& tablet_id, const xrepl::StreamId& stream_id)
      : tablet_id(tablet_id), stream_id(stream_id) {}

  CDCStateTableKey(
      const TabletId& tablet_id,
      const xrepl::StreamId& stream_id,
      const TableId& colocated_table_id)
      : tablet_id(tablet_id), stream_id(stream_id), colocated_table_id(colocated_table_id) {}

  std::string ToString() const;
  std::string CompositeStreamId() const;
  static Result<CDCStateTableKey> FromString(
      const TabletId& tablet_id, const std::string& composite_stream_id);
};

// CDCStateTableEntry represents the cdc_state table row. We can use this object to read and write
// data to the table. Non-key columns are stored as optional fields. During reads these will be
// populated only when they are part of the project. Null are represented as nullopt. During
// write\update any populated field (not nullopt) is written to the table.
struct CDCStateTableEntry {
  explicit CDCStateTableEntry(const TabletId& tablet_id, const xrepl::StreamId& stream_id)
      : key(tablet_id, stream_id) {}
  explicit CDCStateTableEntry(
      const TabletId& tablet_id,
      const xrepl::StreamId& stream_id,
      const TableId& colocated_table_id)
      : key(tablet_id, stream_id, colocated_table_id) {}

  explicit CDCStateTableEntry(const CDCStateTableKey& other) : key(other) {}
  explicit CDCStateTableEntry(CDCStateTableKey&& other) : key(std::move(other)) {}

  CDCStateTableKey key;
  std::optional<OpId> checkpoint;
  std::optional<uint64_t> last_replication_time;

  // CDC Data entries.
  std::optional<uint64_t> active_time;
  std::optional<uint64_t> cdc_sdk_safe_time;
  std::optional<std::string> snapshot_key;
  std::optional<uint64_t> confirmed_flush_lsn;
  std::optional<uint64_t> restart_lsn;
  std::optional<uint32_t> xmin;
  std::optional<uint64_t> record_id_commit_time;

  std::string ToString() const;
};

struct CDCStateTableEntrySelector {
  CDCStateTableEntrySelector() = default;
  CDCStateTableEntrySelector&& IncludeCheckpoint();
  CDCStateTableEntrySelector&& IncludeLastReplicationTime();
  CDCStateTableEntrySelector&& IncludeData();
  CDCStateTableEntrySelector&& IncludeAll();
  CDCStateTableEntrySelector&& IncludeActiveTime();
  CDCStateTableEntrySelector&& IncludeCDCSDKSafeTime();
  CDCStateTableEntrySelector&& IncludeSnapshotKey();
  CDCStateTableEntrySelector&& IncludeConfirmedFlushLSN();
  CDCStateTableEntrySelector&& IncludeRestartLSN();
  CDCStateTableEntrySelector&& IncludeXmin();
  CDCStateTableEntrySelector&& IncludeRecordIdCommitTime();
  std::unordered_set<std::string> columns_;
};

class CDCStateTableRange;

// CDCStateTable is a wrapper class to access the cdc_state table. This handle the YQL table schema
// creation, data serialization\deserialization and column name to id conversions. It internally
// uses the YBClient and YBSession to access the table.
class CDCStateTable {
 public:
  explicit CDCStateTable(std::shared_future<client::YBClient*> client_future);
  explicit CDCStateTable(client::YBClient* client);

  static const std::string& GetNamespaceName();
  static const std::string& GetTableName();
  static Result<master::CreateTableRequestPB> GenerateCreateCdcStateTableRequest();

  Status InsertEntries(const std::vector<CDCStateTableEntry>& entries) EXCLUDES(mutex_);
  Status InsertEntriesAsync(
      const std::vector<CDCStateTableEntry>& entries, StdStatusCallback callback) EXCLUDES(mutex_);
  Status UpdateEntries(
      const std::vector<CDCStateTableEntry>& entries, const bool replace_full_map = false,
      const std::vector<std::string>& keys_to_delete = {}) EXCLUDES(mutex_);
  Status UpsertEntries(
      const std::vector<CDCStateTableEntry>& entries, const bool replace_full_map = false,
      const std::vector<std::string>& keys_to_delete = {}) EXCLUDES(mutex_);
  Status DeleteEntries(const std::vector<CDCStateTableKey>& entry_keys) EXCLUDES(mutex_);

  Result<CDCStateTableRange> GetTableRange(
      CDCStateTableEntrySelector&& field_filter, Status* iteration_status) EXCLUDES(mutex_);
  // Returns early if the CDC state table doesn't exist.
  Result<CDCStateTableRange> GetTableRangeAsync(
      CDCStateTableEntrySelector&& field_filter, Status* iteration_status) EXCLUDES(mutex_);

  // Get a single row from the table. If the row is not found, returns an nullopt.
  Result<std::optional<CDCStateTableEntry>> TryFetchEntry(
      const CDCStateTableKey& key, CDCStateTableEntrySelector&& field_filter = {}) EXCLUDES(mutex_);

 private:
  Result<client::YBClient*> GetClient();
  Result<std::shared_ptr<client::YBSession>> GetSession();
  Status WaitForCreateTableToFinishWithCache() REQUIRES(mutex_);
  Status WaitForCreateTableToFinishWithoutCache();
  Result<std::shared_ptr<client::TableHandle>> GetTable() EXCLUDES(mutex_);
  Status OpenTable(client::TableHandle* cdc_table);
  template <class CDCEntry>
  Status WriteEntriesAsync(
      const std::vector<CDCEntry>& entries, QLWriteRequestPB::QLStmtType statement_type,
      StdStatusCallback callback, QLOperator condition_op = QL_OP_NOOP,
      const bool replace_full_map = false, const std::vector<std::string>& keys_to_delete = {})
      EXCLUDES(mutex_);

  template <class CDCEntry>
  Status WriteEntries(
      const std::vector<CDCEntry>& entries, QLWriteRequestPB::QLStmtType statement_type,
      QLOperator condition_op = QL_OP_NOOP, const bool replace_full_map = false,
      const std::vector<std::string>& keys_to_delete = {}) EXCLUDES(mutex_);

  std::shared_mutex mutex_;
  std::shared_future<client::YBClient*> client_future_;
  client::YBClient* client_ = nullptr;

  std::shared_ptr<client::TableHandle> cdc_table_ GUARDED_BY(mutex_);
  bool created_ GUARDED_BY(mutex_) = false;
};

class CdcStateTableIterator : public client::TableIterator {
 public:
  CdcStateTableIterator() = default;
  explicit CdcStateTableIterator(
      const client::TableHandle* table, const client::TableIteratorOptions& options)
      : TableIterator(table, options), columns_(&options.columns.value()) {}

  Result<CDCStateTableEntry> operator*() const;

 private:
  const std::vector<std::string>* columns_ = nullptr;
};

// CDCStateTableRange a TableRange to iterate over the rows of the table. This supports both
// projection and filtering of the rows.
class CDCStateTableRange {
 public:
  typedef CdcStateTableIterator const_iterator;
  typedef CdcStateTableIterator iterator;

  explicit CDCStateTableRange(
      const std::shared_ptr<client::TableHandle>& table, Status* failure_status,
      std::vector<std::string>&& columns);

  const_iterator begin() const { return CdcStateTableIterator(table_.get(), options_); }
  const_iterator end() const { return CdcStateTableIterator(); }

 private:
  std::shared_ptr<client::TableHandle> table_;
  client::TableIteratorOptions options_;
};

}  // namespace cdc
}  // namespace yb
