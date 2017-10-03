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
#ifndef KUDU_CLIENT_ROW_RESULT_H
#define KUDU_CLIENT_ROW_RESULT_H

#include "kudu/client/scan_batch.h"

namespace kudu {
namespace client {

// DEPRECATED: Kudu 0.7.0 renamed KuduRowResult to KuduScanBatch::RowPtr.
// The newer name is clearer that the row result's lifetime is tied to the
// lifetime of a batch.
typedef KuduScanBatch::RowPtr KuduRowResult;

} // namespace client
} // namespace kudu

#endif
