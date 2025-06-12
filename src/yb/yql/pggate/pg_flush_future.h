// Copyright (c) YugabyteDB, Inc.
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

#include "yb/util/status.h"

#include "yb/yql/pggate/pg_perform_future.h"

namespace yb::pggate {

class PgSession;
class PgDocMetrics;

class FlushFuture {
 public:
  FlushFuture(PerformFuture&& future, PgSession& session, PgDocMetrics& metrics);

  Status Get();
  bool Ready() const;

 private:
  PerformFuture future_;
  PgSession* session_;
  PgDocMetrics* metrics_;
};

} // namespace yb::pggate
