// Copyright (c) YugaByte, Inc.

#ifndef YB_COMMON_YQL_STORAGE_INTERFACE_H
#define YB_COMMON_YQL_STORAGE_INTERFACE_H

#include "yb/common/hybrid_time.h"
#include "yb/common/schema.h"
#include "yb/common/yql_rowwise_iterator_interface.h"

namespace yb {
namespace common {

// An interface to support various different storage backends for a YQL table.
class YQLStorageIf {
 public:
  virtual ~YQLStorageIf() {}

  virtual CHECKED_STATUS GetIterator(const Schema& projection, const Schema& schema,
                                     HybridTime req_hybrid_time,
                                     std::unique_ptr<YQLRowwiseIteratorIf>* iter) const = 0;
  virtual CHECKED_STATUS BuildYQLScanSpec(const YQLReadRequestPB& request,
                                          const HybridTime& hybrid_time,
                                          const Schema& schema,
                                          std::unique_ptr<common::YQLScanSpec>* spec,
                                          HybridTime* req_hybrid_time) const = 0;
};

}  // namespace common
}  // namespace yb
#endif // YB_COMMON_YQL_STORAGE_INTERFACE_H
