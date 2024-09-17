//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByteDB, Inc.
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

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "yb/common/schema.h"

#include "yb/dockv/pg_row.h"
#include "yb/dockv/reader_projection.h"

#include "yb/qlexpr/ql_expr.h"

#include "yb/util/result.h"
#include "yb/util/status_fwd.h"

#include "yb/yql/pggate/pg_memctx.h"
#include "yb/yql/pggate/pg_session.h"
#include "yb/yql/pggate/pg_value.h"
#include "yb/yql/pggate/ybc_pg_typedefs.h"

namespace yb {
namespace pggate {

template<typename T>
using ParamAndIsNullPair = std::pair<T, bool>;

// PgFunctionParams translates Postgres datums into a format that can be consumed by pg_gate. Each
// parameter contains the datum value and the Postgres type information needed for type conversion.
class PgFunctionParams {
 public:

  Status AddParam(
      const std::string& name, const YBCPgTypeEntity* type_entity, uint64_t datum, bool is_null);

  size_t Size() const { return params_by_name_.size(); }

  // Retrieve the value of a parameter given its name, returning the value as
  // an instance of the specified template type T. Also provide a boolean flag
  // is_null to indicate if the value is null.
  template <class T>
  Result<ParamAndIsNullPair<T>> GetParamValue(const std::string& col_name) const;

 private:
  // Return a pair containing a shared pointer to the QLValuePB (internal
  // representation of the parameter value) and the YBCPgTypeEntity (the type
  // of the parameter) given the parameter's name.
  Result<std::pair<std::shared_ptr<const QLValuePB>, const YBCPgTypeEntity*>> GetValueAndType(
      const std::string& name) const;

  std::unordered_map<
      std::string, std::pair<std::shared_ptr<const QLValuePB>, const YBCPgTypeEntity*>>
      params_by_name_;
};

using PgFunctionDataProcessor = std::function<Result<std::list<dockv::PgTableRow>>(
    const PgFunctionParams&, const Schema&, const dockv::ReaderProjection&,
    const scoped_refptr<PgSession>&)>;

class PgFunction : public PgMemctx::Registrable {
 public:
  explicit PgFunction(PgFunctionDataProcessor processor, scoped_refptr<PgSession> pg_session)
      : schema_builder_(),
        pg_session_(pg_session),
        processor_(std::move(processor)) {}

  virtual ~PgFunction() = default;

  Status AddParam(
      const std::string& name, const YBCPgTypeEntity* type_entity, uint64_t datum, bool is_null);

  Status AddTarget(
      const std::string& name, const YBCPgTypeEntity* type_entity, const YBCPgTypeAttrs type_attrs);

  Status FinalizeTargets();

  Status GetNext(uint64_t* values, bool* is_nulls, bool* has_data);

 private:
  Status WritePgTuple(const dockv::PgTableRow& table_row, uint64_t* values, bool* is_nulls);

  PgFunctionParams params_;
  SchemaBuilder schema_builder_;
  Schema schema_;

  scoped_refptr<PgSession> pg_session_;
  PgFunctionDataProcessor processor_;

  bool executed_ = false;
  dockv::ReaderProjection projection_;
  std::list<dockv::PgTableRow> data_;
  std::list<dockv::PgTableRow>::const_iterator current_;
};

Result<std::list<dockv::PgTableRow>> PgLockStatusRequestor(
    const PgFunctionParams& params, const Schema& schema,
    const dockv::ReaderProjection& reader_projection, const scoped_refptr<PgSession>& pg_session);

}  // namespace pggate
}  // namespace yb
