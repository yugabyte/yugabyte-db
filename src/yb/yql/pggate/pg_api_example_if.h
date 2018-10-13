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

// This is just an example of using a single "interface DSL" file to declare a C++ class and
// C wrapper functions for working with instances of that class from PostgreSQL C code.

#ifdef YBC_CXX_DECLARATION_MODE
#include "yb/yql/pggate/pg_session.h"
#endif  // YBC_CXX_DECLARATION_MODE

YBC_ENTER_PGGATE_NAMESPACE

#define YBC_CURRENT_CLASS PgApiExample

YBC_CLASS_START
YBC_CONSTRUCTOR(
    ((const char*, database_name))
    ((const char*, table_name))
    ((const char**, column_names))
)
YBC_VIRTUAL_DESTRUCTOR

YBC_VIRTUAL YBC_METHOD_NO_ARGS(bool, HasNext)

YBC_RESULT_METHOD(int32_t, GetInt32Column,
    ((int, column_index))
)

YBC_STATUS_METHOD(GetStringColumn,
    ((int, column_index))
    ((const char**, result))
)

#ifdef YBC_CXX_DECLARATION_MODE
 private:
  PgSession::ScopedRefPtr pg_session_;
  std::string database_name_;
  std::string table_name_;
  std::vector<std::string> columns_;
#endif  // YBC_CXX_DECLARATION_MODE

YBC_CLASS_END

#undef YBC_CURRENT_CLASS

YBC_LEAVE_PGGATE_NAMESPACE
