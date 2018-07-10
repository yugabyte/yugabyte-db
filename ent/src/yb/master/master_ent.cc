// Copyright (c) YugaByte, Inc.

#include "yb/master/master.h"

#include "yb/master/master_backup.service.h"
#include "yb/master/master_backup_service.h"

#include "yb/rpc/secure_stream.h"

#include "yb/server/hybrid_clock.h"
#include "yb/server/secure.h"

#include "yb/util/flag_tags.h"
#include "yb/util/ntp_clock.h"

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

Master::Master(const MasterOptions& opts) : super(opts) {
}

Master::~Master() {
}

Status Master::RegisterServices() {
  server::HybridClock::RegisterProvider(NtpClock::Name(), [] {
    return std::make_shared<NtpClock>();
  });

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

Status Master::SetupMessengerBuilder(rpc::MessengerBuilder* builder) {
  RETURN_NOT_OK(super::SetupMessengerBuilder(builder));
  secure_context_ = VERIFY_RESULT(server::SetupSecureContext(
      options_.rpc_opts.rpc_bind_addresses, fs_manager_.get(),
      server::SecureContextType::kServerToServer, builder));
  return Status::OK();
}

} // namespace enterprise
} // namespace master
} // namespace yb
