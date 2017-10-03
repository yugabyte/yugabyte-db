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
#ifndef KUDU_CLIENT_ERROR_COLLECTOR_H
#define KUDU_CLIENT_ERROR_COLLECTOR_H

#include <vector>

#include "kudu/gutil/gscoped_ptr.h"
#include "kudu/gutil/macros.h"
#include "kudu/gutil/ref_counted.h"
#include "kudu/util/locks.h"
#include "kudu/util/status.h"

namespace kudu {
namespace client {

class KuduError;
class KuduInsert;

namespace internal {

class ErrorCollector : public RefCountedThreadSafe<ErrorCollector> {
 public:
  ErrorCollector();

  void AddError(gscoped_ptr<KuduError> error);

  // See KuduSession for details.
  int CountErrors() const;

  // See KuduSession for details.
  void GetErrors(std::vector<KuduError*>* errors, bool* overflowed);

 private:
  friend class RefCountedThreadSafe<ErrorCollector>;
  virtual ~ErrorCollector();

  mutable simple_spinlock lock_;
  std::vector<KuduError*> errors_;

  DISALLOW_COPY_AND_ASSIGN(ErrorCollector);
};

} // namespace internal
} // namespace client
} // namespace kudu
#endif /* KUDU_CLIENT_ERROR_COLLECTOR_H */
