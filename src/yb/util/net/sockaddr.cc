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
#include "yb/util/net/sockaddr.h"

#include <stdio.h>
#include <string.h>

#include <string>

#include <boost/functional/hash.hpp>
#include <boost/lexical_cast.hpp>

#include "yb/gutil/macros.h"
#include "yb/gutil/stringprintf.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"

namespace yb {

std::string ToString(const Endpoint& endpoint) {
  return boost::lexical_cast<std::string>(endpoint);
}

std::size_t hash_value(const IpAddress& address) {
  size_t seed = 0;

  if (address.is_v4()) {
    boost::hash_combine(seed, address.to_v4().to_ulong());
  } else {
    boost::hash_combine(seed, address.to_v6().to_bytes());
  }

  return seed;
}

std::size_t hash_value(const Endpoint& endpoint) {
  size_t seed = 0;

  boost::hash_combine(seed, hash_value(endpoint.address()));
  boost::hash_combine(seed, endpoint.port());

  return seed;
}

Result<Endpoint> ParseEndpoint(const std::string& input, uint16_t default_port) {
  boost::system::error_code ec;
  // First of all we try to parse whole string as address w/o port.
  // Important for IPv6 addesses like fe80::1:1.
  auto address = IpAddress::from_string(input, ec);
  if (!ec) {
    return Endpoint(address, default_port);
  }

  std::string::size_type pos = 0;
  std::string::size_type address_begin, address_end;

  if (!input.empty() && input[0] == '[') {
    pos = input.find(']');
    if (pos != std::string::npos) {
      address_begin = 1;
      address_end = pos;
      if (++pos >= input.size()) {
        pos = std::string::npos;
      }
    } else {
      return STATUS_SUBSTITUTE(NetworkError, "']' missing in $0", input);
    }
  } else {
    address_begin = 0;
    pos = input.find(':');
    address_end = pos != std::string::npos ? pos : input.size();
  }
  auto address_str = input.substr(address_begin, address_end - address_begin);
  address = IpAddress::from_string(address_str, ec);
  if (ec) {
    return STATUS_FORMAT(NetworkError, "Failed to parse $0: $1", input, ec);
  }
  if (pos == std::string::npos) {
    return Endpoint(address, default_port);
  }
  if (input[pos] != ':') {
    return STATUS_SUBSTITUTE(NetworkError, "':' missing after ']' in $0", input);
  }
  ++pos;
  if (pos == input.size()) {
    return STATUS_SUBSTITUTE(NetworkError, "Port not specified in $0", input);
  }
  char *end = nullptr;
  auto port = strtoul(input.c_str() + pos, &end, 10);
  if (port > 0xffff || end != input.c_str() + input.size()) {
    return STATUS_SUBSTITUTE(NetworkError, "Invalid port in $0", input);
  }
  return Endpoint(address, port);
}

} // namespace yb
