// Copyright (c) YugaByte, Inc.

#ifndef YB_MASTER_YQL_PEERS_VTABLE_H
#define YB_MASTER_YQL_PEERS_VTABLE_H

#include "yb/master/master.h"
#include "yb/master/yql_virtual_table.h"

namespace yb {
namespace master {

// VTable implementation of system.peers.
class PeersVTable : public YQLVirtualTable {
 public:
  PeersVTable(const Schema& schema, Master* master_);
  CHECKED_STATUS RetrieveData(std::unique_ptr<YQLRowBlock>* vtable) const override;
 private:
  const Master *const master_;
  // Index of columns in the peers table.
  static constexpr size_t kPeerColumnIndex = 0; // peer column.
  static constexpr size_t kRpcAddrColumnIndex = 6; // rpc_address column.
  static constexpr size_t kSchemaVersionColumnIndex = 7; // schema_version column.
};

}  // namespace master
}  // namespace yb
#endif // YB_MASTER_YQL_PEERS_VTABLE_H
