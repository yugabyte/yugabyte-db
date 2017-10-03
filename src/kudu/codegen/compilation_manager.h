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

#ifndef KUDU_CODEGEN_COMPILATION_MANAGER_H
#define KUDU_CODEGEN_COMPILATION_MANAGER_H

#include "kudu/codegen/code_generator.h"
#include "kudu/codegen/code_cache.h"
#include "kudu/gutil/gscoped_ptr.h"
#include "kudu/gutil/macros.h"
#include "kudu/gutil/singleton.h"
#include "kudu/util/atomic.h"
#include "kudu/util/status.h"

namespace kudu {

class Counter;
class MetricEntity;
class MetricRegistry;
class ThreadPool;

namespace codegen {

class RowProjector;

// The compilation manager is a top-level class which manages the actual
// delivery of a code generator's output by maintaining its own
// threadpool and code cache. It accepts requests to compile various classes
// (all the ones that the CodeGenerator offers) and attempts to retrieve a
// cached copy. If no such copy exists, it adds a request to generate it.
//
// Class is thread safe.
//
// The compilation manager is available as a global singleton only because
// it is intended to be used on a per-tablet-server basis. While in
// certain unit tests (that don't depend on compilation performance),
// there may be multiple TSs per processes, this will not occur in a
// distributed enviornment, where each TS has its own process.
// Furthermore, using a singleton ensures that lower-level classes which
// depend on code generation need not depend on a top-level class which
// instantiates them to provide a compilation manager. This avoids many
// unnecessary dependencies on state and lifetime of top-level and
// intermediary classes which should not be aware of the code generation's
// use in the first place.
class CompilationManager {
 public:
  // Waits for all async tasks to finish.
  ~CompilationManager();

  static CompilationManager* GetSingleton() {
    return Singleton<CompilationManager>::get();
  }

  // If a codegenned row projector with compatible schemas (see
  // codegen::JITSchemaPair::ProjectionsCompatible) is ready,
  // then it is written to 'out' and true is returned.
  // Otherwise, this enqueues a compilation task for the parameter
  // schemas in the CompilationManager's thread pool and returns
  // false. Upon any failure, false is returned.
  // Does not write to 'out' if false is returned.
  bool RequestRowProjector(const Schema* base_schema,
                           const Schema* projection,
                           gscoped_ptr<RowProjector>* out);

  // Waits for all asynchronous compilation tasks to finish.
  void Wait();

  // Sets up a metric registry to observe the compilation manager's metrics.
  // This method is used instead of registering a counter with a given
  // registry because the CompilationManager is a singleton and there would
  // be lifetime issues if the manager was dependent on a single registry.
  Status StartInstrumentation(const scoped_refptr<MetricEntity>& metric_entity);

 private:
  friend class Singleton<CompilationManager>;
  CompilationManager();

  static void Shutdown();

  CodeGenerator generator_;
  CodeCache cache_;
  gscoped_ptr<ThreadPool> pool_;

  AtomicInt<int64_t> hit_counter_;
  AtomicInt<int64_t> query_counter_;

  static const int kDefaultCacheCapacity = 100;
  static const int kThreadTimeoutMs = 100;

  DISALLOW_COPY_AND_ASSIGN(CompilationManager);
};

} // namespace codegen
} // namespace kudu

#endif
