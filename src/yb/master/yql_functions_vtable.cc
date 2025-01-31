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
//

#include "yb/master/yql_functions_vtable.h"

#include <boost/asio/ip/address.hpp>

#include "yb/common/ql_type.h"
#include "yb/common/schema.h"
#include "yb/util/status_log.h"

namespace yb {
namespace master {

YQLFunctionsVTable::YQLFunctionsVTable(const TableName& table_name,
                                       const NamespaceName& namespace_name,
                                       Master * const master)
    : YQLEmptyVTable(table_name, namespace_name, master, CreateSchema()) {
}

Schema YQLFunctionsVTable::CreateSchema() const {
  SchemaBuilder builder;
  CHECK_OK(builder.AddHashKeyColumn("keyspace_name", DataType::STRING));
  CHECK_OK(builder.AddKeyColumn("function_name", DataType::STRING));
  CHECK_OK(builder.AddKeyColumn(
      "argument_types", QLType::CreateTypeFrozen(QLType::CreateTypeList(DataType::STRING))));
  CHECK_OK(builder.AddColumn(
      "argument_names", QLType::CreateTypeFrozen(QLType::CreateTypeList(DataType::STRING))));
  CHECK_OK(builder.AddColumn("body", QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn("called_on_null_input", QLType::Create(DataType::BOOL)));
  CHECK_OK(builder.AddColumn("language", QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn("return_type", QLType::Create(DataType::STRING)));
  return builder.Build();
}

}  // namespace master
}  // namespace yb
