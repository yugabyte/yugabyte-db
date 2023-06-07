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
#pragma once

#include "yb/util/jsonwriter.h"
#include "yb/yql/cql/cqlserver/cql_statement.h"
#include "yb/yql/cql/ql/util/cql_message.h"

namespace yb {

namespace cqlserver {

class StatementMetrics {
 public:
  StatementMetrics(const ql::CQLMessage::QueryId& query_id,
                   const std::shared_ptr<const StmtCounters> stmt_counters);

  ~StatementMetrics() {}

  void WriteAsJson(JsonWriter* jw) const;

  ql::CQLMessage::QueryId query_id() const { return query_id_; }

 private:
  const ql::CQLMessage::QueryId query_id_;
  const std::shared_ptr<const StmtCounters> counters_;

};
} // namespace cqlserver
} // namespace yb
