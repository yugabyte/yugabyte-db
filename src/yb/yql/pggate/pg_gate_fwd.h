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

#ifndef YB_YQL_PGGATE_PG_GATE_FWD_H
#define YB_YQL_PGGATE_PG_GATE_FWD_H

#include <memory>

#include "yb/gutil/ref_counted.h"

namespace google {
namespace protobuf {

class Message;

}
}

namespace yb {
namespace pggate {

class PgClient;

class PgTable;
class PgTableDesc;
using PgTableDescPtr = scoped_refptr<PgTableDesc>;

class PgsqlOp;
using PgsqlOpPtr = std::shared_ptr<PgsqlOp>;

}  // namespace pggate
}  // namespace yb

#endif  // YB_YQL_PGGATE_PG_GATE_FWD_H
