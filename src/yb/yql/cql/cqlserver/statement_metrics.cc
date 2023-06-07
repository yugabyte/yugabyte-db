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
#include "yb/gutil/strings/escaping.h"

#include "yb/yql/cql/cqlserver/statement_metrics.h"

namespace yb {
namespace cqlserver {

StatementMetrics::StatementMetrics(const ql::CQLMessage::QueryId& query_id,
                                   const std::shared_ptr<const StmtCounters> stmt_counters)
    : query_id_(query_id), counters_(stmt_counters) {
}

void StatementMetrics::WriteAsJson(JsonWriter* jw) const {
  counters_->WriteAsJson(jw, b2a_hex(query_id_));
}
} // namespace cqlserver

} // namespace yb
