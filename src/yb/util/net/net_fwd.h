//
// Copyright (c) YugaByte, Inc.
//

#ifndef YB_UTIL_NET_NET_FWD_H
#define YB_UTIL_NET_NET_FWD_H

namespace boost {
namespace asio {
namespace ip {

class address;

template <typename InternetProtocol>
class basic_endpoint;

class tcp;

} // namespace ip
} // namespace asio
} // namespace boost

namespace yb {

typedef boost::asio::ip::address IpAddress;
typedef boost::asio::ip::basic_endpoint<boost::asio::ip::tcp> Endpoint;

} // namespace yb

#endif // YB_UTIL_NET_NET_FWD_H
