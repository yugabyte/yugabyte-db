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

#include <boost/preprocessor.hpp>
#include <boost/preprocessor/arithmetic/dec.hpp>
#include <boost/preprocessor/control/expr_iif.hpp>
#include <boost/preprocessor/control/iif.hpp>
#include <boost/preprocessor/logical/bool.hpp>
#include <boost/preprocessor/repetition/for.hpp>
#include <boost/preprocessor/seq/elem.hpp>
#include <boost/preprocessor/seq/size.hpp>
#include <boost/preprocessor/tuple/elem.hpp>
#include <boost/preprocessor/tuple/to_seq.hpp>
#include <boost/preprocessor/variadic/elem.hpp>
#include <string>
#include <future>

#include "yb/tserver/stateful_services/stateful_service_base.h"
#include "yb/tserver/stateful_services/test_echo_service.service.h"
#include "yb/gutil/integral_types.h"
#include "yb/util/status.h"

template <class T> class scoped_refptr;

namespace yb {
class MetricEntity;
namespace client {
class YBClient;
}  // namespace client

namespace stateful_service {
class GetEchoCountRequestPB;
class GetEchoCountResponsePB;
class GetEchoRequestPB;
class GetEchoResponsePB;

class TestEchoService : public StatefulRpcServiceBase<TestEchoServiceIf> {
 public:
  TestEchoService(
      const std::string& node_uuid, const scoped_refptr<MetricEntity>& metric_entity,
      const std::shared_future<client::YBClient*>& client_future);

 private:
  void Activate() override;
  void Deactivate() override;
  Result<bool> RunPeriodicTask() override;
  Status RecordRequestInTable(const std::string& message);
  Status ReloadEchoCountFromTable();

  STATEFUL_SERVICE_IMPL_METHODS(
      GetEcho,
      GetEchoCount);

 private:
  const std::string node_uuid_;
  uint32 echo_count_ = 0;
};

}  // namespace stateful_service
}  // namespace yb
