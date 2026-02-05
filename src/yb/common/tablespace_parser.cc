//--------------------------------------------------------------------------------------------------
// Copyright (c) Yugabyte, Inc.
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
//--------------------------------------------------------------------------------------------------

#include "yb/common/tablespace_parser.h"

#include <boost/algorithm/string/predicate.hpp>

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>

#include "yb/util/enums.h"
#include "yb/util/flags/flag_tags.h"
#include "yb/util/format.h"
#include "yb/util/pb_util.h"
#include "yb/util/status.h"

using std::string;
using std::vector;

DEFINE_RUNTIME_bool(enable_tablespace_validation, true, "Whether to enable extended validation for "
    "tablespaces. This can be set to false to allow ysql_dump to succeed when creating tablespaces "
    "that were created before the checks.");

namespace yb {

const std::string TablespaceParser::kWildcardPlacement = "*";

namespace {

// Formats a JSON parse error with a visual pointer to the error location.
// Example output:
//   Missing a name for object member.
//     [{"cloud"}]
//        ^
std::string FormatJsonParseError(
    const std::string& input, rapidjson::ParseErrorCode error_code, size_t error_offset) {
  std::string pointer_line(error_offset + 2, ' ');  // +2 for "  " prefix
  pointer_line += '^';
  return Format(
      "$0\n  $1\n$2",
      rapidjson::GetParseError_En(error_code),
      input,
      pointer_line);
}

std::string GetRapidJsonTypeName(const rapidjson::Value& value) {
  rapidjson::Type type = value.GetType();
  switch (type) {
    case rapidjson::kNullType: return "null";
    case rapidjson::kFalseType: return "bool";
    case rapidjson::kTrueType: return "bool";
    case rapidjson::kObjectType: return "object";
    case rapidjson::kArrayType: return "array";
    case rapidjson::kStringType: return "string";
    case rapidjson::kNumberType: return "number";
  }
  FATAL_INVALID_ENUM_VALUE(rapidjson::Type, type);
}

std::string ReplicaPlacementOptionName(ReplicaType replica_type) {
  switch (replica_type) {
    case ReplicaType::kLive:
      return "replica_placement";
    case ReplicaType::kReadOnly:
      return "read_replica_placement";
  }
  FATAL_INVALID_ENUM_VALUE(ReplicaType, replica_type);
}

Status AppendPlacementDetails(
  const Status& status, ReplicaType replica_type, const std::string& placement_str) {
  if (status.ok() || placement_str.empty()) {
    return status;
  }
  return status.CloneAndAppend(Format(
      " Provided $0: $1", ReplicaPlacementOptionName(replica_type), placement_str));
}

}  // namespace

// TODO(#26671): When we start using this for the yb-admin APIs as well, we should take into
// account prefix placements (wildcards).
Status ValidateReplicationInfo(const ReplicationInfoPB& replication_info) {
  const PlacementInfoPB& live_replicas = replication_info.live_replicas();
  std::vector<const PlacementInfoPB*> replicas_to_validate{&live_replicas};

  const auto& read_replicas = replication_info.read_replicas();
  if (read_replicas.size() > 1)
    return STATUS_FORMAT(
        Corruption, "more than one read replica placement is not supported: $0",
        replication_info.ShortDebugString());

  if (read_replicas.size() == 1) {
    if (!read_replicas[0].has_placement_uuid())
      return STATUS_FORMAT(
          Corruption, "missing \"placement_uuid\" for read replica: $0",
          read_replicas[0].ShortDebugString());

    replicas_to_validate.push_back(&read_replicas[0]);
  }

  for (const auto* replicas : replicas_to_validate) {
    if (replicas->num_replicas() <= 0) {
      return STATUS_FORMAT(Corruption, "Invalid num_replicas: $0", replicas->num_replicas());
    }

    const auto& placements = replicas->placement_blocks();

    auto total_min_replicas = 0;
    for (auto i = placements.begin(); i != placements.end(); ++i) {
      if (i->min_num_replicas() <= 0) {
        return STATUS_FORMAT(
            Corruption, "min_num_replicas ($0) must be > 0", i->min_num_replicas());
      }
      total_min_replicas += i->min_num_replicas();
      // Check for duplicate placement blocks.
      for (auto j = placements.begin(); j != i; ++j) {
        if (pb_util::ArePBsEqual(i->cloud_info(), j->cloud_info(), nullptr /* diff_str */)) {
          return STATUS_FORMAT(
              Corruption, "Duplicate placement block found: $0",
              i->cloud_info().ShortDebugString());
        }
      }
    }

    if (total_min_replicas > replicas->num_replicas()) {
      return STATUS_FORMAT(
          Corruption,
          "Sum of min_num_replicas fields ($0) exceeds the total replication factor "
          "in the placement policy ($1)",
          total_min_replicas, replicas->num_replicas());
    }
  }

  // Affinitized leader zones should be contiguous.
  for (const auto& affinitized_leader : replication_info.multi_affinitized_leaders()) {
    if (affinitized_leader.zones_size() == 0) {
      return STATUS_FORMAT(
          Corruption,
          "Empty affinitized leader zone set found in replication info "
          "(leader preferences must be contiguous from 1..N): $0",
          replication_info.ShortDebugString());
    }
  }

  return Status::OK();
}

Status TablespaceParser::ReadReplicaPlacementInfoFromJson(
    const rapidjson::Document& placement,
    ReplicationInfoPB& replication_info) {
  if (placement.IsNull()) {
    return Status::OK();
  }

  PlacementInfoPB* placement_info = replication_info.add_read_replicas();
  return PlacementInfoFromJson(
      placement, placement_info, replication_info, ReplicaType::kReadOnly);
}

// Common function to parse the placement info from JSON for both live and read replica placements.
Status TablespaceParser::PlacementInfoFromJson(
    const rapidjson::Value& placement, PlacementInfoPB* placement_info,
    ReplicationInfoPB& replication_info, ReplicaType replica_type) {
  auto replica_type_str = ReplicaPlacementOptionName(replica_type);
  if (!placement.IsObject()) {
    return STATUS_FORMAT(
        Corruption,
        "Invalid JSON type for $0 option. Expected object.",
        replica_type_str);
  }

  if (placement.IsNull()) {
    return STATUS_FORMAT(Corruption, "Empty $0 option", replica_type_str);
  }

  // Parse the number of replicas.
  if (!placement.HasMember("num_replicas") || !placement["num_replicas"].IsInt()) {
    return STATUS_FORMAT(
        Corruption,
        "Invalid or missing \"num_replicas\" field in the $0 option",
        replica_type_str);
  }
  placement_info->set_num_replicas(placement["num_replicas"].GetInt());

  // Parse the placement uuid.
  if (placement.HasMember("placement_uuid")) {
    if (!placement["placement_uuid"].IsString())
      return STATUS_FORMAT(
          Corruption,
          "Invalid type for \"placement_uuid\" field in $0. Expected string, got $1.",
          replica_type_str, GetRapidJsonTypeName(placement["placement_uuid"]));
    placement_info->set_placement_uuid(placement["placement_uuid"].GetString());
  }

  // Parse the placement blocks.
  // It is possible to have no placement blocks -
  // it just means we have no placement constraints on the total num replicas.
  if (placement.HasMember("placement_blocks") && !placement["placement_blocks"].IsArray()) {
    return STATUS_FORMAT(
        Corruption, "\"placement_blocks\" in $0 option should be an array", replica_type_str);
  }

  if (placement.HasMember("placement_blocks")) {
    const rapidjson::Value& pb_arr = placement["placement_blocks"];
    for (rapidjson::SizeType i = 0; i < pb_arr.Size(); ++i) {
      const rapidjson::Value& placement = pb_arr[i];
      if (!placement.HasMember("cloud") || !placement.HasMember("region") ||
          !placement.HasMember("zone") || !placement.HasMember("min_num_replicas")) {
        return STATUS_FORMAT(
            Corruption,
            "$0: Missing required key (cloud/region/zone/min_num_replicas) in "
            "placement block.\n"
            "To indicate that any region/zone is acceptable, use the wildcard character: '$1'",
            replica_type_str, kWildcardPlacement);
      }
      auto validate_non_empty_string = [&](const char *field_name) -> Status {
        const auto& field = placement[field_name];
        if (!field.IsString()) {
          return STATUS_FORMAT(
              Corruption,
              "$0: Invalid type for \"$1\" in placement block. Expected string, got $2.",
              replica_type_str, field_name, GetRapidJsonTypeName(field));
        }
        if (strlen(field.GetString()) == 0) {
          return STATUS_FORMAT(
              Corruption, "$0: \"$1\" in placement block cannot be empty.",
              replica_type_str, field_name);
        }
        return Status::OK();
      };
      RETURN_NOT_OK(validate_non_empty_string("cloud"));
      RETURN_NOT_OK(validate_non_empty_string("region"));
      RETURN_NOT_OK(validate_non_empty_string("zone"));
      if (!placement["min_num_replicas"].IsInt()) {
        return STATUS_FORMAT(
            Corruption,
            "$0: Invalid type for \"min_num_replicas\" in placement block. Expected int, got $1.",
            replica_type_str, GetRapidJsonTypeName(placement["min_num_replicas"]));
      }

      auto* placement_block = placement_info->add_placement_blocks();
      placement_block->set_min_num_replicas(placement["min_num_replicas"].GetInt());

      auto* cloud_info = placement_block->mutable_cloud_info();
      // The special value '*' is allowed for a placement to match any region/zone.
      // It is not allowed to use it for cloud, though because allowing any cloud, region
      // and zone is not really a placement constraint.
      if (std::string(placement["cloud"].GetString()) != kWildcardPlacement) {
        cloud_info->set_placement_cloud(placement["cloud"].GetString());
      } else {
        return STATUS_FORMAT(
            Corruption,
            "$0 placement policy: cannot use wildcard placement at cloud level.",
            replica_type_str);
      }

      bool in_wildcard = false;
      if (std::string(placement["region"].GetString()) != kWildcardPlacement) {
        cloud_info->set_placement_region(placement["region"].GetString());
      } else {
        in_wildcard = true;
      }

      if (std::string(placement["zone"].GetString()) != kWildcardPlacement) {
        if (in_wildcard) {
          return STATUS_FORMAT(
              Corruption,
              "$0: A wildcard '*' at region level should be followed by a "
              "wildcard at zone level.",
              replica_type_str);
        }
        cloud_info->set_placement_zone(placement["zone"].GetString());
      }
      // Add zones until we have at least leader_preference zones.
      if (placement.HasMember("leader_preference")) {
        if (replica_type != ReplicaType::kLive)
          return STATUS_FORMAT(
              Corruption,
              "$0: leader_preference is not supported for read replicas.",
              replica_type_str);

        if (!placement["leader_preference"].IsInt()) {
          return STATUS_FORMAT(
              Corruption,
              "Invalid type for leader_preference option in replica_placement. "
              "Expected int, got $0.",
              GetRapidJsonTypeName(placement["leader_preference"]));
        }

        if (placement["leader_preference"].GetInt() <= 0) {
          return STATUS_FORMAT(
              Corruption,
              "Invalid value for leader_preference option in replica_placement. "
              "Expected: >0, got: $0",
              placement["leader_preference"].GetInt());
        }

        const int priority = placement["leader_preference"].GetInt();
        while (replication_info.multi_affinitized_leaders_size() < priority) {
          replication_info.add_multi_affinitized_leaders();
        }
        auto* zone_set = replication_info.mutable_multi_affinitized_leaders(priority - 1);
        zone_set->add_zones()->CopyFrom(*cloud_info);
      }
    }
  }
  return Status::OK();
}

Result<ReplicationInfoPB> TablespaceParser::FromJson(
    const string& live_placement, const rapidjson::Document& live_placement_document,
    const string& read_replica_placement,
    const rapidjson::Document& read_replica_placement_document, bool fail_on_validation_error) {
  ReplicationInfoPB replication_info;
  PlacementInfoPB* live_placement_info = replication_info.mutable_live_replicas();
  RETURN_NOT_OK(AppendPlacementDetails(
      PlacementInfoFromJson(
          live_placement_document, live_placement_info, replication_info, ReplicaType::kLive),
      ReplicaType::kLive, live_placement));
  RETURN_NOT_OK(AppendPlacementDetails(
      ReadReplicaPlacementInfoFromJson(
          read_replica_placement_document, replication_info),
      ReplicaType::kReadOnly, read_replica_placement));

  auto status = ValidateReplicationInfo(replication_info);
  if (fail_on_validation_error && FLAGS_enable_tablespace_validation) {
    RETURN_NOT_OK(status);
  } else {
    if (!status.ok()) {
      LOG(ERROR) << "Replication info validation failed: " << status;
    }
  }
  return replication_info;
}

Result<ReplicationInfoPB> TablespaceParser::FromString(
    const std::string& live_placement, const std::string& read_replica_placement,
    bool fail_on_validation_error) {
  // Example value of placement:
  //   '{"num_replicas":3,
  //     "placement_blocks": [
  //         {"cloud":"c1", "region":"r1", "zone":"z1", "min_num_replicas":1,
  //            "leader_preference":1},
  //         {"cloud":"c2", "region":"r2", "zone":"z2", "min_num_replicas":1},
  //         {"cloud":"c3", "region":"r3", "zone":"z3", "min_num_replicas":1}]}'

  rapidjson::Document live_placement_document;
  rapidjson::Document read_replica_placement_document;
  if (live_placement_document.Parse(live_placement.c_str()).HasParseError()) {
    return STATUS_FORMAT(
        Corruption, "JSON parsing of live placement option failed:\n$0",
        FormatJsonParseError(
            live_placement,
            live_placement_document.GetParseError(),
            live_placement_document.GetErrorOffset()));
  }
  if (!read_replica_placement.empty() &&
      read_replica_placement_document.Parse(read_replica_placement.c_str()).HasParseError()) {
    return STATUS_FORMAT(
        Corruption, "JSON parsing of read replica placement option failed:\n$0",
        FormatJsonParseError(
            read_replica_placement,
            read_replica_placement_document.GetParseError(),
            read_replica_placement_document.GetErrorOffset()));
  }
  return FromJson(
      live_placement, live_placement_document, read_replica_placement,
      read_replica_placement_document, fail_on_validation_error);
}

Result<ReplicationInfoPB> TablespaceParser::FromQLValue(
    const std::vector<std::string>& placements, bool fail_on_validation_error) {
  // Today, only replica_placement and read_replica_placement are supported.
  if (placements.empty()) {
    return STATUS_FORMAT(Corruption, "Unexpected number of options: $0", placements.size());
  }

  const auto kReplicaPlacementPrefix = "replica_placement=";
  const auto kReadReplicaPlacementPrefix = "read_replica_placement=";
  // Parse options, allowing replica_placement (live) and read_replica_placement (RR).
  // We only validate and return the live replica placement here; callers that need the
  // read-replica info should parse it separately from reloptions if needed.
  std::optional<string> live;
  std::optional<string> rr;
  for (const auto& opt : placements) {
    if (opt.starts_with(kReplicaPlacementPrefix)) {
      if (live.has_value()) {
        return STATUS(
            InvalidArgument, "duplicate replica_placement option found");
      }
      live = opt;
    } else if (opt.starts_with(kReadReplicaPlacementPrefix)) {
      if (rr.has_value()) {
        return STATUS(
            InvalidArgument,
            "duplicate read_replica_placement option found. Only one "
            "replica_placement option is supported via tablespaces.");
      }
      rr = opt;
    } else {
      return STATUS_FORMAT(
          InvalidArgument,
          "expected replica_placement or read_replica_placement option. Got $0",
          opt);
    }
  }
  if (!live) {
    return STATUS(InvalidArgument, "replica_placement (live) option not found in spcoptions");
  }
  const auto live_replica_str = live->substr(strlen(kReplicaPlacementPrefix));
  const auto read_replica_str = rr ? rr->substr(strlen(kReadReplicaPlacementPrefix)) : "";

  ReplicationInfoPB replication_info = VERIFY_RESULT(
      FromString(live_replica_str, read_replica_str, fail_on_validation_error));
  return replication_info;
}

LocalityLevel TablespaceParser::GetLocalityLevel(
    const CloudInfoPB& src_cloud_info, const CloudInfoPB& dest_cloud_info) {
  if (!src_cloud_info.has_placement_region() || !dest_cloud_info.has_placement_region() ||
      src_cloud_info.placement_region() != dest_cloud_info.placement_region()) {
    return LocalityLevel::kNone;
  }
  if (!src_cloud_info.has_placement_zone() || !dest_cloud_info.has_placement_zone() ||
      src_cloud_info.placement_zone() != dest_cloud_info.placement_zone()) {
    return LocalityLevel::kRegion;
  }
  return LocalityLevel::kZone;
}

} // namespace yb
