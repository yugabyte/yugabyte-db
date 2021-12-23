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

#ifndef ENT_SRC_YB_MASTER_CDC_RPC_TASKS_H
#define ENT_SRC_YB_MASTER_CDC_RPC_TASKS_H

#include <stdlib.h>

#include <google/protobuf/repeated_field.h>

#include "yb/common/entity_ids.h"

#include "yb/util/status_fwd.h"
#include "yb/util/net/net_util.h"

namespace yb {
namespace client {

class YBClient;
class YBTableName;

} // namespace client

namespace rpc {

class Messenger;
class SecureContext;

} // namespace rpc

namespace master {

class TableIdentifierPB;
class TabletLocationsPB;

class CDCRpcTasks {
 public:
  static Result<std::shared_ptr<CDCRpcTasks>> CreateWithMasterAddrs(
      const std::string& producer_id, const std::string& master_addrs);

  ~CDCRpcTasks();

  Result<google::protobuf::RepeatedPtrField<TabletLocationsPB>> GetTableLocations(
      const std::string& table_id);
  // Returns a list of (table id, table name).
  Result<std::vector<std::pair<TableId, client::YBTableName>>> ListTables();
  client::YBClient* client() const {
    return yb_client_.get();
  }
  Result<client::YBClient*> UpdateMasters(const std::string& master_addrs);

 private:
  std::unique_ptr<rpc::SecureContext> secure_context_;
  std::unique_ptr<rpc::Messenger> messenger_;
  std::unique_ptr<client::YBClient> yb_client_;
};

} // namespace master
} // namespace yb


#endif // ENT_SRC_YB_MASTER_CDC_RPC_TASKS_H
