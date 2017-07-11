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
//
//
// This file contains common code for evaluating YQLExpressions that may be used by client or server
// TODO (Mihnea or Neil) This should be integrated into YQLExprExecutor when or after support for
// selecting expressions is done.
//--------------------------------------------------------------------------------------------------

#ifndef YB_COMMON_YQL_EXPRESSION_H
#define YB_COMMON_YQL_EXPRESSION_H

#include "yb/common/yql_protocol.pb.h"
#include "yb/util/status.h"
#include "yb/common/yql_value.h"
#include "yb/common/yql_rowblock.h"
#include "yb/common/yql_bfunc.h"

namespace yb {

enum class WriteAction : int {
  REPLACE, // default
  EXTEND, // plus for map and set
  APPEND, // plus for lists
  PREPEND, // plus for lists
  REMOVE_KEYS, // minus for map and set
  REMOVE_VALUES, // minus for lists
};

class YQLExpression {
 public:

  static CHECKED_STATUS Evaluate(const YQLExpressionPB &yql_expr,
                                 const YQLTableRow &table_row,
                                 YQLValueWithPB *result,
                                 WriteAction *write_action);
};

} // namespace yb

#endif // YB_COMMON_YQL_EXPRESSION_H
