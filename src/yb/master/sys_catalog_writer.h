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

#ifndef YB_MASTER_SYS_CATALOG_WRITER_H
#define YB_MASTER_SYS_CATALOG_WRITER_H

#include "yb/common/common_fwd.h"
#include "yb/common/entity_ids.h"

#include "yb/master/catalog_entity_info.h"

#include "yb/tablet/tablet_fwd.h"

namespace yb {
namespace master {

bool IsWrite(QLWriteRequestPB::QLStmtType op_type);

class SysCatalogWriter {
 public:
  SysCatalogWriter(const TabletId& tablet_id, const Schema& schema_with_ids, int64_t leader_term);

  ~SysCatalogWriter() = default;

  template <class PersistentDataEntryClass>
  CHECKED_STATUS MutateItem(const MetadataCowWrapper<PersistentDataEntryClass>* item,
                            QLWriteRequestPB::QLStmtType op_type) {
    const auto& old_pb = item->metadata().state().pb;
    const auto& new_pb = IsWrite(op_type) ? item->metadata().dirty().pb : old_pb;
    return DoMutateItem(
        PersistentDataEntryClass::type(), item->id(), old_pb, new_pb, op_type);
  }

  // Insert a row into a Postgres sys catalog table.
  CHECKED_STATUS InsertPgsqlTableRow(const Schema& source_schema,
                                     const QLTableRow& source_row,
                                     const TableId& target_table_id,
                                     const Schema& target_schema,
                                     const uint32_t target_schema_version,
                                     bool is_upsert);

  const tserver::WriteRequestPB& req() const {
    return req_;
  }

  int64_t leader_term() const {
    return leader_term_;
  }

 private:
  CHECKED_STATUS DoMutateItem(
      int8_t type,
      const std::string& item_id,
      const google::protobuf::Message& prev_pb,
      const google::protobuf::Message& new_pb,
      QLWriteRequestPB::QLStmtType op_type);

  const Schema& schema_with_ids_;
  tserver::WriteRequestPB req_;
  const int64_t leader_term_;


  DISALLOW_COPY_AND_ASSIGN(SysCatalogWriter);
};

CHECKED_STATUS FillSysCatalogWriteRequest(
    int8_t type, const std::string& item_id, const google::protobuf::Message& new_pb,
    QLWriteRequestPB::QLStmtType op_type, const Schema& schema_with_ids, QLWriteRequestPB* req);

// Enumerate sys catalog calling provided callback for all entries of the specified type in sys
// catalog.
CHECKED_STATUS EnumerateSysCatalog(
    tablet::Tablet* tablet, const Schema& schema, int8_t entry_type,
    const std::function<Status(const Slice& id, const Slice& data)>& callback);

} // namespace master
} // namespace yb

#endif // YB_MASTER_SYS_CATALOG_WRITER_H
