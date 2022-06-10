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

#include "yb/client/namespace_alterer.h"

#include "yb/client/client-internal.h"

#include "yb/master/master_ddl.pb.h"

namespace yb {
namespace client {

YBNamespaceAlterer::YBNamespaceAlterer(
    YBClient* client, const std::string& namespace_name, const std::string& namespace_id)
    : client_(client), namespace_name_(namespace_name), namespace_id_(namespace_id) {}

YBNamespaceAlterer::~YBNamespaceAlterer() {}

Status YBNamespaceAlterer::Alter(CoarseTimePoint deadline) {
  master::AlterNamespaceRequestPB req;
  RETURN_NOT_OK(ToRequest(&req));
  return client_->data_->AlterNamespace(client_, req, client_->PatchAdminDeadline(deadline));
}

YBNamespaceAlterer* YBNamespaceAlterer::RenameTo(const std::string& new_name) {
  rename_to_ = new_name;
  return this;
}

YBNamespaceAlterer* YBNamespaceAlterer::SetDatabaseType(YQLDatabase type) {
  database_type_ = type;
  return this;
}

Status YBNamespaceAlterer::ToRequest(master::AlterNamespaceRequestPB* req) {
  req->mutable_namespace_()->set_name(namespace_name_);
  if (!namespace_id_.empty()) {
    req->mutable_namespace_()->set_id(namespace_id_);
  }
  if (database_type_) {
    req->mutable_namespace_()->set_database_type(*database_type_);
  }
  if (!rename_to_) {
    return STATUS(InvalidArgument, "Cannot alter namespace without specifying new name");
  }
  req->set_new_name(*rename_to_);
  return Status::OK();
}

}  // namespace client
}  // namespace yb
