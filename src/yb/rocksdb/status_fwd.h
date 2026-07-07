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
// Forward declaration of rocksdb::Status (a typedef for yb::Status). Use this header instead of
// yb/rocksdb/status.h when you only need to refer to Status in declarations or by reference --
// e.g. parameters of type `const rocksdb::Status&`, function return types, member pointers --
// and do not need to call any Status methods or copy/move a Status by value. The full definition
// is in yb/rocksdb/status.h (which pulls in yb/util/status.h).

#pragma once

#include "yb/util/status_fwd.h"

namespace rocksdb {

typedef yb::Status Status;

}  // namespace rocksdb
