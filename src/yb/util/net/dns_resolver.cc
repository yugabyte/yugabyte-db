// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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
//

#include "yb/util/net/dns_resolver.h"

#include <vector>

#include <gflags/gflags.h>
#include <glog/logging.h>

#include "yb/util/flag_tags.h"
#include "yb/util/threadpool.h"
#include "yb/util/net/net_util.h"
#include "yb/util/net/sockaddr.h"

DEFINE_int32(dns_num_resolver_threads, 1, "The number of threads to use for DNS resolution");
TAG_FLAG(dns_num_resolver_threads, advanced);

using std::vector;

namespace yb {

DnsResolver::DnsResolver() {
  CHECK_OK(ThreadPoolBuilder("dns-resolver")
           .set_max_threads(FLAGS_dns_num_resolver_threads)
           .Build(&pool_));
}

DnsResolver::~DnsResolver() {
  pool_->Shutdown();
}

namespace {

thread_local Histogram* active_metric_ = nullptr;

} // anonymous namespace

void DnsResolver::ResolveAddresses(const HostPort& hostport,
                                   std::vector<Endpoint>* addresses,
                                   const StatusCallback& cb) {
  ScopedLatencyMetric latency_metric(ScopedDnsTracker::active_metric(), Auto::kFalse);

  Status s = pool_->SubmitFunc(
      [hostport, addresses, cb, latency_metric = std::move(latency_metric)]() mutable {
    latency_metric.Restart();
    cb.Run(hostport.ResolveAddresses(addresses));
    latency_metric.Finish();
  });
  if (!s.ok()) {
    cb.Run(s);
  }
}

ScopedDnsTracker::ScopedDnsTracker(const scoped_refptr<Histogram>& metric)
    : old_metric_(active_metric()), metric_(metric) {
  active_metric_ = metric.get();
}

ScopedDnsTracker::~ScopedDnsTracker() {
  DCHECK_EQ(metric_.get(), active_metric());
  active_metric_ = old_metric_;
}

Histogram* ScopedDnsTracker::active_metric() {
  return active_metric_;
}

} // namespace yb
