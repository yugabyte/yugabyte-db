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

#include "kudu/server/rpcz-path-handler.h"

#include <boost/bind.hpp>
#include <fstream>
#include <memory>
#include <string>

#include "kudu/gutil/map-util.h"
#include "kudu/gutil/strings/numbers.h"
#include "kudu/rpc/messenger.h"
#include "kudu/rpc/rpc_introspection.pb.h"
#include "kudu/server/webserver.h"

using kudu::rpc::DumpRunningRpcsRequestPB;
using kudu::rpc::DumpRunningRpcsResponsePB;
using kudu::rpc::Messenger;
using std::shared_ptr;
using std::stringstream;

namespace kudu {

namespace {

void RpczPathHandler(const shared_ptr<Messenger>& messenger,
                     const Webserver::WebRequest& req, stringstream* output) {
  DumpRunningRpcsRequestPB dump_req;
  DumpRunningRpcsResponsePB dump_resp;

  string arg = FindWithDefault(req.parsed_args, "include_traces", "false");
  dump_req.set_include_traces(ParseLeadingBoolValue(arg.c_str(), false));

  messenger->DumpRunningRpcs(dump_req, &dump_resp);

  JsonWriter writer(output, JsonWriter::PRETTY);
  writer.Protobuf(dump_resp);
}

} // anonymous namespace

void AddRpczPathHandlers(const shared_ptr<Messenger>& messenger, Webserver* webserver) {
  webserver->RegisterPathHandler("/rpcz", "RPCs",
                                 boost::bind(RpczPathHandler, messenger, _1, _2),
                                 false, true);
}

} // namespace kudu
