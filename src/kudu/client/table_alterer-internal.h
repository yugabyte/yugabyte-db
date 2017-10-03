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
#ifndef KUDU_CLIENT_TABLE_ALTERER_INTERNAL_H
#define KUDU_CLIENT_TABLE_ALTERER_INTERNAL_H

#include <boost/optional.hpp>
#include <string>
#include <vector>

#include "kudu/client/client.h"
#include "kudu/master/master.pb.h"
#include "kudu/util/status.h"

namespace kudu {
namespace master {
class AlterTableRequestPB_AlterColumn;
} // namespace master
namespace client {

class KuduColumnSpec;

class KuduTableAlterer::Data {
 public:
  Data(KuduClient* client, std::string name);
  ~Data();
  Status ToRequest(master::AlterTableRequestPB* req);


  KuduClient* const client_;
  const std::string table_name_;

  Status status_;

  struct Step {
    master::AlterTableRequestPB::StepType step_type;

    // Owned by KuduTableAlterer::Data.
    KuduColumnSpec *spec;
  };
  std::vector<Step> steps_;

  MonoDelta timeout_;

  bool wait_;

  boost::optional<std::string> rename_to_;

 private:
  DISALLOW_COPY_AND_ASSIGN(Data);
};

} // namespace client
} // namespace kudu

#endif
