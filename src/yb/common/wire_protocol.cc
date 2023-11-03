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
#include "yb/common/wire_protocol.h"

#include <string>
#include <vector>

#include "yb/common/common.pb.h"
#include "yb/common/ql_type.h"
#include "yb/common/wire_protocol.messages.h"

#include "yb/gutil/port.h"
#include "yb/gutil/stl_util.h"
#include "yb/gutil/strings/fastmem.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/util/enums.h"
#include "yb/util/errno.h"
#include "yb/util/faststring.h"
#include "yb/util/logging.h"
#include "yb/util/net/net_util.h"
#include "yb/util/net/sockaddr.h"
#include "yb/util/result.h"
#include "yb/util/slice.h"
#include "yb/util/status_format.h"
#include "yb/yql/cql/ql/util/errcodes.h"
#include "yb/util/flags.h"

using google::protobuf::RepeatedPtrField;
using std::vector;

DEFINE_UNKNOWN_string(use_private_ip, "never",
              "When to use private IP for connection. "
              "cloud - would use private IP if destination node is located in the same cloud. "
              "region - would use private IP if destination node is located in the same cloud and "
                  "region. "
              "zone - would use private IP if destination node is located in the same cloud, "
                  "region and zone."
              "never - would never use private IP if broadcast address is specified.");
