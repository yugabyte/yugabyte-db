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

#include <netinet/in.h>

#include <iosfwd>
#include <string>

#include <boost/asio/ip/tcp.hpp>
#undef EV_ERROR // sys/event.h conflicts with libev

#include "yb/util/status_fwd.h"

namespace yb {

typedef boost::asio::ip::address IpAddress;
typedef boost::asio::ip::tcp::endpoint Endpoint;

std::string ToString(const Endpoint& endpoint);
Result<Endpoint> ParseEndpoint(const std::string& input, uint16_t default_port);

std::size_t hash_value(const IpAddress& address);
std::size_t hash_value(const Endpoint& endpoint);

class EndpointHash {
 public:
  typedef size_t result_type;

  result_type operator()(const Endpoint& endpoint) const {
    return hash_value(endpoint);
  }
};

class IpAddressHash {
 public:
  typedef size_t result_type;

  result_type operator()(const IpAddress& address) const {
    return hash_value(address);
  }
};

} // namespace yb
