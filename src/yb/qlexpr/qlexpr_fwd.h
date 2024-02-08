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

#pragma once

#include <memory>

#include "yb/common/value.fwd.h"

namespace yb::qlexpr {

class IndexInfo;
class IndexMap;
class PgsqlScanSpec;
class QLExprExecutor;
class QLTableRow;
class QLResultSet;
class QLRow;
class QLRowBlock;
class QLScanSpec;
class YQLScanSpec;

struct IndexColumn;
struct QLTableColumn;

template <class Val>
class ExprResultWriter;

using QLExprExecutorPtr = std::shared_ptr<QLExprExecutor>;
using LWExprResultWriter = ExprResultWriter<LWQLValuePB>;
using QLExprResultWriter = ExprResultWriter<QLValuePB>;

enum class QLNameOption : int8_t;

}  // namespace yb::qlexpr
