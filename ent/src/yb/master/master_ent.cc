// Copyright (c) YugaByte, Inc.

#include "yb/master/master.h"
#include "yb/util/flag_tags.h"

#include "yb/master/master_backup.service.h"
#include "yb/master/master_backup_service.h"

DEFINE_int32(master_backup_svc_queue_length, 50,
             "RPC queue length for master backup service");
TAG_FLAG(master_backup_svc_queue_length, advanced);

namespace yb {
namespace master {
namespace enterprise {

using yb::rpc::ServiceIf;

Status Master::RegisterServices() {
  std::unique_ptr<ServiceIf> master_backup_service(new MasterBackupServiceImpl(this));
  RETURN_NOT_OK(RpcAndWebServerBase::RegisterService(FLAGS_master_backup_svc_queue_length,
                                                     std::move(master_backup_service)));

  return super::RegisterServices();
}

} // namespace enterprise
} // namespace master
} // namespace yb
