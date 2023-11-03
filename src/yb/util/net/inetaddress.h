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

#include <string.h>

#include <functional>
#include <string>
#include <vector>

#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/ip/address_v6.hpp>
#include <boost/system/error_code.hpp>

#include "yb/gutil/stringprintf.h"

#include "yb/util/status_fwd.h"
#include "yb/util/slice.h"

namespace yb {

constexpr size_t kInetAddressV4Size = 4;
constexpr size_t kInetAddressV6Size = 16;

// Generic class that encapsulates IPv4 and IPv6 addresses and uses the boost implementation
// underneath.
class InetAddress {
 public:
  InetAddress();

  explicit InetAddress(const boost::asio::ip::address& address);

  InetAddress(const InetAddress& other);

  // Fills in strval with the string representation of an IPv4 or IPv6 address.
  Status ToString(std::string* strval) const;

  // Returns string representation of an IPv4 or IPv6 address. This method doesn't return a
  // Status for usecases in the code where we don't support returning a status.
  std::string ToString() const;

  // Fills in the given string with the raw bytes for the appropriate address in network byte order.
  template<class Buffer>
  void AppendToBytes(Buffer* bytes) const {
    if (boost_addr_.is_v4()) {
      auto v4bytes = boost_addr_.to_v4().to_bytes();
      bytes->append(reinterpret_cast<char *>(v4bytes.data()), v4bytes.size());
    } else {
      auto v6bytes = boost_addr_.to_v6().to_bytes();
      bytes->append(reinterpret_cast<char *>(v6bytes.data()), v6bytes.size());
    }
  }

  std::string ToBytes() const;

  // Give a slice holding raw bytes in network byte order, build the appropriate InetAddress
  // object. If size_hint is specified, it indicates the number of bytes to decode from the slice.
  Status FromSlice(const Slice& slice, size_t size_hint = 0);

  const boost::asio::ip::address& address() const {
    return boost_addr_;
  }

  bool isV4() const;

  bool isV6() const;

  bool operator==(const InetAddress& other) const {
    return (boost_addr_ == other.boost_addr_);
  }

  bool operator!=(const InetAddress& other) const {
    return !(*this == other);
  }

  bool operator<(const InetAddress& other) const;

  bool operator>(const InetAddress& other) const {
    return (other < *this);
  }

  bool operator<=(const InetAddress& other) const {
    return !(other < *this);
  }

  bool operator>=(const InetAddress& other) const {
    return !(*this < other);
  }

  InetAddress& operator=(const InetAddress& other) {
    boost_addr_ = other.boost_addr_;
    return *this;
  }

 private:
  boost::asio::ip::address boost_addr_;
};

void FilterAddresses(const std::string &transform_spec,
                     std::vector<boost::asio::ip::address> *addresses);

} // namespace yb
