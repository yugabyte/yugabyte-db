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

#include <array>
#include <functional>
#include <string_view>
#include <utility>

#include "yb/gutil/macros.h"

#include "yb/util/metric_entity.h"
#include "yb/util/metrics_fwd.h"
#include "yb/util/result.h"
#include "yb/util/status.h"

namespace yb {
namespace server {

class RpcServerBase;

}

namespace pgwrapper {

class PGConn;

struct MetricWatcherDescriptor {
  MetricWatcherDescriptor(
      size_t* delta_receiver_,
      std::reference_wrapper<const MetricEntity::MetricMap> map_,
      std::reference_wrapper<const MetricPrototype> proto_)
      : delta_receiver(*delta_receiver_), map(map_), proto(proto_) {}

  size_t& delta_receiver;
  const MetricEntity::MetricMap& map;
  const MetricPrototype& proto;

 private:
  DISALLOW_COPY_AND_ASSIGN(MetricWatcherDescriptor);
};

using MetricWatcherFunctor = std::function<Status()>;

Status UpdateDelta(
  const MetricWatcherDescriptor* descrs, size_t count, const MetricWatcherFunctor& func);

const MetricEntity::MetricMap& GetMetricMap(
    std::reference_wrapper<const server::RpcServerBase> server);

template<class Describer>
class MetricWatcherBase {
 public:
  Result<typename Describer::DeltaType> Delta(const MetricWatcherFunctor& func) const {
    const auto& descrs = describer_.descriptors;
    RETURN_NOT_OK(UpdateDelta(descrs.data(), descrs.size(), func));
    return describer_.delta;
  }

 protected:
  explicit MetricWatcherBase(const Describer& describer)
      : describer_(describer) {}

 private:
  const Describer& describer_;
};

template<class Describer>
class MetricWatcher : private Describer, public MetricWatcherBase<Describer> {
 public:
  template<class... Args>
  explicit MetricWatcher(Args&&... args)
      : Describer(std::forward<Args>(args)...),
        MetricWatcherBase<Describer>(static_cast<const Describer&>(*this)) {}
};

template<class Delta, size_t N>
struct MetricWatcherDeltaDescriberTraits {
  using Descriptor = MetricWatcherDescriptor;
  using DeltaType = Delta;
  using Descriptors = std::array<Descriptor, N>;
};

struct SingleMetricDescriber : public MetricWatcherDeltaDescriberTraits<size_t, 1> {
  SingleMetricDescriber(std::reference_wrapper<const MetricEntity::MetricMap> map,
                        std::reference_wrapper<const MetricPrototype> metric)
      : descriptors{Descriptor{&delta, map, metric}} {}


  SingleMetricDescriber(std::reference_wrapper<const server::RpcServerBase> server,
                        std::reference_wrapper<const MetricPrototype> metric)
      : SingleMetricDescriber(GetMetricMap(server), metric) {}

  DeltaType delta;
  Descriptors descriptors;
};

using SingleMetricWatcher = MetricWatcher<SingleMetricDescriber>;

[[nodiscard]] bool HasTransactionError(const Status& status);
[[nodiscard]] bool IsRetryable(const Status& status);
[[nodiscard]] bool IsSerializeAccessError(const Status& status);

[[nodiscard]] std::string_view SerializeAccessErrorMessageSubstring();
[[nodiscard]] std::string MaxQueryLayerRetriesConf(uint16_t max_retries);


YB_STRONGLY_TYPED_BOOL(IsBreakingCatalogVersionChange);

Status SetNonDDLTxnAllowedForSysTableWrite(PGConn& conn, bool value);

Status IncrementAllDBCatalogVersions(
    PGConn& conn,
    IsBreakingCatalogVersionChange is_breaking = IsBreakingCatalogVersionChange::kTrue);

} // namespace pgwrapper
} // namespace yb
