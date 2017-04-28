// Copyright (c) YugaByte, Inc.

#ifndef YB_MASTER_SYSTEM_TABLET_H
#define YB_MASTER_SYSTEM_TABLET_H

#include "yb/common/entity_ids.h"
#include "yb/common/hybrid_time.h"
#include "yb/common/schema.h"
#include "yb/master/yql_virtual_table.h"
#include "yb/tablet/abstract_tablet.h"

namespace yb {
namespace master {

// This is a virtual tablet that is used for our virtual tables in the system namespace.
class SystemTablet : public tablet::AbstractTablet {
 public:
  SystemTablet(const Schema& schema, std::unique_ptr<YQLVirtualTable> yql_virtual_table,
               const TabletId& tablet_id);

  const Schema& SchemaRef() const override;

  const common::YQLStorageIf& YQLStorage() const override;

  TableType table_type() const override;

  const TabletId& tablet_id() const override;

  void RegisterReaderTimestamp(HybridTime read_point) override;
  void UnregisterReader(HybridTime read_point) override;
  HybridTime SafeTimestampToRead() const override;

  CHECKED_STATUS HandleRedisReadRequest(
      HybridTime timestamp, const RedisReadRequestPB& redis_read_request,
      RedisResponsePB* response) override;

  CHECKED_STATUS CreatePagingStateForRead(const YQLReadRequestPB& yql_read_request,
                                          const YQLRowBlock& rowblock,
                                          YQLResponsePB* response) const override;
  const TableName& GetTableName() const;
 private:
  Schema schema_;
  std::unique_ptr<YQLVirtualTable> yql_virtual_table_;
  TabletId tablet_id_;
};

}  // namespace master
}  // namespace yb
#endif // YB_MASTER_SYSTEM_TABLET_H
