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

#pragma once

#include <functional>
#include <memory>

#include "yb/common/pg_types.h"

#include "yb/util/result.h"

namespace yb::pggate {

struct LightweightTableYbctid;
class YbctidReaderProvider;
struct BufferingSettings;

class PgFKReferenceCache {
 public:
  struct IntentOptions {
    bool is_region_local;
    bool is_deferred;
  };

  PgFKReferenceCache(YbctidReaderProvider& reader_provider,
                     std::reference_wrapper<const BufferingSettings> buffering_settings);
  ~PgFKReferenceCache();

  void Clear();
  void DeleteReference(const LightweightTableYbctid& key);
  void AddReference(const LightweightTableYbctid& key);
  Result<bool> IsReferenceExists(PgOid database_id, const LightweightTableYbctid& key);
  void AddIntent(const LightweightTableYbctid& key, const IntentOptions& options);
  void OnDeferredTriggersProcessingStarted();

 private:
  class Impl;

  std::unique_ptr<Impl> impl_;
};

} // namespace yb::pggate
