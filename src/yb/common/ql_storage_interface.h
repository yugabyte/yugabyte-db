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

#ifndef YB_COMMON_QL_STORAGE_INTERFACE_H
#define YB_COMMON_QL_STORAGE_INTERFACE_H

#include <boost/optional.hpp>

#include "yb/common/hybrid_time.h"
#include "yb/common/schema.h"
#include "yb/common/ql_rowwise_iterator_interface.h"

namespace yb {
namespace common {

// An interface to support various different storage backends for a QL table.
class QLStorageIf {
 public:
  virtual ~QLStorageIf() {}

  virtual CHECKED_STATUS GetIterator(
      const QLReadRequestPB& request,
      const Schema& projection,
      const Schema& schema,
      const TransactionOperationContextOpt& txn_op_context,
      HybridTime req_hybrid_time,
      std::unique_ptr<QLRowwiseIteratorIf>* iter) const = 0;
  virtual CHECKED_STATUS BuildQLScanSpec(const QLReadRequestPB& request,
                                          const HybridTime& hybrid_time,
                                          const Schema& schema,
                                          bool include_static_columns,
                                          const Schema& static_projection,
                                          std::unique_ptr<common::QLScanSpec>* spec,
                                          std::unique_ptr<common::QLScanSpec>* static_row_spec,
                                          HybridTime* req_hybrid_time) const = 0;
};

}  // namespace common
}  // namespace yb
#endif // YB_COMMON_QL_STORAGE_INTERFACE_H
