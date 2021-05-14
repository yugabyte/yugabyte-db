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

#ifndef YB_MASTER_SYS_CATALOG_INTERNAL_H_
#define YB_MASTER_SYS_CATALOG_INTERNAL_H_

#include "yb/common/ql_expr.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/sys_catalog_writer.h"
#include "yb/tserver/tserver.pb.h"
#include "yb/util/debug/trace_event.h"
#include "yb/util/pb_util.h"
#include "yb/master/sys_catalog_constants.h"
#include "yb/master/sys_catalog.h"

namespace yb {
namespace master {

class VisitorBase {
 public:
  VisitorBase() {}
  virtual ~VisitorBase() = default;

  virtual int entry_type() const = 0;

  virtual CHECKED_STATUS Visit(Slice id, Slice data) = 0;

 protected:
};

template <class PersistentDataEntryClass>
class Visitor : public VisitorBase {
 public:
  Visitor() {}
  virtual ~Visitor() = default;

  virtual CHECKED_STATUS Visit(Slice id, Slice data) {
    typename PersistentDataEntryClass::data_type metadata;
    RETURN_NOT_OK_PREPEND(
        pb_util::ParseFromArray(&metadata, data.data(), data.size()),
        "Unable to parse metadata field for item id: " + id.ToBuffer());

    return Visit(id.ToBuffer(), metadata);
  }

  int entry_type() const { return PersistentDataEntryClass::type(); }

 protected:
  virtual CHECKED_STATUS Visit(
      const std::string& id, const typename PersistentDataEntryClass::data_type& metadata) = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(Visitor);
};

// Template method defintions must go into a header file.
template <class Item>
CHECKED_STATUS SysCatalogTable::AddItem(Item* item, int64_t leader_term) {
  TRACE_EVENT1("master", "SysCatalogTable::Add",
               "table", item->ToString());
  return AddItems(std::vector<Item*>({ item }), leader_term);
}

template <class Item>
CHECKED_STATUS SysCatalogTable::AddItems(const vector<Item>& items, int64_t leader_term) {
  return MutateItems(items, QLWriteRequestPB::QL_STMT_INSERT, leader_term);
}

template <class Item>
CHECKED_STATUS SysCatalogTable::AddAndUpdateItems(
    const vector<Item>& added_items,
    const vector<Item>& updated_items,
    int64_t leader_term) {
  auto w = NewWriter(leader_term);
  for (const auto& item : added_items) {
    RETURN_NOT_OK(w->MutateItem(item, QLWriteRequestPB::QL_STMT_INSERT));
  }
  for (const auto& item : updated_items) {
    RETURN_NOT_OK(w->MutateItem(item, QLWriteRequestPB::QL_STMT_UPDATE));
  }
  return SyncWrite(w.get());
}

template <class Item>
CHECKED_STATUS SysCatalogTable::UpdateItem(Item* item, int64_t leader_term) {
  TRACE_EVENT1("master", "SysCatalogTable::Update",
               "table", item->ToString());
  return UpdateItems(std::vector<Item*>({ item }), leader_term);
}

template <class Item>
CHECKED_STATUS SysCatalogTable::UpdateItems(const vector<Item>& items, int64_t leader_term) {
  return MutateItems(items, QLWriteRequestPB::QL_STMT_UPDATE, leader_term);
}

template <class Item>
CHECKED_STATUS SysCatalogTable::DeleteItem(Item* item, int64_t leader_term) {
  TRACE_EVENT1("master", "SysCatalogTable::Delete",
               "table", item->ToString());
  return DeleteItems(std::vector<Item*>({ item }), leader_term);
}

template <class Item>
CHECKED_STATUS SysCatalogTable::DeleteItems(const vector<Item>& items, int64_t leader_term) {
  return MutateItems(items, QLWriteRequestPB::QL_STMT_DELETE, leader_term);
}

template <class Item>
CHECKED_STATUS SysCatalogTable::MutateItems(
    const vector<Item>& items, const QLWriteRequestPB::QLStmtType& op_type, int64_t leader_term) {
  auto w = NewWriter(leader_term);
  for (const auto& item : items) {
    RETURN_NOT_OK(w->MutateItem(item, op_type));
  }
  return SyncWrite(w.get());
}

std::unique_ptr<SysCatalogWriter> SysCatalogTable::NewWriter(int64_t leader_term) {
  return std::make_unique<SysCatalogWriter>(kSysCatalogTabletId, schema_, leader_term);
}

} // namespace master
} // namespace yb

#endif // YB_MASTER_SYS_CATALOG_INTERNAL_H_
