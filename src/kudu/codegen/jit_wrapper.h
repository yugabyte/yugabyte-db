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

#ifndef KUDU_CODEGEN_JIT_WRAPPER_H
#define KUDU_CODEGEN_JIT_WRAPPER_H

#include <memory>

#include "kudu/gutil/casts.h"
#include "kudu/gutil/macros.h"
#include "kudu/gutil/ref_counted.h"
#include "kudu/util/status.h"

namespace llvm {
class ExecutionEngine;
} // namespace llvm

namespace kudu {

class faststring;

namespace codegen {

typedef llvm::ExecutionEngine JITCodeOwner;

// A JITWrapper is the combination of a jitted code and a pointer
// (or pointers) to function(s) within that jitted code. Holding
// a ref-counted pointer to a JITWrapper ensures the validity of
// the codegenned function. A JITWrapper owns its code uniquely.
//
// All independent units which should be codegenned should derive
// from this type and update the JITWrapperType enum below so that there is
// a consistent unique identifier among jitted cache keys (each class may
// have its own different key encodings after those bytes).
class JITWrapper : public RefCountedThreadSafe<JITWrapper> {
 public:
  enum JITWrapperType {
    ROW_PROJECTOR
  };

  // Returns the key encoding (for the code cache) for this upon success.
  // If two JITWrapper instances of the same type have the same key, then
  // their codegenned code should be functionally equivalent.
  // Appends key to 'out' upon success.
  // The key must be unique amongst all derived types of JITWrapper.
  // To do this, the type's enum value from JITWrapper::JITWrapperType
  // should be prefixed to out.
  virtual Status EncodeOwnKey(faststring* out) = 0;

 protected:
  explicit JITWrapper(std::unique_ptr<JITCodeOwner> owner);
  virtual ~JITWrapper();

 private:
  friend class RefCountedThreadSafe<JITWrapper>;

  std::unique_ptr<JITCodeOwner> owner_;

  DISALLOW_COPY_AND_ASSIGN(JITWrapper);
};

} // namespace codegen
} // namespace kudu

#endif
