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

#include "yb/dockv/expiration.h"

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

#include "yb/util/result.h"
#include "yb/util/slice.h"
#include "yb/util/status.h"
#include "yb/util/tostring.h"

namespace yb::dockv {

Result<MonoDelta> Expiration::ComputeRelativeTtl(const HybridTime& input_time) {
  if (input_time < write_ht)
    return STATUS(Corruption, "Read time earlier than record write time.");
  if (ttl == ValueControlFields::kMaxTtl || ttl.IsNegative()) {
    return ttl;
  }
  MonoDelta elapsed_time = MonoDelta::FromMicroseconds(
      input_time.GetPhysicalValueMicros() - write_ht.GetPhysicalValueMicros());
  // This way, we keep the default TTL, and all negative TTLs are expired.
  MonoDelta new_ttl(ttl);
  return new_ttl -= elapsed_time;
}

std::string Expiration::ToString() const {
  return YB_STRUCT_TO_STRING(ttl, write_ht, always_override);
}

}  // namespace yb::dockv
