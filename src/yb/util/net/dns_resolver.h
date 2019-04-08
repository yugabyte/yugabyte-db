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
#ifndef YB_UTIL_NET_DNS_RESOLVER_H
#define YB_UTIL_NET_DNS_RESOLVER_H

#include <vector>

#include "yb/gutil/gscoped_ptr.h"
#include "yb/gutil/macros.h"

#include "yb/util/async_util.h"
#include "yb/util/result.h"
#include "yb/util/status.h"

#include "yb/util/net/net_fwd.h"
#include "yb/util/net/inetaddress.h"

namespace yb {

class Histogram;
class HostPort;
class ThreadPool;

// DNS Resolver which supports async address resolution.
class DnsResolver {
 public:
  DnsResolver();
  ~DnsResolver();

  // Resolve any addresses corresponding to this host:port pair.
  // Note that a host may resolve to more than one IP address.
  //
  // 'addresses' may be NULL, in which case this function simply checks that
  // the host/port pair can be resolved, without returning anything.
  //
  // When the result is available, or an error occurred, 'cb' is called
  // with the result Status.
  //
  // NOTE: the callback should be fast since it is called by the DNS
  // resolution thread.
  // NOTE: in some rare cases, the callback may also be called inline
  // from this function call, on the caller's thread.
  void ResolveAddresses(const HostPort& hostport,
                        std::vector<Endpoint>* addresses,
                        const StatusCallback& cb);

 private:
  gscoped_ptr<ThreadPool> pool_;

  DISALLOW_COPY_AND_ASSIGN(DnsResolver);
};

class ScopedDnsTracker {
 public:
  explicit ScopedDnsTracker(const scoped_refptr<Histogram>& metric);
  ~ScopedDnsTracker();

  ScopedDnsTracker(const ScopedDnsTracker&) = delete;
  void operator=(const ScopedDnsTracker&) = delete;

  static Histogram* active_metric();
 private:
  Histogram* old_metric_;
  scoped_refptr<Histogram> metric_;
};

std::future<Result<InetAddress>> ResolveDnsFuture(const std::string& host, Resolver* resolver);

// Processes result of DNS resolution.
// Returns failure status if error happened.
// On success returns first ip v4 address if available, otherwise first ip v6 address is returned.
Result<InetAddress> PickResolvedAddress(
    const std::string& host, const boost::system::error_code& error,
    const ResolverResults& entries);

} // namespace yb

#endif /* YB_UTIL_NET_DNS_RESOLVER_H */
