//
// Copyright (c) YugaByte, Inc.
//

#include "yb/util/opid.h"

#include <iostream>

#include <glog/logging.h>

namespace yb {

constexpr int64_t OpId::kUnknownTerm;

void OpId::UpdateIfGreater(const OpId& rhs) {
  if (rhs.index > index) {
    DCHECK_LE(term, rhs.term);
    *this = rhs;
  } else {
    DCHECK_LE(rhs.term, term);
  }
}

std::ostream& operator<<(std::ostream& out, const OpId& op_id) {
  return out << "{term=" << op_id.term << ", index=" << op_id.index << "}";
}

} // namespace yb
