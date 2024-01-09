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

void RpczPathHandler(Messenger* messenger,
                     const Webserver::WebRequest& req, Webserver::WebResponse* resp) {
  std::stringstream *output = &resp->output;
  DumpRunningRpcsRequestPB dump_req;
  DumpRunningRpcsResponsePB dump_resp;

  dump_req.set_include_traces(GetBool(req.parsed_args, "include_traces", false));
  dump_req.set_dump_timed_out(GetBool(req.parsed_args, "timed_out", false));
  dump_req.set_get_local_calls(true);

  WARN_NOT_OK(messenger->DumpRunningRpcs(dump_req, &dump_resp), "DumpRunningRpcs failed");

  JsonWriter writer(output, JsonWriter::PRETTY);
  writer.Protobuf(dump_resp);
}

} // anonymous namespace

void AddRpczPathHandlers(Messenger* messenger, Webserver* webserver) {
  webserver->RegisterPathHandler(
      "/rpcz", "RPCs", std::bind(RpczPathHandler, messenger, _1, _2), false, false);
}

} // namespace yb
