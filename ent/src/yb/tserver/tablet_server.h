// Copyright (c) YugaByte, Inc.

#ifndef ENT_SRC_YB_TSERVER_TABLET_SERVER_H
#define ENT_SRC_YB_TSERVER_TABLET_SERVER_H

#include "../../../../src/yb/tserver/tablet_server.h"

namespace yb {

class UniverseKeyRegistryPB;

namespace enterprise {

class UniverseKeyManager;

}

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
  yb::enterprise::UniverseKeyManager* GetUniverseKeyManager();
  CHECKED_STATUS SetUniverseKeyRegistry(
      const yb::UniverseKeyRegistryPB& universe_key_registry) override;

 protected:
  CHECKED_STATUS RegisterServices() override;
  CHECKED_STATUS SetupMessengerBuilder(rpc::MessengerBuilder* builder) override;

 private:
  std::unique_ptr<rpc::SecureContext> secure_context_;
  // Object that manages the universe key registry used for encrypting and decrypting data keys.
  // Copies are given to each Env.
  std::shared_ptr<yb::enterprise::UniverseKeyManager> universe_key_manager_;
  // Encrypted env for all non-rocksdb file i/o operations.
  std::unique_ptr<yb::Env> env_;
  // Encrypted env for all rocksdb file i/o operations.
  std::unique_ptr<rocksdb::Env> rocksdb_env_;
};

} // namespace enterprise
} // namespace tserver
} // namespace yb

#endif // ENT_SRC_YB_TSERVER_TABLET_SERVER_H
