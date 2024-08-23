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

#pragma once

#include <set>
#include <utility>

#include "yb/common/common_fwd.h"
#include "yb/common/entity_ids_types.h"
#include "yb/common/ql_protocol.pb.h"

#include "yb/docdb/docdb_fwd.h"

#include "yb/tablet/tablet_fwd.h"

#include "yb/tserver/tserver_fwd.h"

#include "yb/util/status.h"
#include "yb/util/type_traits.h"

namespace yb {
namespace master {

class TableInfo;
class TabletInfo;

bool IsWrite(QLWriteRequestPB::QLStmtType op_type);

class SysCatalogWriter {
 public:
  SysCatalogWriter(const Schema& schema_with_ids, int64_t leader_term);

  ~SysCatalogWriter();

  template <bool require_check>
  Status Mutate(QLWriteRequestPB::QLStmtType op_type) {
    return Status::OK();
  }

  template <bool require_check, class Item, class... Items>
  Status Mutate(
      QLWriteRequestPB::QLStmtType op_type, const Item& item, Items&&... items) {
    RETURN_NOT_OK(MutateHelper<require_check>(item, op_type, true /* skip_if_clean */));
    return Mutate<require_check>(op_type, std::forward<Items>(items)...);
  }

  Status Mutate(
      int8_t type, const std::string& item_id, const google::protobuf::Message& new_pb,
      QLWriteRequestPB::QLStmtType op_type);

  Status ForceMutate(QLWriteRequestPB::QLStmtType op_type) {
    return Status::OK();
  }

  template <class Item, class... Items>
  Status ForceMutate(
      QLWriteRequestPB::QLStmtType op_type, const Item& item, Items&&... items) {
    RETURN_NOT_OK(MutateHelper<false>(item, op_type, false /* skip_if_clean */));
    return ForceMutate(op_type, std::forward<Items>(items)...);
  }

  // Insert a row into a Postgres sys catalog table.
  Status InsertPgsqlTableRow(const Schema& source_schema,
                             const qlexpr::QLTableRow& source_row,
                             const TableId& target_table_id,
                             const Schema& target_schema,
                             const uint32_t target_schema_version,
                             bool is_upsert);

  // Delete a row in a Postgres sys catalog table.
  Status DeleteYsqlTableRow(const Schema& schema,
                            const qlexpr::QLTableRow& row,
                            const TableId& table_id,
                            const uint32_t schema_version);

  tserver::WriteRequestPB& req() {
    return *req_;
  }

  int64_t leader_term() const {
    return leader_term_;
  }

 private:
  template <bool require_check, class Item>
  Status MutateHelper(const Item* item, QLWriteRequestPB::QLStmtType op_type, bool skip_if_clean) {
    static_assert(
        !require_check ||
            (!std::is_same<Item, TableInfo>::value && !std::is_same<Item, TabletInfo>::value),
        "To write TableInfo or TabletInfo objects to the sys catalog, use the sys catalog mutate "
        "overloads which take a LeaderEpoch.");
    const auto& old_pb = item->old_pb();
    const auto& new_pb = IsWrite(op_type) ? item->new_pb() : old_pb;
    return DoMutateItem(Item::type(), item->id(), old_pb, new_pb, op_type, skip_if_clean);
  }

  template <bool require_check, class Item>
  Status MutateHelper(
      const scoped_refptr<Item>& item, QLWriteRequestPB::QLStmtType op_type, bool skip_if_clean) {
    return MutateHelper<require_check>(item.get(), op_type, skip_if_clean);
  }

  template <bool require_check, class Item>
  Status MutateHelper(
      const std::shared_ptr<Item>& item, QLWriteRequestPB::QLStmtType op_type, bool skip_if_clean) {
    return MutateHelper<require_check>(item.get(), op_type, skip_if_clean);
  }

  template <bool require_check, class Item>
  Status MutateHelper(
      const std::reference_wrapper<Item> item,
      QLWriteRequestPB::QLStmtType op_type, bool skip_if_clean) {
    return MutateHelper<require_check>(item.get(), op_type, skip_if_clean);
  }

  template <bool require_check, class Items>
  typename std::enable_if<IsCollection<Items>::value, Status>::type
  MutateHelper(
      const Items& items, QLWriteRequestPB::QLStmtType op_type, bool skip_if_clean) {
    for (const auto& item : items) {
      RETURN_NOT_OK(MutateHelper<require_check>(item, op_type, skip_if_clean));
    }
    return Status::OK();
  }

  Status DoMutateItem(
      int8_t type,
      const std::string& item_id,
      const google::protobuf::Message& prev_pb,
      const google::protobuf::Message& new_pb,
      QLWriteRequestPB::QLStmtType op_type,
      bool skip_if_clean);

  const Schema& schema_with_ids_;
  std::unique_ptr<tserver::WriteRequestPB> req_;
  const int64_t leader_term_;

  DISALLOW_COPY_AND_ASSIGN(SysCatalogWriter);
};

Status FillSysCatalogWriteRequest(
    int8_t type, const std::string& item_id, const google::protobuf::Message& new_pb,
    QLWriteRequestPB::QLStmtType op_type, const Schema& schema_with_ids, QLWriteRequestPB* req);

Status FillSysCatalogWriteRequest(
    int8_t type, const std::string& item_id, const Slice& data,
    QLWriteRequestPB::QLStmtType op_type, const Schema& schema_with_ids, QLWriteRequestPB* req);

using EnumerationCallback = std::function<Status(const Slice& id, const Slice& data)>;

// Enumerate sys catalog calling provided callback for all entries of the specified type in sys
// catalog.
Status EnumerateSysCatalog(
    tablet::Tablet* tablet, const Schema& schema, int8_t entry_type,
    const EnumerationCallback& callback);
Status EnumerateSysCatalog(
    docdb::DocRowwiseIterator* doc_iter, const Schema& schema, int8_t entry_type,
    const EnumerationCallback& callback);

using SysCatalogEntryCallback = std::function<
    Status(int8_t entry_type, const Slice& id, const Slice& data)>;

// Enumerate sys catalog calling provided callback for all entries in sys catalog.
Status EnumerateAllSysCatalogEntries(
    tablet::Tablet* tablet, const Schema& schema, const SysCatalogEntryCallback& callback);
Status EnumerateAllSysCatalogEntries(
    docdb::DocRowwiseIterator* doc_iter, const Schema& schema,
    const SysCatalogEntryCallback& callback);

} // namespace master
} // namespace yb
