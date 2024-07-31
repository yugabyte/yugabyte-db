//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
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
//--------------------------------------------------------------------------------------------------

#pragma once

#include "yb/yql/pggate/pg_dml_read.h"

namespace yb::pggate {

class PgSelect : public PgDmlRead {
 public:
  PgSelect(
      PgSession::ScopedRefPtr pg_session, const PgObjectId& table_id, bool is_region_local,
      const PrepareParameters& prepare_params, const PgObjectId& index_id = {});

  // Prepare query before execution.
  Status Prepare() override;
};

}  // namespace yb::pggate
