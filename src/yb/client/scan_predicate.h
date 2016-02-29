// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
#ifndef KUDU_CLIENT_SCAN_PREDICATE_H
#define KUDU_CLIENT_SCAN_PREDICATE_H

#ifdef YB_HEADERS_NO_STUBS
#include "yb/gutil/macros.h"
#include "yb/gutil/port.h"
#else
#include "yb/client/stubs.h"
#endif

#include "yb/client/schema.h"
#include "yb/util/yb_export.h"

namespace yb {
namespace client {

class YB_EXPORT KuduPredicate {
 public:
  enum ComparisonOp {
    LESS_EQUAL,
    GREATER_EQUAL,
    EQUAL
  };

  ~KuduPredicate();

  // Returns a new, identical, KuduPredicate.
  KuduPredicate* Clone() const;

  // The PIMPL class has to be public since it's actually just an interface,
  // and gcc gives an error trying to derive from a private nested class.
  class YB_NO_EXPORT Data;
 private:
  friend class KuduScanner;
  friend class KuduTable;
  friend class ComparisonPredicateData;
  friend class ErrorPredicateData;

  explicit KuduPredicate(Data* d);

  Data* data_;
  DISALLOW_COPY_AND_ASSIGN(KuduPredicate);
};

} // namespace client
} // namespace yb
#endif // KUDU_CLIENT_SCAN_PREDICATE_H