namespace yb {

namespace {

YB_STRONGLY_TYPED_BOOL(PublicAddressAllowed);

template <class Index, class Value>
void SetAt(
    Index index, const Value& value, const Value& default_value, std::vector<Value>* vector) {
  size_t int_index = static_cast<size_t>(index);
  size_t new_size = vector->size();
  while (new_size <= int_index) {
    new_size = std::max<size_t>(1, new_size * 2);
  }
  vector->resize(new_size, default_value);
  (*vector)[int_index] = value;
}

std::vector<AppStatusPB::ErrorCode> CreateStatusToErrorCode() {
  std::vector<AppStatusPB::ErrorCode> result;
  const auto default_value = AppStatusPB::UNKNOWN_ERROR;
  #define YB_STATUS_CODE(name, pb_name, value, message) \
    SetAt(Status::BOOST_PP_CAT(k, name), AppStatusPB::pb_name, default_value, &result); \
    static_assert( \
        static_cast<int32_t>(to_underlying(AppStatusPB::pb_name)) == \
            to_underlying(Status::BOOST_PP_CAT(k, name)), \
        "The numeric value of AppStatusPB::" BOOST_PP_STRINGIZE(pb_name) " defined in" \
            " wire_protocol.proto does not match the value of Status::k" BOOST_PP_STRINGIZE(name) \
            " defined in status.h.");
  #include "yb/util/status_codes.h"
  #undef YB_STATUS_CODE
  return result;
}

const std::vector<AppStatusPB::ErrorCode> kStatusToErrorCode = CreateStatusToErrorCode();

std::vector<Status::Code> CreateErrorCodeToStatus() {
  size_t max_index = 0;
  for (const auto error_code : kStatusToErrorCode) {
    if (error_code == AppStatusPB::UNKNOWN_ERROR) {
      continue;
    }
    max_index = std::max(max_index, static_cast<size_t>(error_code));
  }

  std::vector<Status::Code> result(max_index + 1);
  for (size_t int_code = 0; int_code != kStatusToErrorCode.size(); ++int_code) {
    if (kStatusToErrorCode[int_code] == AppStatusPB::UNKNOWN_ERROR) {
      continue;
    }
    result[static_cast<size_t>(kStatusToErrorCode[int_code])] = static_cast<Status::Code>(int_code);
  }

  return result;
}

const std::vector<Status::Code> kErrorCodeToStatus = CreateErrorCodeToStatus();

const HostPortPB& GetHostPort(
    const google::protobuf::RepeatedPtrField<HostPortPB>& broadcast_addresses,
    const google::protobuf::RepeatedPtrField<HostPortPB>& private_host_ports,
    PublicAddressAllowed public_address_allowed) {
  if (!broadcast_addresses.empty() && public_address_allowed) {
    return broadcast_addresses[0];
  }
  if (!private_host_ports.empty()) {
    return private_host_ports[0];
  }
  static const HostPortPB empty_host_port;
  return empty_host_port;
}

void DupMessage(const Slice& message, AppStatusPB* pb) {
  pb->set_message(message.cdata(), message.size());
}

void DupErrors(const Slice& errors, AppStatusPB* pb) {
  pb->set_errors(errors.cdata(), errors.size());
}

void DupMessage(const Slice& message, LWAppStatusPB* pb) {
  pb->dup_message(message);
}

void DupErrors(const Slice& errors, LWAppStatusPB* pb) {
  pb->dup_errors(errors);
}

template <class PB>
void SharedStatusToPB(const Status& status, PB* pb) {
  pb->Clear();

  if (status.ok()) {
    pb->set_code(AppStatusPB::OK);
    // OK statuses don't have any message or posix code.
    return;
  }

  auto code = static_cast<size_t>(status.code()) < kStatusToErrorCode.size()
      ? kStatusToErrorCode[status.code()] : AppStatusPB::UNKNOWN_ERROR;
  pb->set_code(code);
  if (code == AppStatusPB::UNKNOWN_ERROR) {
    LOG(WARNING) << "Unknown error code translation connect_from internal error "
                 << status << ": sending UNKNOWN_ERROR";
    // For unknown status codes, include the original stringified error
    // code.
    DupMessage(status.CodeAsString() + ": " + status.message().ToBuffer(), pb);
  } else {
    // Otherwise, just encode the message itself, since the other end
    // will reconstruct the other parts of the ToString() response.
    DupMessage(status.message(), pb);
  }

  auto error_codes = status.ErrorCodesSlice();
  DupErrors(error_codes, pb);
  // We always has 0 as terminating byte for error codes, so non empty error codes would have
  // more than one bytes.
  if (error_codes.size() > 1) {
    // Set old protobuf fields for backward compatibility.
    Errno err(status);
    if (err != 0) {
      pb->set_posix_code(err.value());
    }
    const auto* ql_error_data = status.ErrorData(ql::QLError::kCategory);
    if (ql_error_data) {
      pb->set_ql_error_code(static_cast<int64_t>(ql::QLErrorTag::Decode(ql_error_data)));
    }
  }

  pb->set_source_line(status.line_number());
}

} // namespace

void StatusToPB(const Status& status, AppStatusPB* pb) {
  SharedStatusToPB(status, pb);

  pb->set_source_file(status.file_name());
}

void StatusToPB(const Status& status, LWAppStatusPB* pb) {
  SharedStatusToPB(status, pb);

  pb->dup_source_file(status.file_name());
}

struct WireProtocolTabletServerErrorTag {
  static constexpr uint8_t kCategory = 5;

  enum Value {};

  static size_t EncodedSize(Value value) {
    return sizeof(Value);
  }

