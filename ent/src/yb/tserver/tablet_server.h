// Copyright (c) YugaByte, Inc.

#ifndef ENT_SRC_YB_TSERVER_TABLET_SERVER_H
#define ENT_SRC_YB_TSERVER_TABLET_SERVER_H

#include "../../../../src/yb/tserver/tablet_server.h"

namespace yb {

namespace rpc {

class SecureContext;

}

namespace tserver {
namespace enterprise {

class TabletServer : public yb::tserver::TabletServer {
  typedef yb::tserver::TabletServer super;
 public:
  explicit TabletServer(const TabletServerOptions& opts);
  TabletServer(const TabletServer&) = delete;
  void operator=(const TabletServer&) = delete;
  ~TabletServer();

  Env* GetEnv() override;
  rocksdb::Env* GetRocksDBEnv() override;

 protected:
  CHECKED_STATUS RegisterServices() override;
  CHECKED_STATUS SetupMessengerBuilder(rpc::MessengerBuilder* builder) override;

 private:
  std::unique_ptr<rpc::SecureContext> secure_context_;
  std::unique_ptr<yb::Env> env_;
  std::unique_ptr<rocksdb::Env> rocksdb_env_;
};

} // namespace enterprise
} // namespace tserver
} // namespace yb

#endif // ENT_SRC_YB_TSERVER_TABLET_SERVER_H
