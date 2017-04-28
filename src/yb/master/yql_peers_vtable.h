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
  explicit PeersVTable(const Master* const master_);
  CHECKED_STATUS RetrieveData(std::unique_ptr<YQLRowBlock>* vtable) const;

 protected:
  Schema CreateSchema() const;

 private:
  static constexpr const char* const kPeer = "peer";
  static constexpr const char* const kDataCenter = "data_center";
  static constexpr const char* const kHostId = "host_id";
  static constexpr const char* const kPreferredIp = "preferred_ip";
  static constexpr const char* const kRack = "rack";
  static constexpr const char* const kReleaseVersion = "release_version";
  static constexpr const char* const kRPCAddress = "rpc_address";
  static constexpr const char* const kSchemaVersion = "schema_version";
  static constexpr const char* const kTokens = "tokens";
};

}  // namespace master
}  // namespace yb
#endif // YB_MASTER_YQL_PEERS_VTABLE_H
