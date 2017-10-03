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

#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include <algorithm>
#include <unordered_set>
#include <utility>
#include <vector>

#include "kudu/gutil/gscoped_ptr.h"
#include "kudu/gutil/map-util.h"
#include "kudu/gutil/strings/join.h"
#include "kudu/gutil/strings/numbers.h"
#include "kudu/gutil/strings/split.h"
#include "kudu/gutil/strings/strip.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/gutil/strings/util.h"
#include "kudu/util/debug/trace_event.h"
#include "kudu/util/errno.h"
#include "kudu/util/faststring.h"
#include "kudu/util/net/net_util.h"
#include "kudu/util/net/sockaddr.h"
#include "kudu/util/stopwatch.h"
#include "kudu/util/subprocess.h"

// Mac OS 10.9 does not appear to define HOST_NAME_MAX in unistd.h
#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX 64
#endif

using std::unordered_set;
using std::vector;
using strings::Substitute;

namespace kudu {

namespace {
struct AddrinfoDeleter {
  void operator()(struct addrinfo* info) {
    freeaddrinfo(info);
  }
};
}

HostPort::HostPort()
  : host_(""),
    port_(0) {
}

HostPort::HostPort(std::string host, uint16_t port)
    : host_(std::move(host)), port_(port) {}

HostPort::HostPort(const Sockaddr& addr)
  : host_(addr.host()),
    port_(addr.port()) {
}

Status HostPort::ParseString(const string& str, uint16_t default_port) {
  std::pair<string, string> p = strings::Split(str, strings::delimiter::Limit(":", 1));

  // Strip any whitespace from the host.
  StripWhiteSpace(&p.first);

  // Parse the port.
  uint32_t port;
  if (p.second.empty() && strcount(str, ':') == 0) {
    // No port specified.
    port = default_port;
  } else if (!SimpleAtoi(p.second, &port) ||
             port > 65535) {
    return Status::InvalidArgument("Invalid port", str);
  }

  host_.swap(p.first);
  port_ = port;
  return Status::OK();
}

Status HostPort::ResolveAddresses(vector<Sockaddr>* addresses) const {
  TRACE_EVENT1("net", "HostPort::ResolveAddresses",
               "host", host_);
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  struct addrinfo* res = nullptr;
  int rc;
  LOG_SLOW_EXECUTION(WARNING, 200,
                     Substitute("resolving address for $0", host_)) {
    rc = getaddrinfo(host_.c_str(), nullptr, &hints, &res);
  }
  if (rc != 0) {
    return Status::NetworkError(
      StringPrintf("Unable to resolve address '%s'", host_.c_str()),
      gai_strerror(rc));
  }
  gscoped_ptr<addrinfo, AddrinfoDeleter> scoped_res(res);
  for (; res != nullptr; res = res->ai_next) {
    CHECK_EQ(res->ai_family, AF_INET);
    struct sockaddr_in* addr = reinterpret_cast<struct sockaddr_in*>(res->ai_addr);
    addr->sin_port = htons(port_);
    Sockaddr sockaddr(*addr);
    if (addresses) {
      addresses->push_back(sockaddr);
    }
    VLOG(2) << "Resolved address " << sockaddr.ToString()
            << " for host/port " << ToString();
  }
  return Status::OK();
}

Status HostPort::ParseStrings(const string& comma_sep_addrs,
                              uint16_t default_port,
                              vector<HostPort>* res) {
  vector<string> addr_strings = strings::Split(comma_sep_addrs, ",", strings::SkipEmpty());
  for (const string& addr_string : addr_strings) {
    HostPort host_port;
    RETURN_NOT_OK(host_port.ParseString(addr_string, default_port));
    res->push_back(host_port);
  }
  return Status::OK();
}

string HostPort::ToString() const {
  return Substitute("$0:$1", host_, port_);
}

string HostPort::ToCommaSeparatedString(const vector<HostPort>& hostports) {
  vector<string> hostport_strs;
  for (const HostPort& hostport : hostports) {
    hostport_strs.push_back(hostport.ToString());
  }
  return JoinStrings(hostport_strs, ",");
}

bool IsPrivilegedPort(uint16_t port) {
  return port <= 1024 && port != 0;
}

Status ParseAddressList(const std::string& addr_list,
                        uint16_t default_port,
                        std::vector<Sockaddr>* addresses) {
  vector<HostPort> host_ports;
  RETURN_NOT_OK(HostPort::ParseStrings(addr_list, default_port, &host_ports));
  unordered_set<Sockaddr> uniqued;

  for (const HostPort& host_port : host_ports) {
    vector<Sockaddr> this_addresses;
    RETURN_NOT_OK(host_port.ResolveAddresses(&this_addresses));

    // Only add the unique ones -- the user may have specified
    // some IP addresses in multiple ways
    for (const Sockaddr& addr : this_addresses) {
      if (InsertIfNotPresent(&uniqued, addr)) {
        addresses->push_back(addr);
      } else {
        LOG(INFO) << "Address " << addr.ToString() << " for " << host_port.ToString()
                  << " duplicates an earlier resolved entry.";
      }
    }
  }
  return Status::OK();
}

Status GetHostname(string* hostname) {
  TRACE_EVENT0("net", "GetHostname");
  char name[HOST_NAME_MAX];
  int ret = gethostname(name, HOST_NAME_MAX);
  if (ret != 0) {
    return Status::NetworkError("Unable to determine local hostname",
                                ErrnoToString(errno),
                                errno);
  }
  *hostname = name;
  return Status::OK();
}

Status GetFQDN(string* hostname) {
  TRACE_EVENT0("net", "GetFQDN");
  // Start with the non-qualified hostname
  RETURN_NOT_OK(GetHostname(hostname));

  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_flags = AI_CANONNAME;

  struct addrinfo* result;
  LOG_SLOW_EXECUTION(WARNING, 200,
                     Substitute("looking up canonical hostname for localhost $0", hostname)) {
    TRACE_EVENT0("net", "getaddrinfo");
    int rc = getaddrinfo(hostname->c_str(), nullptr, &hints, &result);
    if (rc != 0) {
      return Status::NetworkError("Unable to lookup FQDN", ErrnoToString(errno), errno);
    }
  }

  *hostname = result->ai_canonname;
  freeaddrinfo(result);
  return Status::OK();
}

Status SockaddrFromHostPort(const HostPort& host_port, Sockaddr* addr) {
  vector<Sockaddr> addrs;
  RETURN_NOT_OK(host_port.ResolveAddresses(&addrs));
  if (addrs.empty()) {
    return Status::NetworkError("Unable to resolve address", host_port.ToString());
  }
  *addr = addrs[0];
  if (addrs.size() > 1) {
    VLOG(1) << "Hostname " << host_port.host() << " resolved to more than one address. "
            << "Using address: " << addr->ToString();
  }
  return Status::OK();
}

Status HostPortFromSockaddrReplaceWildcard(const Sockaddr& addr, HostPort* hp) {
  string host;
  if (addr.IsWildcard()) {
    RETURN_NOT_OK(GetFQDN(&host));
  } else {
    host = addr.host();
  }
  hp->set_host(host);
  hp->set_port(addr.port());
  return Status::OK();
}

void TryRunLsof(const Sockaddr& addr, vector<string>* log) {
#if defined(__APPLE__)
  string cmd = strings::Substitute(
      "lsof -n -i 'TCP:$0' -sTCP:LISTEN ; "
      "for pid in $$(lsof -F p -n -i 'TCP:$0' -sTCP:LISTEN | cut -f 2 -dp) ; do"
      "  pstree $$pid || ps h -p $$pid;"
      "done",
      addr.port());
#else
  // Little inline bash script prints the full ancestry of any pid listening
  // on the same port as 'addr'. We could use 'pstree -s', but that option
  // doesn't exist on el6.
  string cmd = strings::Substitute(
      "export PATH=$$PATH:/usr/sbin ; "
      "lsof -n -i 'TCP:$0' -sTCP:LISTEN ; "
      "for pid in $$(lsof -F p -n -i 'TCP:$0' -sTCP:LISTEN | cut -f 2 -dp) ; do"
      "  while [ $$pid -gt 1 ] ; do"
      "    ps h -fp $$pid ;"
      "    stat=($$(</proc/$$pid/stat)) ;"
      "    pid=$${stat[3]} ;"
      "  done ; "
      "done",
      addr.port());
#endif // defined(__APPLE__)

  LOG_STRING(WARNING, log) << "Failed to bind to " << addr.ToString() << ". "
                           << "Trying to use lsof to find any processes listening "
                           << "on the same port:";
  LOG_STRING(INFO, log) << "$ " << cmd;
  vector<string> argv = { "bash", "-c", cmd };
  string results;
  Status s = Subprocess::Call(argv, &results);
  if (PREDICT_FALSE(!s.ok())) {
    LOG_STRING(WARNING, log) << s.ToString();
  }
  LOG_STRING(WARNING, log) << results;
}

} // namespace kudu