  static uint8_t* Encode(Value value, uint8_t* out) {
    Store<Value, LittleEndian>(out, value);
    return out + sizeof(Value);
  }
};

// Backward compatibility.
template<class PB>
Status StatusFromOldPB(const PB& pb) {
  auto code = kErrorCodeToStatus[pb.code()];

  auto status_factory = [code, &pb](const Slice& errors) {
    return Status(
        code, Slice(pb.source_file()).cdata(), pb.source_line(), pb.message(), errors,
        pb.source_file().size());
  };

  #define ENCODE_ERROR_AND_RETURN_STATUS(Tag, value) \
    auto error_code = static_cast<Tag::Value>((value)); \
    auto size = 2 + Tag::EncodedSize(error_code); \
    uint8_t* buffer = static_cast<uint8_t*>(alloca(size)); \
    buffer[0] = Tag::kCategory; \
    Tag::Encode(error_code, buffer + 1); \
    buffer[size - 1] = 0; \
    return status_factory(Slice(buffer, size)); \
    /**/

  if (code == Status::kQLError) {
    if (!pb.has_ql_error_code()) {
      return STATUS(InternalError, "Query error code missing");
    }

    ENCODE_ERROR_AND_RETURN_STATUS(ql::QLErrorTag, pb.ql_error_code())
  } else if (pb.has_posix_code()) {
    if (code == Status::kIllegalState || code == Status::kLeaderNotReadyToServe ||
        code == Status::kLeaderHasNoLease) {

      ENCODE_ERROR_AND_RETURN_STATUS(WireProtocolTabletServerErrorTag, pb.posix_code())
    } else {
      ENCODE_ERROR_AND_RETURN_STATUS(ErrnoTag, pb.posix_code())
    }
  }

  return Status(code, Slice(pb.source_file()).cdata(), pb.source_line(), pb.message(), "",
                nullptr /* error */, pb.source_file().size());
  #undef ENCODE_ERROR_AND_RETURN_STATUS
}

namespace {

template<class PB>
Status DoStatusFromPB(const PB& pb) {
  if (pb.code() == AppStatusPB::OK) {
    return Status::OK();
  } else if (pb.code() == AppStatusPB::UNKNOWN_ERROR ||
             static_cast<size_t>(pb.code()) >= kErrorCodeToStatus.size()) {
    LOG(WARNING) << "Unknown error code in status: " << pb.ShortDebugString();
    return STATUS_FORMAT(
        RuntimeError, "($0 unknown): $1", pb.code(), pb.message());
  }

  if (pb.has_errors()) {
    return Status(kErrorCodeToStatus[pb.code()], Slice(pb.source_file()).cdata(), pb.source_line(),
                  pb.message(), pb.errors(), pb.source_file().size());
  }

  return StatusFromOldPB(pb);
}

} // namespace

Status StatusFromPB(const AppStatusPB& pb) {
  return DoStatusFromPB(pb);
}

Status StatusFromPB(const LWAppStatusPB& pb) {
  return DoStatusFromPB(pb);
}

void HostPortToPB(const HostPort& host_port, HostPortPB* host_port_pb) {
  host_port_pb->set_host(host_port.host());
  host_port_pb->set_port(host_port.port());
}

HostPort HostPortFromPB(const HostPortPB& host_port_pb) {
  HostPort host_port;
  host_port.set_host(host_port_pb.host());
  host_port.set_port(host_port_pb.port());
  return host_port;
}

bool HasHostPortPB(
    const google::protobuf::RepeatedPtrField<HostPortPB>& list, const HostPortPB& hp) {
  for (const auto& i : list) {
    if (i.host() == hp.host() && i.port() == hp.port()) {
      return true;
    }
  }
  return false;
}

Status EndpointFromHostPortPB(const HostPortPB& host_portpb, Endpoint* endpoint) {
  HostPort host_port = HostPortFromPB(host_portpb);
  return EndpointFromHostPort(host_port, endpoint);
}

void HostPortsToPBs(const std::vector<HostPort>& addrs, RepeatedPtrField<HostPortPB>* pbs) {
  for (const auto& addr : addrs) {
    HostPortToPB(addr, pbs->Add());
  }
}

void HostPortsFromPBs(const RepeatedPtrField<HostPortPB>& pbs, std::vector<HostPort>* addrs) {
  addrs->reserve(pbs.size());
  for (const auto& pb : pbs) {
    addrs->push_back(HostPortFromPB(pb));
  }
}

Status AddHostPortPBs(const std::vector<Endpoint>& addrs,
                      RepeatedPtrField<HostPortPB>* pbs) {
  for (const auto& addr : addrs) {
    HostPortPB* pb = pbs->Add();
    pb->set_port(addr.port());
    if (addr.address().is_unspecified()) {
      VLOG(4) << " Asked to add unspecified address: " << addr.address();
      auto status = GetFQDN(pb->mutable_host());
      if (!status.ok()) {
        std::vector<IpAddress> locals;
        if (!GetLocalAddresses(FLAGS_net_address_filter, &locals).ok() ||
            locals.empty()) {
          return status;
        }
        for (auto& address : locals) {
          if (pb == nullptr) {
            pb = pbs->Add();
            pb->set_port(addr.port());
          }
          pb->set_host(address.to_string());
          VLOG(4) << "Adding local address: " << pb->host();
          pb = nullptr;
        }
      } else {
        VLOG(4) << "Adding FQDN " << pb->host();
      }
    } else {
      pb->set_host(addr.address().to_string());
      VLOG(4) << "Adding specific address: " << pb->host();
    }
  }
  return Status::OK();
}

Result<UsePrivateIpMode> GetPrivateIpMode() {
  for (auto i : UsePrivateIpModeList()) {
    if (FLAGS_use_private_ip == ToCString(i)) {
      return i;
    }
  }
  return STATUS_FORMAT(
      IllegalState,
      "Invalid value of FLAGS_use_private_ip: $0, using private ip everywhere",
      FLAGS_use_private_ip);
}

UsePrivateIpMode GetMode() {
  auto result = GetPrivateIpMode();
  if (result.ok()) {
    return *result;
  }
  YB_LOG_EVERY_N_SECS(WARNING, 300) << result.status();
  return UsePrivateIpMode::never;
}

PublicAddressAllowed UsePublicIp(const CloudInfoPB& connect_to, const CloudInfoPB& connect_from) {
  auto mode = GetMode();

  if (mode == UsePrivateIpMode::never) {
    return PublicAddressAllowed::kTrue;
  }
  if (connect_to.placement_cloud() != connect_from.placement_cloud()) {
    return PublicAddressAllowed::kTrue;
  }
  if (mode == UsePrivateIpMode::cloud) {
    return PublicAddressAllowed::kFalse;
  }
  if (connect_to.placement_region() != connect_from.placement_region()) {
    return PublicAddressAllowed::kTrue;
  }
  if (mode == UsePrivateIpMode::region) {
    return PublicAddressAllowed::kFalse;
  }
  if (connect_to.placement_zone() != connect_from.placement_zone()) {
    return PublicAddressAllowed::kTrue;
  }
  return mode == UsePrivateIpMode::zone
      ? PublicAddressAllowed::kFalse
      : PublicAddressAllowed::kTrue;
}

const HostPortPB& PublicHostPort(const ServerRegistrationPB& registration) {
  return GetHostPort(registration.broadcast_addresses(),
                     registration.private_rpc_addresses(),
                     PublicAddressAllowed::kTrue);
}

const HostPortPB& DesiredHostPort(
    const google::protobuf::RepeatedPtrField<HostPortPB>& broadcast_addresses,
    const google::protobuf::RepeatedPtrField<HostPortPB>& private_host_ports,
    const CloudInfoPB& connect_to,
    const CloudInfoPB& connect_from) {
  return GetHostPort(broadcast_addresses,
                     private_host_ports,
                     UsePublicIp(connect_to, connect_from));
}

const HostPortPB& DesiredHostPort(const ServerRegistrationPB& registration,
                                  const CloudInfoPB& connect_from) {
  return DesiredHostPort(
      registration.broadcast_addresses(), registration.private_rpc_addresses(),
      registration.cloud_info(), connect_from);
}

static const std::string kSplitChildTabletIdsCategoryName = "split child tablet IDs";

StatusCategoryRegisterer split_child_tablet_ids_category_registerer(
    StatusCategoryDescription::Make<SplitChildTabletIdsTag>(&kSplitChildTabletIdsCategoryName));

std::string SplitChildTabletIdsTag::ToMessage(const Value& value) {
  return Format("Split child tablet IDs: $0", value);
}

} // namespace yb
