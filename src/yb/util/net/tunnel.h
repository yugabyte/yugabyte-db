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
//

#pragma once

#include <boost/asio/io_context.hpp>

#include "yb/util/net/net_fwd.h"
#include "yb/util/status_fwd.h"

namespace yb {

// Check that we should accept connections from specified address.
typedef std::function<bool(const IpAddress&)> AddressChecker;

// Tunnel that accepts connections at local endpoints and transparently transfer traffic
// to remote endpoint.
class Tunnel {
 public:
  explicit Tunnel(boost::asio::io_context* io_context);
  ~Tunnel();

  // Listen local and forward data to/from remote.
  // If address_checker is specified, we use it to check whether we originating address of
  // connection is acceptable.
  Status Start(const Endpoint& local, const Endpoint& remote,
               AddressChecker address_checker = AddressChecker());
  void Shutdown();

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace yb
