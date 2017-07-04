// Copyright (c) YugaByte, Inc.

#include "yb/tserver/tablet_server.h"
#include "yb/tserver/backup_service.h"
#include "yb/util/flag_tags.h"

DEFINE_int32(ts_backup_svc_num_threads, 4,
             "Number of RPC worker threads for the TS backup service");
TAG_FLAG(ts_backup_svc_num_threads, advanced);

DEFINE_int32(ts_backup_svc_queue_length, 50,
             "RPC queue length for the TS backup service");
TAG_FLAG(ts_backup_svc_queue_length, advanced);

namespace yb {
namespace tserver {
namespace enterprise {

using yb::rpc::ServiceIf;

Status TabletServer::RegisterServices() {
  std::unique_ptr<ServiceIf> backup_service(
      new TabletServiceBackupImpl(tablet_manager_.get(), metric_entity()));
  RETURN_NOT_OK(RpcAndWebServerBase::RegisterService(FLAGS_ts_backup_svc_queue_length,
                                                     std::move(backup_service)));

  return super::RegisterServices();
}

} // namespace enterprise
} // namespace tserver
} // namespace yb
