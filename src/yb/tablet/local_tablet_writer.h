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

#include <future>
#include <vector>

#include <google/protobuf/repeated_field.h>

#include "yb/common/common_fwd.h"

#include "yb/gutil/macros.h"

#include "yb/tablet/write_query_context.h"

#include "yb/tserver/tserver_fwd.h"

namespace yb {
namespace tablet {

// Helper class to write directly into a local tablet, without going
// through TabletPeer, consensus, etc.
//
// This is useful for unit-testing the Tablet code paths with no consensus
// implementation or thread pools.
class LocalTabletWriter : public WriteQueryContext {
 public:
  typedef google::protobuf::RepeatedPtrField<QLWriteRequestPB> Batch;

  explicit LocalTabletWriter(TabletPtr tablet);
  ~LocalTabletWriter();

  Status Write(QLWriteRequestPB* req);
  Status WriteBatch(Batch* batch);

 private:
  void Submit(std::unique_ptr<Operation> operation, int64_t term) override;
  Result<HybridTime> ReportReadRestart() override;

  TabletPtr tablet_;

  std::unique_ptr<tserver::WriteRequestPB> req_;
  std::unique_ptr<tserver::WriteResponsePB> resp_;
  std::promise<Status> write_promise_;

  DISALLOW_COPY_AND_ASSIGN(LocalTabletWriter);
};


}  // namespace tablet
}  // namespace yb
