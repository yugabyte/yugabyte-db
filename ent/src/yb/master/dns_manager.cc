// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.

#include "yb/master/dns_manager.h"

#include <aws/route53/model/ChangeResourceRecordSetsRequest.h>

#include "yb/common/wire_protocol.h"

#include "yb/master/master.pb.h"
#include "yb/master/ts_descriptor.h"
#include "yb/util/net/net_util.h"
#include "yb/util/tostring.h"

DECLARE_string(tservers_domain_name);
DECLARE_string(tservers_route53_hosted_zone_id);

using Aws::Route53::Model::Change;
using Aws::Route53::Model::ChangeAction;
using Aws::Route53::Model::ChangeBatch;
using Aws::Route53::Model::ChangeResourceRecordSetsRequest;
using Aws::Route53::Model::ResourceRecord;
using Aws::Route53::Model::ResourceRecordSet;
using Aws::Route53::Model::RRType;

namespace yb {
namespace master {
namespace enterprise {

const MonoDelta DnsManager::kDnsSyncPeriod = MonoDelta::FromSeconds(5);

DnsManager::DnsManager() : pool_(1) {
  Aws::InitAPI(aws_options_);
  route53_client_ = Aws::Route53::Route53Client();
}

DnsManager::~DnsManager() {
  route53_client_.reset();
  Aws::ShutdownAPI(aws_options_);
  pool_.Shutdown();
  pool_.Join();
}

bool DnsManager::AreLiveTServersChanged(
    const std::vector<std::shared_ptr<yb::master::TSDescriptor>>& live_tservers) {
  if (live_tservers.size() != live_tserver_hosts_.size()) {
    return true;
  }

  TSRegistrationPB reg;
  for (const auto& ts_desc : live_tservers) {
    ts_desc->GetRegistration(&reg);
    const auto& addr = DesiredHostPort(reg.common(), CloudInfoPB());
    if (addr.host().empty()) {
      LOG(ERROR) << "Unable to find TS address: " << reg.DebugString();
    } else if (!live_tserver_hosts_.count(addr.host())) {
      return true;
    }
  }
  return false;
}

void DnsManager::MayBeUpdateTServersDns(
    const std::vector<std::shared_ptr<yb::master::TSDescriptor>>& live_tservers) {
  if (!AreLiveTServersChanged(live_tservers)) {
    return;
  }

  if (!tservers_dns_update_started_.exchange(true)) {
    live_tserver_hosts_new_.clear();
    TSRegistrationPB reg;
    for (const auto& ts_desc : live_tservers) {
      ts_desc->GetRegistration(&reg);
      const auto& host = DesiredHostPort(reg.common(), CloudInfoPB()).host();
      if (!host.empty()) {
        live_tserver_hosts_new_.push_back(host);
      }
    }
    pool_.io_service().post(std::bind(&DnsManager::UpdateTServersDns, this));
  }
}

void DnsManager::UpdateTServersDns() {
  LOG(INFO) << "Live tservers hosts changed, updating tservers DNS record with: "
            << yb::ToString(live_tserver_hosts_new_) << "...";

  ResourceRecordSet record_set;
  record_set
      .WithType(RRType::A)
      .WithTTL(kDnsSyncPeriod.ToSeconds())
      .WithName(FLAGS_tservers_domain_name);
  for (const auto& host : live_tserver_hosts_new_) {
    auto addr = HostToAddress(host);
    if (addr.ok()) {
      ResourceRecord record;
      record_set.AddResourceRecords(std::move(record.WithValue(addr->to_string().c_str())));
    } else {
      LOG(ERROR) << addr.status();
    }
  }
  Change change;
  change
      .WithAction(Aws::Route53::Model::ChangeAction::UPSERT)
      .WithResourceRecordSet(record_set);
  ChangeBatch batch;
  batch.AddChanges(change);
  ChangeResourceRecordSetsRequest request;
  request
      .WithChangeBatch(batch)
      .WithHostedZoneId(FLAGS_tservers_route53_hosted_zone_id);

  auto response = route53_client_->ChangeResourceRecordSets(request);
  if (response.IsSuccess()) {
    live_tserver_hosts_.clear();
    live_tserver_hosts_.insert(live_tserver_hosts_new_.begin(), live_tserver_hosts_new_.end());
    LOG(INFO) << "tservers DNS record has been updated.";
  } else {
    LOG(ERROR) << "Failed to update tservers DNS record: " << response.GetError().GetMessage();
  }

  tservers_dns_update_started_.store(false);
}

} // namespace enterprise
} // namespace master
} // namespace yb
