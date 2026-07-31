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

#include "yb/client/in_flight_op.h"

#include <boost/preprocessor.hpp>
#include <boost/preprocessor/arithmetic/dec.hpp>
#include <boost/preprocessor/control/expr_iif.hpp>
#include <boost/preprocessor/control/iif.hpp>
#include <boost/preprocessor/logical/bool.hpp>
#include <boost/preprocessor/punctuation/is_begin_parens.hpp>
#include <boost/preprocessor/repetition/for.hpp>
#include <boost/preprocessor/seq/elem.hpp>
#include <boost/preprocessor/seq/size.hpp>
#include <boost/preprocessor/tuple/elem.hpp>
#include <boost/preprocessor/tuple/to_seq.hpp>
#include <boost/preprocessor/variadic/elem.hpp>
#include <string>
#include <utility>

#include "yb/client/yb_op.h"  // IWYU pragma: keep
#include "yb/util/tostring.h"

namespace yb {
namespace client {

namespace internal {

InFlightOp::InFlightOp(std::shared_ptr<YBOperation> yb_op_, size_t seq_no)
    : yb_op(std::move(yb_op_)), sequence_number(seq_no) {
}

std::string InFlightOp::ToString() const {
  return YB_STRUCT_TO_STRING(yb_op, tablet, sequence_number);
}

} // namespace internal
} // namespace client
} // namespace yb
