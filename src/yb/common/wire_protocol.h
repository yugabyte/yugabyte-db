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
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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
// Helpers for dealing with the protobufs defined in wire_protocol.proto.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "yb/common/common_fwd.h"

#include <google/protobuf/repeated_field.h>

#include "yb/gutil/endian.h"

#include "yb/util/status_fwd.h"
#include "yb/util/cast.h"
#include "yb/util/enums.h"
#include "yb/util/math_util.h"
#include "yb/util/net/net_fwd.h"
#include "yb/util/status_ec.h"
#include "yb/util/type_traits.h"
#include "yb/util/result.h"

using namespace std::literals;

namespace yb {

class faststring;
class HostPort;
class Slice;

// Convert the given C++ Status object into the equivalent Protobuf.
void StatusToPB(const Status& status, AppStatusPB* pb);
void StatusToPB(const Status& status, LWAppStatusPB* pb);

// Convert the given protobuf into the equivalent C++ Status object.
Status StatusFromPB(const AppStatusPB& pb);

Status StatusFromPB(const LWAppStatusPB& pb);

// Convert the specified HostPort to protobuf.
void HostPortToPB(const HostPort& host_port, HostPortPB* host_port_pb);
void HostPortToPB(const HostPort& host_port, LWHostPortPB* host_port_pb);
HostPortPB HostPortToPB(const HostPort& host_port);

// Returns the HostPort created from the specified protobuf.
HostPort HostPortFromPB(const HostPortPB& host_port_pb);

// Whether two addresses are the same address. Host and port together are the identity: the
// same host on another port is a different address.
bool HasSameHostPort(const HostPortPB& lhs, const HostPortPB& rhs);

bool HasHostPortPB(
    const google::protobuf::RepeatedPtrField<HostPortPB>& list, const HostPortPB& hp);

// Returns an Endpoint from HostPortPB.
Status EndpointFromHostPortPB(const HostPortPB& host_portpb, Endpoint* endpoint);

// Adds addresses in 'addrs' to 'pbs'. If an address is a wildcard (e.g., "0.0.0.0"),
// then the local machine's FQDN or its network interface address is used in its place.
Status AddHostPortPBs(const std::vector<Endpoint>& addrs,
                      google::protobuf::RepeatedPtrField<HostPortPB>* pbs);

// Simply convert the list of host ports into a repeated list of corresponding PB's.
void HostPortsToPBs(const std::vector<HostPort>& addrs,
                    google::protobuf::RepeatedPtrField<HostPortPB>* pbs);

// Convert list of HostPortPBs into host ports.
void HostPortsFromPBs(const google::protobuf::RepeatedPtrField<HostPortPB>& pbs,
                      std::vector<HostPort>* addrs);

YB_DEFINE_ENUM(UsePrivateIpMode, (cloud)(region)(zone)(never));

// Returns mode for selecting between private and public IP.
Result<UsePrivateIpMode> GetPrivateIpMode();

// Returns the scope outside which node to node connections are encrypted.
Result<UsePrivateIpMode> GetNodeToNodeEncryptionScope();

// Pick node's public host and port
// registration - node registration information
const HostPortPB& PublicHostPort(const ServerRegistrationPB& registration);

// Whether a connection went out on a node's broadcast address rather than its private one.
// Distinct from a public address merely being permitted, which is the scope's answer: a node
// that reported no broadcast address is reached privately whatever the scope allows.
YB_STRONGLY_TYPED_BOOL(UsedBroadcastAddress);

// An address to reach a node at, together with which list it came from. The two travel
// together because a caller that encrypts has to know which one it got: what a connection
// carries follows the address chosen for it, and recomputing that choice separately would
// let the two answers drift apart.
struct SelectedHostPort {
  const HostPortPB& host_port;
  UsedBroadcastAddress used_broadcast;
};

// Whether reaching a node at this address leaves its private address, which is what decides
// whether the connection is held to node_to_node_encryption_required_on_broadcast. The
// address decides it, not the list it was read from: a node that reports one address in both
// lists, as the Kubernetes manifests in cloud/ do, is reached privately at it however it was
// chosen. An address the node reports in neither list cannot be shown to be private, so it is
// treated as broadcast.
//
// This also answers for an address chosen some other way than by selection. A tserver reaches
// its master over an address the master configuration named, raced against every other, so
// that connection's provenance is recovered here rather than selected.
UsedBroadcastAddress UsesBroadcastAddress(
    const google::protobuf::RepeatedPtrField<HostPortPB>& private_host_ports,
    const HostPortPB& host_port);
UsedBroadcastAddress UsesBroadcastAddress(
    const ServerRegistrationPB& registration, const HostPortPB& host_port);

// Pick host and port that should be used to connect node
// broadcast_addresses - node public host ports
// private_host_ports - node private host ports
// connect_to - node placement information
// connect_from - placement information of connect originator
SelectedHostPort SelectHostPort(
    const google::protobuf::RepeatedPtrField<HostPortPB>& broadcast_addresses,
    const google::protobuf::RepeatedPtrField<HostPortPB>& private_host_ports,
    const CloudInfoPB& connect_to,
    const CloudInfoPB& connect_from);

// Pick host and port that should be used to connect node
// registration - node registration information
// connect_from - placement information of connect originator
SelectedHostPort SelectHostPort(
    const ServerRegistrationPB& registration, const CloudInfoPB& connect_from);

// The address SelectHostPort chooses, for callers that have no use for which list it came
// from. Anything deciding a connection's transport must take both from one SelectHostPort
// call instead, so that the address and the transport cannot describe different connections.
const HostPortPB& DesiredHostPort(
    const google::protobuf::RepeatedPtrField<HostPortPB>& broadcast_addresses,
    const google::protobuf::RepeatedPtrField<HostPortPB>& private_host_ports,
    const CloudInfoPB& connect_to,
    const CloudInfoPB& connect_from);

const HostPortPB& DesiredHostPort(
    const ServerRegistrationPB& registration, const CloudInfoPB& connect_from);

// Whether a connection to connect_to should be encrypted: node_to_node_encryption_scope
// exempts destinations within a placement boundary, and
// node_to_node_encryption_required_on_broadcast withholds that exemption from any connection
// that did not stay on the destination's private address.
//
// used_broadcast comes from the same SelectHostPort call that chose the address, so the
// transport always describes the connection actually being made.
//
// This answers the policy alone. A messenger built without encryption has no encrypted
// transport to name, so ProxyContext::ProtocolFor pairs this with the transports the
// messenger actually holds, the way SelectHostPort pairs the scope with the addresses a node
// actually reported.
bool UseEncryption(
    UsedBroadcastAddress used_broadcast, const CloudInfoPB& connect_to,
    const CloudInfoPB& connect_from);

HAS_MEMBER_FUNCTION(error);
HAS_MEMBER_FUNCTION(status);

template<class Response>
Status ResponseStatus(
    const Response& response,
    typename std::enable_if<HasMemberFunction_error<Response>::value, void*>::type = nullptr) {
  // Response has has_error method, use status from it.
  if (response.has_error()) {
    return StatusFromPB(response.error().status());
  }
  return Status::OK();
}

template<class Response>
Status ResponseStatus(
    const Response& response,
    typename std::enable_if<HasMemberFunction_status<Response>::value &&
                            !HasMemberFunction_error<Response>::value, void*>::type = nullptr) {
  if (response.has_status()) {
    return StatusFromPB(response.status());
  }
  return Status::OK();
}

struct SplitChildTabletIdsTag : yb::StringVectorBackedErrorTag {
  // It is part of the wire protocol and should not be changed once released.
  static constexpr CategoryDescriptor kCategory{14, "split child tablet IDs"sv};

  static std::string ToMessage(const Value& value);
};

using SplitChildTabletIdsData = yb::StatusErrorCodeImpl<SplitChildTabletIdsTag>;

} // namespace yb
