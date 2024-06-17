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
#include "yb/server/rpcz-path-handler.h"

#include <functional>
#include <memory>
#include <string>

#include "yb/rpc/messenger.h"
#include "yb/rpc/rpc_introspection.pb.h"
#include "yb/server/webserver.h"
#include "yb/util/jsonwriter.h"
#include "yb/util/status_log.h"

namespace yb {

using yb::rpc::DumpRunningRpcsRequestPB;
using yb::rpc::DumpRunningRpcsResponsePB;
using yb::rpc::Messenger;
using std::stringstream;

using namespace std::placeholders;

namespace {

template <class Map>
bool GetBool(const Map& map, const typename Map::key_type& key, bool default_value) {
  auto it = map.find(key);
  if (it == map.end()) {
    return default_value;
  }

  return ParseLeadingBoolValue(it->second, default_value);
}

void MakeAshUuidsHumanReadable(rpc::RpcCallInProgressPB* pb) {
  if (!pb->has_wait_state() || !pb->wait_state().has_metadata()) {
    return;
  }
  AshMetadataPB* metadata_pb = pb->mutable_wait_state()->mutable_metadata();
  // Convert root_request_id and yql_endpoint_tserver_uuid from binary to
  // human-readable formats
  if (metadata_pb->has_root_request_id()) {
    Result<Uuid> result = Uuid::FromSlice(metadata_pb->root_request_id());
    if (result.ok()) {
      metadata_pb->set_root_request_id(result.ToString());
    }
  }
  if (metadata_pb->has_yql_endpoint_tserver_uuid()) {
    Result<Uuid> result = Uuid::FromSlice(metadata_pb->yql_endpoint_tserver_uuid());
    if (result.ok()) {
      metadata_pb->set_yql_endpoint_tserver_uuid(result.ToString());
    }
  }
}

void MakeAshUuidsHumanReadable(rpc::RpcConnectionPB* pb) {
  for (int i = 0; i < pb->calls_in_flight_size(); i++) {
    MakeAshUuidsHumanReadable(pb->mutable_calls_in_flight(i));
  }
}

void MakeAshUuidsHumanReadable(DumpRunningRpcsResponsePB* dump_resp) {
  for (int i = 0; i < dump_resp->inbound_connections_size(); i++) {
    MakeAshUuidsHumanReadable(dump_resp->mutable_inbound_connections(i));
  }
  for (int i = 0; i < dump_resp->outbound_connections_size(); i++) {
    MakeAshUuidsHumanReadable(dump_resp->mutable_outbound_connections(i));
  }
  if (dump_resp->has_local_calls()) {
    MakeAshUuidsHumanReadable(dump_resp->mutable_local_calls());
  }
}

void RpczPathHandler(
    Messenger* messenger, bool show_local_calls, const Webserver::WebRequest& req,
    Webserver::WebResponse* resp) {
  std::stringstream *output = &resp->output;
  DumpRunningRpcsRequestPB dump_req;
  DumpRunningRpcsResponsePB dump_resp;

  const bool get_wait_state = GetBool(req.parsed_args, "get_wait_state", false);
  dump_req.set_get_wait_state(get_wait_state);
  dump_req.set_export_wait_state_code_as_string(true);
  dump_req.set_include_traces(GetBool(req.parsed_args, "include_traces", false));
  dump_req.set_dump_timed_out(GetBool(req.parsed_args, "timed_out", false));
  dump_req.set_get_local_calls(show_local_calls);

  WARN_NOT_OK(messenger->DumpRunningRpcs(dump_req, &dump_resp), "DumpRunningRpcs failed");
  if (get_wait_state) {
    MakeAshUuidsHumanReadable(&dump_resp);
  }

  JsonWriter writer(output, JsonWriter::PRETTY);
  writer.Protobuf(dump_resp);
}

} // anonymous namespace

void AddRpczPathHandlers(Messenger* messenger, bool show_local_calls, Webserver* webserver) {
  webserver->RegisterPathHandler(
      "/rpcz", "RPCs", std::bind(RpczPathHandler, messenger, show_local_calls, _1, _2), false,
      false);
}

} // namespace yb
