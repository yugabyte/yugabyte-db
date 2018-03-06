// Copyright (c) YugaByte, Inc.

#include "yb/master/master.h"
#include "yb/util/flag_tags.h"

#include "yb/master/master_backup.service.h"
#include "yb/master/master_backup_service.h"

DEFINE_int32(master_backup_svc_queue_length, 50,
             "RPC queue length for master backup service");
TAG_FLAG(master_backup_svc_queue_length, advanced);

DEFINE_string(tservers_domain_name, "",
              "Domain name to be updated to point to tservers IPs");
TAG_FLAG(tservers_domain_name, advanced);

DEFINE_string(tservers_route53_hosted_zone_id, "",
             "Route53 hosted zone ID to place tservers domain name");
TAG_FLAG(tservers_route53_hosted_zone_id, advanced);

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

void Master::OnTSHeartbeat(
    const std::vector<std::shared_ptr<yb::master::TSDescriptor>>& live_tservers) {
  if (!FLAGS_tservers_domain_name.empty() && !FLAGS_tservers_route53_hosted_zone_id.empty()) {
    auto now = MonoTime::Now();
    if (now > last_dns_sync_.load() + DnsManager::kDnsSyncPeriod
        && !dns_sync_in_progress_.exchange(true)) {
      last_dns_sync_.store(now);
      dns_manager_.MayBeUpdateTServersDns(live_tservers);
      dns_sync_in_progress_.store(false);
    }
  }
}

} // namespace enterprise
} // namespace master
} // namespace yb
