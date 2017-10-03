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

#include "kudu/util/net/dns_resolver.h"

#include <boost/bind.hpp>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <vector>

#include "kudu/util/flag_tags.h"
#include "kudu/util/threadpool.h"
#include "kudu/util/net/net_util.h"
#include "kudu/util/net/sockaddr.h"

DEFINE_int32(dns_num_resolver_threads, 1, "The number of threads to use for DNS resolution");
TAG_FLAG(dns_num_resolver_threads, advanced);

using std::vector;

namespace kudu {

DnsResolver::DnsResolver() {
  CHECK_OK(ThreadPoolBuilder("dns-resolver")
           .set_max_threads(FLAGS_dns_num_resolver_threads)
           .Build(&pool_));
}

DnsResolver::~DnsResolver() {
  pool_->Shutdown();
}

namespace {
static void DoResolution(const HostPort &hostport, vector<Sockaddr>* addresses,
                         StatusCallback cb) {
  cb.Run(hostport.ResolveAddresses(addresses));
}
} // anonymous namespace

void DnsResolver::ResolveAddresses(const HostPort& hostport,
                                   vector<Sockaddr>* addresses,
                                   const StatusCallback& cb) {
  Status s = pool_->SubmitFunc(boost::bind(&DoResolution, hostport, addresses, cb));
  if (!s.ok()) {
    cb.Run(s);
  }
}

} // namespace kudu
