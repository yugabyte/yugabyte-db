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

#include <boost/intrusive/list.hpp>

#include "yb/consensus/consensus_types.pb.h"

#include "yb/util/opid.h"
#include "yb/util/status.h"

namespace yb {
namespace tablet {

// Checks whether operation with specified id and type is allowed in some scenario defined
// by the user of this class.
// Please note that this class does not enforce any restrictions on its usage, and
// doesn't care about scenarios that use this class.
class OperationFilter : public boost::intrusive::list_base_hook<> {
 public:
  virtual Status CheckOperationAllowed(
      const OpId& id, consensus::OperationType op_type) const = 0;

  virtual ~OperationFilter() = default;
};

template <class F>
class FunctorOperationFilter : public OperationFilter {
 public:
  explicit FunctorOperationFilter(const F& f) : f_(f) {}

  Status CheckOperationAllowed(
      const OpId& id, consensus::OperationType op_type) const override {
    return f_(id, op_type);
  }

 private:
  F f_;
};

template <class F>
std::unique_ptr<OperationFilter> MakeFunctorOperationFilter(const F& f) {
  return std::make_unique<FunctorOperationFilter<F>>(f);
}

}  // namespace tablet
}  // namespace yb
