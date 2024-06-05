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
#pragma once

#include <future>

#include "yb/gutil/macros.h"
#include "yb/gutil/ref_counted.h"

#include "yb/util/status_fwd.h"
#include "yb/util/net/net_fwd.h"

namespace yb {

class EventStats;
class HostPort;
class ThreadPool;

using AsyncResolveCallback = std::function<void(const Result<IpAddress>& result)>;

// DNS Resolver which supports async address resolution.
class DnsResolver {
 public:
  DnsResolver(const DnsResolver&) = delete;
  void operator=(const DnsResolver&) = delete;

  explicit DnsResolver(IoService* io_service);
  ~DnsResolver();

  std::shared_future<Result<IpAddress>> ResolveFuture(const std::string& host);
  void AsyncResolve(const std::string& host, const AsyncResolveCallback& callback);
  Result<IpAddress> Resolve(const std::string& host);

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

class ScopedDnsTracker {
 public:
  explicit ScopedDnsTracker(const scoped_refptr<EventStats>& metric);
  ~ScopedDnsTracker();

  ScopedDnsTracker(const ScopedDnsTracker&) = delete;
  void operator=(const ScopedDnsTracker&) = delete;

  static EventStats* active_metric();
 private:
  EventStats* old_metric_;
  scoped_refptr<EventStats> metric_;
};

} // namespace yb
