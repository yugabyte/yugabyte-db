//
// Copyright (c) YugaByte, Inc.
//

#include "yb/util/opid.h"

#include <iostream>

namespace yb {

std::ostream& operator<<(std::ostream& out, const OpId& op_id) {
  return out << "{term=" << op_id.term << ", index=" << op_id.index << "}";
}

} // namespace yb
