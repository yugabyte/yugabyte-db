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
#include <string>

#include "yb/yql/cql/cqlserver/statements-path-handler.h"

#include "yb/server/webserver.h"
#include "yb/util/jsonwriter.h"
#include "yb/yql/cql/cqlserver/cql_service.h"

namespace yb {

using std::shared_ptr;

using namespace std::placeholders;

namespace {

void StatementsPathHandler(shared_ptr<CQLServiceImpl> service,
                     const Webserver::WebRequest& req, Webserver::WebResponse* resp) {
  std::stringstream *output = &resp->output;
  JsonWriter jw(output, JsonWriter::PRETTY);
  service->DumpStatementMetricsAsJson(&jw);
}

void ResetStatementsPathHandler(shared_ptr<CQLServiceImpl> service,
                                const Webserver::WebRequest& req, Webserver::WebResponse* resp) {
  std::stringstream *output = &resp->output;
  JsonWriter jw(output, JsonWriter::PRETTY);

  jw.StartObject();
  jw.String("statements");
  service->ResetStatementsCounters();
  jw.String("CQL Statements reset.");
  jw.EndObject();
}

} // anonymous namespace

void AddStatementsPathHandlers(Webserver* webserver, shared_ptr<CQLServiceImpl> service) {
  webserver->RegisterPathHandler(
      "/statements", "CQL Statements", std::bind(StatementsPathHandler, service, _1, _2),
      false, false);
  webserver->RegisterPathHandler(
      "/statements-reset", "Reset CQL Statements",
      std::bind(ResetStatementsPathHandler, service, _1, _2), false, false);
}

} // namespace yb
