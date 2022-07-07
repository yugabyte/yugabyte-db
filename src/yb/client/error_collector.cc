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

#include "yb/client/error_collector.h"

#include "yb/client/error.h"
#include "yb/client/yb_op.h"

namespace yb {
namespace client {
namespace internal {

ErrorCollector::ErrorCollector() {
}

ErrorCollector::~ErrorCollector() {}

void ErrorCollector::AddError(std::unique_ptr<YBError> error) {
  LOG_IF(DFATAL, error->status().ok())
      << "Unexpected to add OK status as error for: " << error->failed_op().ToString();
  errors_.push_back(std::move(error));
}

size_t ErrorCollector::CountErrors() const {
  return errors_.size();
}

CollectedErrors ErrorCollector::GetAndClearErrors() {
  CollectedErrors result;
  errors_.swap(result);
  return result;
}

void ErrorCollector::AddError(std::shared_ptr<YBOperation> operation, Status status) {
  AddError(std::make_unique<YBError>(std::move(operation), std::move(status)));
}

} // namespace internal
} // namespace client
} // namespace yb
