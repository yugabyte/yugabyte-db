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

#include "yb/common/wire_protocol.h"
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
    std::lock_guard l(universe_key_mutex_);
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
    std::lock_guard l(universe_key_mutex_);
    *resp->mutable_universe_keys() = *universe_keys_;
  }
  LOG(INFO) << "Responding to GetUniverseKeyRegistry request with key ids: "
            << UniverseKeyIdsString();

  return Status::OK();
}

Status EncryptionManager::GetFullUniverseKeyRegistry(const EncryptionInfoPB& encryption_info,
                                                     GetFullUniverseKeyRegistryResponsePB* resp) {
  Slice registry(encryption_info.universe_key_registry_encoded());
  std::string decrypted_registry;
  if (encryption_info.encryption_enabled()) {
    // Decrypt the universe key registry.
    auto universe_key = VERIFY_RESULT(GetLatestUniverseKey(&encryption_info));
    decrypted_registry = VERIFY_RESULT(DecryptUniverseKeyRegistry(registry, Slice(universe_key)));
    registry = Slice(decrypted_registry);
  } // Otherwise, the registry should already be in decrypted form.
  auto universe_key_registry =
      VERIFY_RESULT(pb_util::ParseFromSlice<encryption::UniverseKeyRegistryPB>(registry));

  *resp->mutable_universe_key_registry() = universe_key_registry;

  return Status::OK();
}

Status EncryptionManager::HasUniverseKeyInMemory(
    const HasUniverseKeyInMemoryRequestPB* req, HasUniverseKeyInMemoryResponsePB* resp) {
  bool has_key;
  {
    std::lock_guard l(universe_key_mutex_);
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

  RETURN_NOT_OK(RotateUniverseKey(Slice(old_universe_key), Slice(new_universe_key),
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
      VERIFY_RESULT(DecryptUniverseKeyRegistry(
          encryption_info.universe_key_registry_encoded(), Slice(universe_key)));
  auto universe_key_registry =
      VERIFY_RESULT(pb_util::ParseFromSlice<encryption::UniverseKeyRegistryPB>(decrypted_registry));

  resp->set_key_id(universe_key_registry.latest_version_id());
  LOG(INFO) << Format("Cluster encrypted with key $0", resp->key_id());

  return Status::OK();
}

EncryptionManager::EncryptionState EncryptionManager::GetEncryptionState(
    const EncryptionInfoPB& encryption_info, IsEncryptionEnabledResponsePB* encryption_resp) {
  EncryptionState state = EncryptionState::kUnknown;

  Status encryption_status = IsEncryptionEnabled(encryption_info, encryption_resp);
  WARN_NOT_OK(encryption_status, "Unable to determine encryption status");
  if (encryption_status.ok()) {
    if (encryption_resp->encryption_enabled()) {
      state = EncryptionState::kEnabled;

      HasUniverseKeyInMemoryRequestPB has_key_in_mem_req;
      has_key_in_mem_req.set_version_id(encryption_resp->key_id());
      HasUniverseKeyInMemoryResponsePB has_key_in_mem_res;
      encryption_status = HasUniverseKeyInMemory(&has_key_in_mem_req, &has_key_in_mem_res);
      if (encryption_status.ok() && has_key_in_mem_res.has_error()) {
        encryption_status = StatusFromPB(has_key_in_mem_res.error().status());
      }

      WARN_NOT_OK(encryption_status, "Unable to determine if encryption keys are in memory");
      if (!encryption_status.ok()) {
        state = EncryptionState::kEnabledUnkownIfKeyIsInMem;
      } else if (!has_key_in_mem_res.has_key()) {
        state = EncryptionState::kEnabledKeyNotInMem;
      }
    } else {
      state = EncryptionState::kNeverEnabled;

      Slice registry_decrypted(encryption_info.universe_key_registry_encoded());
      auto universe_key_registry =
          pb_util::ParseFromSlice<encryption::UniverseKeyRegistryPB>(registry_decrypted);
      if (!universe_key_registry.ok()) {
        WARN_NOT_OK(
            universe_key_registry.status(), "Unable to determine if encryption is Disabled");
      } else if (!universe_key_registry->universe_keys().empty()) {
        state = EncryptionState::kDisabled;
      }
    }
  }
  return state;
}

Status EncryptionManager::FillHeartbeatResponseEncryption(
    const EncryptionInfoPB& encryption_info, TSHeartbeatResponsePB* resp) {
  Slice decrypted_registry(encryption_info.universe_key_registry_encoded());
  std::string decrypted;
  if (encryption_info.encryption_enabled()) {
    auto res = GetLatestUniverseKey(&encryption_info);
    if (!res.ok()) {
      LOG(WARNING) << "Leader master does not have universe key.";
      return Status::OK();
    }
    decrypted = VERIFY_RESULT(DecryptUniverseKeyRegistry(decrypted_registry, *res));
    decrypted_registry = Slice(decrypted);
  }

  auto registry = VERIFY_RESULT(pb_util::ParseFromSlice<encryption::UniverseKeyRegistryPB>(
      decrypted_registry));
  resp->mutable_universe_key_registry()->CopyFrom(registry);
  return Status::OK();
}

void EncryptionManager::PopulateUniverseKeys(
      const encryption::UniverseKeysPB& universe_key_registry) {
  std::lock_guard l(universe_key_mutex_);
  for (const auto& entry : universe_key_registry.map()) {
    (*universe_keys_->mutable_map())[entry.first] = entry.second;
  }
}

Result<std::string> EncryptionManager::GetLatestUniverseKey(
    const EncryptionInfoPB* encryption_info) {
  RSTATUS_DCHECK(encryption_info->encryption_enabled(), IllegalState,
      "Attempts to fetch the latest universe key under the EAR have been disabled");
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
  std::lock_guard l(universe_key_mutex_);
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
    std::lock_guard l(universe_key_mutex_);
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
