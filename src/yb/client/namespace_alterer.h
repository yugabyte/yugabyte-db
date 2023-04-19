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

#include <string>

#include "yb/client/client_fwd.h"

#include "yb/common/common_fwd.h"
#include "yb/common/common_types.pb.h"

#include "yb/master/master_ddl.fwd.h"
#include "yb/master/master_fwd.h"

#include "yb/util/monotime.h"
#include "yb/util/status.h"

namespace yb {
namespace client {

class YBNamespaceAlterer {
 public:
  ~YBNamespaceAlterer();

  YBNamespaceAlterer* RenameTo(const std::string& new_name);
  YBNamespaceAlterer* SetDatabaseType(YQLDatabase type);

  Status Alter(CoarseTimePoint deadline = CoarseTimePoint());

 private:
  friend class YBClient;

  YBNamespaceAlterer(
      YBClient* client, const std::string& namespace_name, const std::string& namespace_id);

  Status ToRequest(master::AlterNamespaceRequestPB* req);

  YBClient* const client_;
  const std::string namespace_name_;
  const std::string namespace_id_;

  Status status_;

  boost::optional<std::string> rename_to_;
  boost::optional<YQLDatabase> database_type_;

  DISALLOW_COPY_AND_ASSIGN(YBNamespaceAlterer);
};

}  // namespace client
}  // namespace yb
