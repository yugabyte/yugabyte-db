// Copyright (c) YugaByte, Inc.

#ifndef YB_TABLET_ABSTRACT_TABLET_H
#define YB_TABLET_ABSTRACT_TABLET_H

#include "yb/common/redis_protocol.pb.h"
#include "yb/common/schema.h"
#include "yb/common/yql_storage_interface.h"

namespace yb {
namespace tablet {

class AbstractTablet {
 public:
  virtual ~AbstractTablet() {}

  virtual const Schema& SchemaRef() const = 0;

  virtual const common::YQLStorageIf& YQLStorage() const = 0;

  virtual TableType table_type() const = 0;

  virtual const std::string& tablet_id() const = 0;

  virtual CHECKED_STATUS HandleRedisReadRequest(
      HybridTime timestamp, const RedisReadRequestPB& redis_read_request,
      RedisResponsePB* response) = 0;

  virtual CHECKED_STATUS HandleYQLReadRequest(
      HybridTime timestamp, const YQLReadRequestPB& yql_read_request, YQLResponsePB* response,
      gscoped_ptr<faststring>* rows_data);

  virtual CHECKED_STATUS CreatePagingStateForRead(const YQLReadRequestPB& yql_read_request,
                                                  const YQLRowBlock& rowblock,
                                                  YQLResponsePB* response) const = 0;

  virtual void RegisterReaderTimestamp(HybridTime read_point) = 0;
  virtual void UnregisterReader(HybridTime read_point) = 0;
  virtual HybridTime SafeTimestampToRead() const = 0;
};

}  // namespace tablet
}  // namespace yb

#endif // YB_TABLET_ABSTRACT_TABLET_H
