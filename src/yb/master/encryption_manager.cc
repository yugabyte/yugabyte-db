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

#include "yb/master/encryption_manager.h"

#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>

#include "yb/encryption/encryption.pb.h"

#include "yb/master/master_encryption.pb.h"
#include "yb/master/master_cluster.pb.h"
#include "yb/master/master_heartbeat.pb.h"
#include "yb/master/master_replication.pb.h"
#include "yb/master/universe_key_registry_service.h"

#include "yb/util/env.h"
#include "yb/util/pb_util.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"

using namespace std::chrono_literals;

namespace yb {
namespace master {

EncryptionManager::EncryptionManager()
    : universe_keys_(std::make_unique<encryption::UniverseKeysPB>()) {}

Status EncryptionManager::AddUniverseKeys(
    const AddUniverseKeysRequestPB* req, AddUniverseKeysResponsePB* resp) {
  {
    std::lock_guard<simple_spinlock> l(universe_key_mutex_);
    for (const auto& entry : req->universe_keys().map()) {
      (*universe_keys_->mutable_map())[entry.first] = entry.second;
    }
  }
  LOG(INFO) << "After AddUniverseKeys, key ids in memory: " << UniverseKeyIdsString();
  return Status::OK();
}


Status EncryptionManager::GetUniverseKeyRegistry(const GetUniverseKeyRegistryRequestPB* req,
                                                 GetUniverseKeyRegistryResponsePB* resp) {
  {
    std::lock_guard<simple_spinlock> l(universe_key_mutex_);
    *resp->mutable_universe_keys() = *universe_keys_;
  }
  LOG(INFO) << "Responding to GetUniverseKeyRegistry request with key ids: "
            << UniverseKeyIdsString();
  return Status::OK();
}

Status EncryptionManager::HasUniverseKeyInMemory(
    const HasUniverseKeyInMemoryRequestPB* req, HasUniverseKeyInMemoryResponsePB* resp) {
  bool has_key;
  {
    std::lock_guard<simple_spinlock> l(universe_key_mutex_);
    has_key = universe_keys_->map().count(req->version_id()) != 0;
  }
  resp->set_has_key(has_key);
  LOG(INFO) << Format("For universe key $0, has key: $1", req->version_id(), has_key);
  return Status::OK();
}

Status EncryptionManager::ChangeEncryptionInfo(const ChangeEncryptionInfoRequestPB* req,
                                               EncryptionInfoPB* encryption_info) {
  std::string old_universe_key;
  std::string new_universe_key;
  std::string new_key_version_id;

  if (encryption_info->encryption_enabled()) {
    old_universe_key = VERIFY_RESULT(GetLatestUniverseKey(encryption_info));
  }

  if (req->encryption_enabled()) {
    new_universe_key = VERIFY_RESULT(GetUniverseKeyFromRotateRequest(req));
    new_key_version_id = req->has_version_id()
        ? req->version_id()
        : to_string(boost::uuids::random_generator()());
  }

  RETURN_NOT_OK(enterprise::RotateUniverseKey(Slice(old_universe_key), Slice(new_universe_key),
                                              new_key_version_id, req->encryption_enabled(),
                                              encryption_info));

  if (req->has_key_path()) {
    encryption_info->set_key_path(req->key_path());
  }
  encryption_info->set_key_in_memory(req->in_memory());

  LOG(INFO) << "Finished ChangeEncryptionInfo with key id " << new_key_version_id;
  return Status::OK();
}

Status EncryptionManager::IsEncryptionEnabled(const EncryptionInfoPB& encryption_info,
                                              IsEncryptionEnabledResponsePB* resp) {
  resp->set_encryption_enabled(encryption_info.encryption_enabled());
  if (!encryption_info.encryption_enabled()) {
    return Status::OK();
  }

  // Decrypt the universe key registry to get the latest version id.
  auto universe_key = VERIFY_RESULT(GetLatestUniverseKey(&encryption_info));
  auto decrypted_registry =
      VERIFY_RESULT(enterprise::DecryptUniverseKeyRegistry(
          encryption_info.universe_key_registry_encoded(), Slice(universe_key)));
  auto universe_key_registry =
      VERIFY_RESULT(pb_util::ParseFromSlice<encryption::UniverseKeyRegistryPB>(decrypted_registry));

  resp->set_key_id(universe_key_registry.latest_version_id());
  LOG(INFO) << Format("Cluster encrypted with key $0", resp->key_id());

  return Status::OK();
}

CHECKED_STATUS EncryptionManager::FillHeartbeatResponseEncryption(
    const EncryptionInfoPB& encryption_info, TSHeartbeatResponsePB* resp) {
  Slice decrypted_registry(encryption_info.universe_key_registry_encoded());
  std::string decrypted;
  if (encryption_info.encryption_enabled()) {
    auto res = GetLatestUniverseKey(&encryption_info);
    if (!res.ok()) {
      LOG(WARNING) << "Leader master does not have universe key.";
      return Status::OK();
    }
    decrypted = VERIFY_RESULT(enterprise::DecryptUniverseKeyRegistry(decrypted_registry, *res));
    decrypted_registry = Slice(decrypted);
  }

  auto registry = VERIFY_RESULT(pb_util::ParseFromSlice<encryption::UniverseKeyRegistryPB>(
      decrypted_registry));
  resp->mutable_universe_key_registry()->CopyFrom(registry);
  return Status::OK();
}

void EncryptionManager::PopulateUniverseKeys(
      const encryption::UniverseKeysPB& universe_key_registry) {
  std::lock_guard<simple_spinlock> l(universe_key_mutex_);
  for (const auto& entry : universe_key_registry.map()) {
    (*universe_keys_->mutable_map())[entry.first] = entry.second;
  }
}

Result<std::string> EncryptionManager::GetLatestUniverseKey(
    const EncryptionInfoPB* encryption_info) {
  return GetKeyFromParams(encryption_info->key_in_memory(),
                          encryption_info->key_path(),
                          encryption_info->latest_version_id());
}

Result<std::string> EncryptionManager::GetUniverseKeyFromRotateRequest(
    const ChangeEncryptionInfoRequestPB* req) {
  return GetKeyFromParams(req->in_memory(), req->key_path(), req->version_id());
}

std::string EncryptionManager::UniverseKeyIdsString() {
  std::string key_ids;
  bool first = true;
  std::lock_guard<simple_spinlock> l(universe_key_mutex_);
  for (const auto& p : universe_keys_->map()) {
    if (first) {
      first = false;
    } else {
      key_ids += ", ";
    }
    key_ids += p.first;
  }
  return key_ids;
}

Result<std::string> EncryptionManager::GetKeyFromParams(
    bool in_memory, const std::string& key_path, const std::string& version_id) {
  if (in_memory) {
    std::lock_guard<simple_spinlock> l(universe_key_mutex_);
    const auto& it = universe_keys_->map().find(version_id);
    if (it == universe_keys_->map().end()) {
      return STATUS_SUBSTITUTE(NotFound, "Could not find key with version $0", version_id);
    }
    return it->second;
  }

  faststring contents;
  RETURN_NOT_OK(ReadFileToString(Env::Default(), key_path, &contents));
  return contents.ToString();
}

} // namespace master
} // namespace yb
