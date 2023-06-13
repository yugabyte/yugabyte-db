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

#include "yb/tserver/pg_mutation_counter.h"
#include "yb/tserver/stateful_services/pg_auto_analyze_service.service.h"
#include "yb/tserver/stateful_services/stateful_service_base.h"

namespace yb {
namespace stateful_service {
class PgAutoAnalyzeService : public StatefulRpcServiceBase<PgAutoAnalyzeServiceIf> {
 public:
  explicit PgAutoAnalyzeService(
      const scoped_refptr<MetricEntity>& metric_entity,
      const std::shared_future<client::YBClient*>& client_future);

 private:
  void Activate() override;
  void Deactivate() override;
  virtual uint32 PeriodicTaskIntervalMs() const override;
  virtual Result<bool> RunPeriodicTask() override;
  Status UpdateMutationsSinceLastAnalyze();

  STATEFUL_SERVICE_IMPL_METHODS((IncreaseMutationCounters));

  tserver::PgMutationCounter pg_cluster_level_mutation_counter_;
};

}  // namespace stateful_service
}  // namespace yb
