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

#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include "yb/util/net/inetaddress.h"

using boost::asio::ip::address;
using boost::asio::ip::address_v4;
using boost::asio::ip::address_v6;
using boost::asio::ip::tcp;

namespace yb {

InetAddress::InetAddress() {
}

InetAddress::InetAddress(const boost::asio::ip::address& address)
    : boost_addr_(address) {
}

InetAddress::InetAddress(const InetAddress& other) {
  boost_addr_ = other.boost_addr_;
}

CHECKED_STATUS ResolveInternal(const std::string& host,
                               tcp::resolver::iterator* iter) {
  boost::system::error_code ec;
  boost::asio::io_service io;
  tcp::resolver resolver(io);
  // Port 80 is just a placeholder since boost doesn't seem to support a DNS lookup API without
  // the port.
  tcp::resolver::query query(host, "80");
  *iter = resolver.resolve(query, ec);
  if (ec.value()) {
    return STATUS_SUBSTITUTE(InvalidArgument, "$0 is an invalid host/ip address: $1", host,
                             ec.message());
  }
  return Status::OK();
}

CHECKED_STATUS InetAddress::Resolve(const std::string& host, std::vector<InetAddress>* addresses) {
  // Try to see if we already have an IP address.
  boost::system::error_code ec;
  boost::asio::ip::address addr = address::from_string(host, ec);
  if (ec.value()) {
    // Resolve the host if we don't have a valid IP addr notation string.
    tcp::resolver::iterator iter;
    RETURN_NOT_OK(ResolveInternal(host, &iter));
    tcp::resolver::iterator end;
    while (iter != end) {
      addresses->emplace_back(iter->endpoint().address());
      iter++;
    }
  } else {
    addresses->emplace_back(addr);
  }
  return Status::OK();
}

CHECKED_STATUS InetAddress::FromString(const std::string& strval) {
  // Try to see if we already have an IP address.
  boost::system::error_code ec;
  boost_addr_ = address::from_string(strval, ec);
  if (!ec.value()) {
    return Status::OK();
  }

  tcp::resolver::iterator iter;
  RETURN_NOT_OK(ResolveInternal(strval, &iter));
  if (iter == tcp::resolver::iterator()) {
    return STATUS_FORMAT(NotFound, "Unable to resolve address: $0", strval);
  }
  // Pick the first IP address in the list.
  boost_addr_ = iter->endpoint().address();

  return Status::OK();
}

std::string InetAddress::ToString() const {
  std::string strval;
  CHECK_OK(ToString(&strval));
  return strval;
}

CHECKED_STATUS InetAddress::ToString(std::string *strval) const {
  boost::system::error_code ec;
  *strval = boost_addr_.to_string(ec);
  if (ec.value()) {
    return STATUS(IllegalState, "InetAddress object cannot be converted to string: $0",
                  ec.message());
  }
  return Status::OK();
}

CHECKED_STATUS InetAddress::ToBytes(std::string* bytes) const {
  try {
    if (boost_addr_.is_v4()) {
      auto v4bytes = boost_addr_.to_v4().to_bytes();
      bytes->assign(reinterpret_cast<char *>(v4bytes.data()), v4bytes.size());
    } else if (boost_addr_.is_v6()) {
      auto v6bytes = boost_addr_.to_v6().to_bytes();
      bytes->assign(reinterpret_cast<char *>(v6bytes.data()), v6bytes.size());
    } else {
      return STATUS(Uninitialized, "InetAddress doesn't hold a valid IPv4 or IPv6 address");
    }
  } catch (std::exception& e) {
    return STATUS(Corruption, "Couldn't serialize InetAddress to raw bytes!");
  }
  return Status::OK();
}

CHECKED_STATUS InetAddress::FromSlice(const Slice& slice, size_t size_hint) {
  size_t expected_size = (size_hint == 0) ? slice.size() : size_hint;
  if (expected_size > slice.size()) {
    return STATUS_SUBSTITUTE(InvalidArgument, "Size of slice: $0 is smaller than provided "
        "size_hint: $1", slice.size(), expected_size);
  }
  if (expected_size == kInetAddressV4Size) {
    address_v4::bytes_type v4bytes;
    DCHECK_EQ(expected_size, v4bytes.size());
    memcpy(v4bytes.data(), slice.data(), v4bytes.size());
    address_v4 v4address(v4bytes);
    boost_addr_ = v4address;
  } else if (expected_size == kInetAddressV6Size) {
    address_v6::bytes_type v6bytes;
    DCHECK_EQ(expected_size, v6bytes.size());
    memcpy(v6bytes.data(), slice.data(), v6bytes.size());
    address_v6 v6address(v6bytes);
    boost_addr_ = v6address;
  } else {
    return STATUS_SUBSTITUTE(InvalidArgument, "Size of slice is invalid: $0", expected_size);
  }
  return Status::OK();
}

CHECKED_STATUS InetAddress::FromBytes(const std::string& bytes) {
  Slice slice (bytes.data(), bytes.size());
  return FromSlice(slice);
}

} // namespace yb
