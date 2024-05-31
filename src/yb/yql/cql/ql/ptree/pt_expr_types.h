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

#include "yb/common/value.messages.h"

#include "yb/util/memory/mc_types.h"

#include "yb/yql/cql/ql/ptree/ptree_fwd.h"

namespace yb {
namespace ql {

template<typename ReturnType>
class PTLiteral;

template<InternalType itype, DataType ytype,
         typename ReturnType, typename LiteralType = PTLiteral<ReturnType>>
class PTExprConst;

using PTConstText = PTExprConst<InternalType::kStringValue,
                                DataType::STRING,
                                MCSharedPtr<MCString>,
                                PTLiteralString>;
using PTConstVarInt = PTExprConst<InternalType::kVarintValue,
                                  DataType::VARINT,
                                  MCSharedPtr<MCString>,
                                  PTLiteralString>;
using PTConstDecimal = PTExprConst<InternalType::kDecimalValue,
                                   DataType::DECIMAL,
                                   MCSharedPtr<MCString>,
                                   PTLiteralString>;
using PTConstUuid = PTExprConst<InternalType::kUuidValue,
                                DataType::UUID,
                                MCSharedPtr<MCString>,
                                PTLiteralString>;
using PTConstBinary = PTExprConst<InternalType::kBinaryValue,
                                  DataType::BINARY,
                                  MCSharedPtr<MCString>,
                                  PTLiteralString>;

// Boolean constant.
using PTConstBool = PTExprConst<InternalType::kBoolValue,
                                DataType::BOOL,
                                bool>;

// Obsolete numeric constant classes.
using PTConstInt = PTExprConst<InternalType::kInt64Value,
                               DataType::INT64,
                               int64_t>;

using PTConstInt32 = PTExprConst<InternalType::kInt32Value,
                                 DataType::INT32,
                                 int32_t>;

using PTConstInt16 = PTExprConst<InternalType::kInt16Value,
                                 DataType::INT16,
                                 int16_t>;

using PTConstDouble = PTExprConst<InternalType::kDoubleValue,
                                  DataType::DOUBLE,
                                  long double>;

using PTConstFloat = PTExprConst<InternalType::kFloatValue,
                                 DataType::FLOAT,
                                 float>;

using PTConstTimestamp = PTExprConst<InternalType::kTimestampValue,
                                     DataType::TIMESTAMP,
                                     int64_t>;

using PTConstDate = PTExprConst<InternalType::kDateValue,
                                DataType::DATE,
                                uint32_t>;

template<InternalType itype, DataType ytype, class expr_class = PTExpr>
class PTExpr0;

template<InternalType itype, DataType ytype, class expr_class = PTExpr>
class PTExpr1;

template<InternalType itype, DataType ytype, class expr_class = PTExpr>
class PTExpr2;

template<InternalType itype, DataType ytype, class expr_class = PTExpr>
class PTExpr3;

using PTOperator0 = PTExpr0<InternalType::VALUE_NOT_SET, DataType::UNKNOWN_DATA, PTOperatorExpr>;
using PTOperator1 = PTExpr1<InternalType::VALUE_NOT_SET, DataType::UNKNOWN_DATA, PTOperatorExpr>;
using PTOperator2 = PTExpr2<InternalType::VALUE_NOT_SET, DataType::UNKNOWN_DATA, PTOperatorExpr>;
using PTOperator3 = PTExpr3<InternalType::VALUE_NOT_SET, DataType::UNKNOWN_DATA, PTOperatorExpr>;

using PTLogic1 = PTExpr1<InternalType::kBoolValue, DataType::BOOL, PTLogicExpr>;
using PTLogic2 = PTExpr2<InternalType::kBoolValue, DataType::BOOL, PTLogicExpr>;

using PTRelation0 = PTExpr0<InternalType::kBoolValue, DataType::BOOL, PTRelationExpr>;
using PTRelation1 = PTExpr1<InternalType::kBoolValue, DataType::BOOL, PTRelationExpr>;
using PTRelation2 = PTExpr2<InternalType::kBoolValue, DataType::BOOL, PTRelationExpr>;
using PTRelation3 = PTExpr3<InternalType::kBoolValue, DataType::BOOL, PTRelationExpr>;

}  // namespace ql
}  // namespace yb
