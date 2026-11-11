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

#include "yb/master_host/ycql_virtual_tables.h"

#include <limits>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "yb/common/ql_protocol.messages.h"
#include "yb/common/ql_type.h"
#include "yb/common/ql_value.h"
#include "yb/common/roles_permissions.h"
#include "yb/common/schema.h"
#include "yb/common/schema_pbutil.h"
#include "yb/common/wire_protocol.h"

#include "yb/dockv/partition.h"

#include "yb/master/master_defaults.h"
#include "yb/master/master_heartbeat.pb.h"
#include "yb/master/master_util.h"
#include "yb/master/util/yql_vtable_helpers.h"

#include "yb/qlexpr/index.h"
#include "yb/qlexpr/index_column.h"
#include "yb/qlexpr/ql_name.h"
#include "yb/qlexpr/ql_rowblock.h"

#include "yb/util/monotime.h"
#include "yb/util/net/dns_resolver.h"
#include "yb/util/net/inetaddress.h"
#include "yb/util/status_format.h"
#include "yb/util/uuid.h"
#include "yb/util/yb_partition.h"

namespace yb::master_host {

using master::YcqlVirtualTableInputPB;
using master::VTableDataPtr;

namespace {

const std::string& IndexedTableId(const master::SysTablesEntryPB& table) {
  static const std::string kEmpty;
  return table.has_index_info() ? table.index_info().indexed_table_id()
         : table.has_indexed_table_id() ? table.indexed_table_id() : kEmpty;
}

bool IsUniqueIndex(const master::SysTablesEntryPB& table) {
  return table.has_index_info() ? table.index_info().is_unique() : table.is_unique_index();
}

std::string SortingTypeToString(SortingType sorting_type) {
  switch (sorting_type) {
    case SortingType::kNotSpecified: return "none";
    case SortingType::kAscending: return "asc";
    case SortingType::kDescending: return "desc";
    case SortingType::kAscendingNullsLast: return "asc nulls last";
    case SortingType::kDescendingNullsLast: return "desc nulls last";
  }
  FATAL_INVALID_ENUM_VALUE(SortingType, sorting_type);
}

const std::string& ColumnName(const Schema& schema, ColumnId id) {
  auto column = schema.column_by_id(id);
  DCHECK(column.ok());
  return column->name();
}

// YQLIndexesVTable's QLExpressionPBToPredicateString.
std::string PredicateString(const QLExpressionPB& where_expr, const Schema& schema) {
  switch (where_expr.expr_case()) {
    case QLExpressionPB::EXPR_NOT_SET: return "NULL";
    case QLExpressionPB::kValue:
      return QLValue(where_expr.value()).ToValueString(QuotesType::kSingleQuotes);
    case QLExpressionPB::kColumnId: return ColumnName(schema, ColumnId(where_expr.column_id()));
    case QLExpressionPB::kCondition: {
      std::string res = PredicateString(where_expr.condition().operands(0), schema);
      switch (where_expr.condition().op()) {
        case QLOperator::QL_OP_AND: res += " AND "; break;
        case QLOperator::QL_OP_EQUAL: res += " = "; break;
        case QLOperator::QL_OP_NOT_EQUAL: res += " != "; break;
        case QLOperator::QL_OP_GREATER_THAN: res += " > "; break;
        case QLOperator::QL_OP_GREATER_THAN_EQUAL: res += " >= "; break;
        case QLOperator::QL_OP_LESS_THAN: res += " < "; break;
        case QLOperator::QL_OP_LESS_THAN_EQUAL: res += " <= "; break;
        default: break;
      }
      return res + PredicateString(where_expr.condition().operands(1), schema);
    }
    default: break;
  }
  return std::string();
}

QLValuePB StringMap(std::initializer_list<std::pair<std::string, std::string>> entries) {
  QLValue map;
  map.set_map_value();
  for (const auto& [key, value] : entries) {
    map.add_map_key()->set_string_value(key);
    map.add_map_value()->set_string_value(value);
  }
  return map.value();
}

class HostVirtualTable : public master::YQLVirtualTable {
 public:
  HostVirtualTable(
      const master::SystemTableDef& def, const TabletId& tablet_id,
      const scoped_refptr<MetricEntity>& metric_entity, DnsResolver* resolver,
      VirtualTableInputSource source)
      : YQLVirtualTable(def.table_name, def.namespace_name, metric_entity, def.create_schema()),
        namespace_name_(def.namespace_name),
        tablet_id_(tablet_id),
        resolver_(*resolver),
        source_(std::move(source)) {}

  Result<VTableDataPtr> RetrieveData(const QLReadRequestMsg& request) const override {
    const auto input = VERIFY_RESULT(source_(tablet_id_));
    auto vtable = std::make_shared<qlexpr::QLRowBlock>(schema());
    const auto& name = table_name();
    if (namespace_name_ == master::kSystemNamespaceName) {
      if (name == master::kSystemLocalTableName) {
        RETURN_NOT_OK(Local(request, input, vtable.get()));
      } else if (name == master::kSystemPeersTableName) {
        RETURN_NOT_OK(Peers(request, input, vtable.get()));
      } else if (name == master::kSystemPartitionsTableName) {
        RETURN_NOT_OK(Partitions(input, vtable.get()));
      } else if (name == master::kSystemSizeEstimatesTableName) {
        RETURN_NOT_OK(SizeEstimates(input, vtable.get()));
      }
    } else if (namespace_name_ == master::kSystemSchemaNamespaceName) {
      if (name == master::kSystemSchemaKeyspacesTableName) {
        RETURN_NOT_OK(Keyspaces(input, vtable.get()));
      } else if (name == master::kSystemSchemaTablesTableName) {
        RETURN_NOT_OK(Tables(input, vtable.get()));
      } else if (name == master::kSystemSchemaColumnsTableName) {
        RETURN_NOT_OK(Columns(input, vtable.get()));
      } else if (name == master::kSystemSchemaIndexesTableName) {
        RETURN_NOT_OK(Indexes(input, vtable.get()));
      } else if (name == master::kSystemSchemaTypesTableName) {
        RETURN_NOT_OK(Types(input, vtable.get()));
      }
      // Aggregates, functions, triggers and views are empty.
    } else if (namespace_name_ == master::kSystemAuthNamespaceName) {
      if (name == master::kSystemAuthRolesTableName) {
        RETURN_NOT_OK(Roles(input, vtable.get()));
      } else if (name == master::kSystemAuthRolePermissionsTableName) {
        RETURN_NOT_OK(RolePermissions(input, vtable.get()));
      } else if (name == master::kSystemAuthResourceRolePermissionsIndexTableName) {
        RETURN_NOT_OK(ResourceRolePermissionsIndex(input, vtable.get()));
      }
    }
    return vtable;
  }

 private:
  struct TServerEntry {
    size_t index;
    const master::TSInformationPB* ts_info;
    master::util::PublicPrivateIPFutures ips{};
  };

  // LocalVTable::RetrieveData.
  Status Local(
      const QLReadRequestMsg& request, const YcqlVirtualTableInputPB& input,
      qlexpr::QLRowBlock* vtable) const {
    std::vector<TServerEntry> entries;
    InetAddress remote_ip;
    for (int i = 0; i < input.tservers_size(); ++i) {
      const auto& ts_info = input.tservers(i);
      if (!request.proxy_uuid().empty()) {
        if (ts_info.tserver_instance().permanent_uuid() != request.proxy_uuid()) {
          continue;
        }
      } else {
        if (i == 0) {
          remote_ip = InetAddress(VERIFY_RESULT(resolver_.Resolve(
              request.remote_endpoint().host())));
        }
        if (!master::util::RemoteEndpointMatchesTServer(ts_info, remote_ip)) {
          continue;
        }
      }
      entries.push_back({static_cast<size_t>(i), &ts_info});
      entries.back().ips = master::util::GetPublicPrivateIPFutures(ts_info, &resolver_);
    }

    for (const auto& entry : entries) {
      auto& row = vtable->Extend();
      InetAddress private_ip(VERIFY_RESULT(Copy(entry.ips.private_ip_future.get())));
      InetAddress public_ip(VERIFY_RESULT(Copy(entry.ips.public_ip_future.get())));
      const auto& cloud_info = entry.ts_info->registration().common().cloud_info();
      RETURN_NOT_OK(SetColumnValue("key", "local", &row));
      RETURN_NOT_OK(SetColumnValue("bootstrapped", "COMPLETED", &row));
      RETURN_NOT_OK(SetColumnValue("broadcast_address", public_ip, &row));
      RETURN_NOT_OK(SetColumnValue("cluster_name", "local cluster", &row));
      RETURN_NOT_OK(SetColumnValue("cql_version", "3.4.2", &row));
      RETURN_NOT_OK(SetColumnValue("data_center", cloud_info.placement_region(), &row));
      RETURN_NOT_OK(SetColumnValue("gossip_generation", 0, &row));
      auto host_id = VERIFY_RESULT(Uuid::FromHexString(
          entry.ts_info->tserver_instance().permanent_uuid()));
      RETURN_NOT_OK(SetColumnValue("host_id", host_id, &row));
      RETURN_NOT_OK(SetColumnValue("listen_address", private_ip, &row));
      RETURN_NOT_OK(SetColumnValue("native_protocol_version", "4", &row));
      RETURN_NOT_OK(SetColumnValue(
          "partitioner", "org.apache.cassandra.dht.Murmur3Partitioner", &row));
      RETURN_NOT_OK(SetColumnValue("rack", cloud_info.placement_zone(), &row));
      RETURN_NOT_OK(SetColumnValue(
          master::kSystemTablesReleaseVersionColumn, master::kSystemTablesReleaseVersion, &row));
      RETURN_NOT_OK(SetColumnValue("rpc_address", public_ip, &row));
      Uuid schema_version = VERIFY_RESULT(Uuid::FromString(master::kDefaultSchemaVersion));
      RETURN_NOT_OK(SetColumnValue("schema_version", schema_version, &row));
      RETURN_NOT_OK(SetColumnValue("thrift_version", "20.1.0", &row));
      RETURN_NOT_OK(SetColumnValue(
          "tokens", master::util::GetTokensValue(entry.index, input.tservers_size()), &row));
      break;
    }
    return Status::OK();
  }

  // PeersVTable::RetrieveData.
  Status Peers(
      const QLReadRequestMsg& request, const YcqlVirtualTableInputPB& input,
      qlexpr::QLRowBlock* vtable) const {
    InetAddress remote_endpoint(VERIFY_RESULT(resolver_.Resolve(request.remote_endpoint().host())));
    const auto& proxy_uuid = request.proxy_uuid();
    std::vector<TServerEntry> entries;
    for (int i = 0; i < input.tservers_size(); ++i) {
      const auto& ts_info = input.tservers(i);
      if (!proxy_uuid.empty()) {
        if (ts_info.tserver_instance().permanent_uuid() == proxy_uuid) {
          continue;
        }
      } else if (master::util::RemoteEndpointMatchesTServer(ts_info, remote_endpoint)) {
        continue;
      }
      entries.push_back({static_cast<size_t>(i), &ts_info});
      entries.back().ips = master::util::GetPublicPrivateIPFutures(ts_info, &resolver_);
    }

    for (const auto& entry : entries) {
      auto private_ip = entry.ips.private_ip_future.get();
      if (!private_ip.ok()) {
        LOG(WARNING) << "Failed to get private ip from " << entry.ts_info->ShortDebugString()
                     << ": " << private_ip.status();
        continue;
      }
      auto public_ip = entry.ips.public_ip_future.get();
      if (!public_ip.ok()) {
        LOG(WARNING) << "Failed to get public ip from " << entry.ts_info->ShortDebugString()
                     << ": " << public_ip.status();
        continue;
      }
      auto& row = vtable->Extend();
      RETURN_NOT_OK(SetColumnValue("peer", InetAddress(*public_ip), &row));
      RETURN_NOT_OK(SetColumnValue("rpc_address", InetAddress(*public_ip), &row));
      RETURN_NOT_OK(SetColumnValue("preferred_ip", InetAddress(*private_ip), &row));
      const auto& cloud_info = entry.ts_info->registration().common().cloud_info();
      RETURN_NOT_OK(SetColumnValue("data_center", cloud_info.placement_region(), &row));
      RETURN_NOT_OK(SetColumnValue("rack", cloud_info.placement_zone(), &row));
      auto host_id = VERIFY_RESULT(Uuid::FromHexString(
          entry.ts_info->tserver_instance().permanent_uuid()));
      RETURN_NOT_OK(SetColumnValue("host_id", host_id, &row));
      RETURN_NOT_OK(SetColumnValue(
          master::kSystemTablesReleaseVersionColumn, master::kSystemTablesReleaseVersion, &row));
      Uuid schema_version = VERIFY_RESULT(Uuid::FromString(master::kDefaultSchemaVersion));
      RETURN_NOT_OK(SetColumnValue("schema_version", schema_version, &row));
      RETURN_NOT_OK(SetColumnValue(
          "tokens", master::util::GetTokensValue(entry.index, input.tservers_size()), &row));
    }
    return Status::OK();
  }

  // YQLKeyspacesVTable::RetrieveData.
  Status Keyspaces(const YcqlVirtualTableInputPB& input, qlexpr::QLRowBlock* vtable) const {
    for (const auto& keyspace : input.keyspaces()) {
      auto& row = vtable->Extend();
      RETURN_NOT_OK(SetColumnValue("keyspace_name", keyspace.name(), &row));
      RETURN_NOT_OK(SetColumnValue("durable_writes", true, &row));
      RETURN_NOT_OK(SetColumnValue(
          "replication", master::util::GetReplicationValue(keyspace.replication_factor()),
          &row));
    }
    return Status::OK();
  }

  // YQLTablesVTable::RetrieveData.
  Status Tables(const YcqlVirtualTableInputPB& input, qlexpr::QLRowBlock* vtable) const {
    for (const auto& entry : input.tables()) {
      const auto& table = entry.table();
      if (!IndexedTableId(table).empty()) {
        continue;
      }
      auto& row = vtable->Extend();
      RETURN_NOT_OK(SetColumnValue("keyspace_name", entry.namespace_name(), &row));
      RETURN_NOT_OK(SetColumnValue("table_name", table.name(), &row));
      QLValuePB flags;
      flags.mutable_set_value()->add_elems()->set_string_value("compound");
      RETURN_NOT_OK(SetColumnValue("flags", flags, &row));
      auto uuid = VERIFY_RESULT(Uuid::FromHexString(entry.id()));
      RETURN_NOT_OK(SetColumnValue("id", uuid, &row));

      Schema schema;
      RETURN_NOT_OK(SchemaFromPB(table.schema(), &schema));
      const auto& properties = schema.table_properties();
      // Milliseconds internally, seconds in CQL.
      int32_t cql_ttl = static_cast<int32_t>(
          properties.DefaultTimeToLive() / MonoTime::kMillisecondsPerSecond);
      RETURN_NOT_OK(SetColumnValue("default_time_to_live", cql_ttl, &row));
      if (properties.HasNumTablets()) {
        int32_t num_tablets = properties.num_tablets();
        RETURN_NOT_OK(SetColumnValue("tablets", num_tablets, &row));
      }
      RETURN_NOT_OK(SetColumnValue(
          "transactions",
          StringMap({{"enabled", properties.is_transactional() ? "true" : "false"}}), &row));
    }
    return Status::OK();
  }

  Status ColumnRow(
      const Schema& schema, const std::string& keyspace_name, const std::string& table_name,
      size_t col_idx, const std::string& kind, int32_t position,
      qlexpr::QLRowBlock* vtable) const {
    auto& row = vtable->Extend();
    const auto& column = schema.column(col_idx);
    RETURN_NOT_OK(SetColumnValue("keyspace_name", keyspace_name, &row));
    RETURN_NOT_OK(SetColumnValue("table_name", table_name, &row));
    RETURN_NOT_OK(SetColumnValue(
        "column_name",
        schema.table_properties().use_mangled_column_name()
            ? qlexpr::YcqlName::DemangleName(column.name()) : column.name(),
        &row));
    RETURN_NOT_OK(SetColumnValue(
        "clustering_order", SortingTypeToString(column.sorting_type()), &row));
    RETURN_NOT_OK(SetColumnValue(
        "type", column.is_counter() ? "counter" : column.type()->ToString(), &row));
    RETURN_NOT_OK(SetColumnValue("kind", kind, &row));
    RETURN_NOT_OK(SetColumnValue("position", position, &row));
    return Status::OK();
  }

  // YQLColumnsVTable::RetrieveData.
  Status Columns(const YcqlVirtualTableInputPB& input, qlexpr::QLRowBlock* vtable) const {
    for (const auto& entry : input.tables()) {
      Schema schema;
      RETURN_NOT_OK(SchemaFromPB(entry.table().schema(), &schema));
      const auto& keyspace_name = entry.namespace_name();
      const auto& table_name = entry.table().name();
      const auto num_hash = schema.num_hash_key_columns();
      const auto num_range = schema.num_range_key_columns();
      for (size_t i = 0; i < num_hash; ++i) {
        RETURN_NOT_OK(ColumnRow(
            schema, keyspace_name, table_name, i, "partition_key", static_cast<int32_t>(i),
            vtable));
      }
      for (size_t i = num_hash; i < num_hash + num_range; ++i) {
        RETURN_NOT_OK(ColumnRow(
            schema, keyspace_name, table_name, i, "clustering",
            static_cast<int32_t>(i - num_hash), vtable));
      }
      for (size_t i = num_hash + num_range; i < schema.num_columns(); ++i) {
        RETURN_NOT_OK(ColumnRow(
            schema, keyspace_name, table_name, i,
            schema.column(i).is_static() ? "static" : "regular", -1, vtable));
      }
    }
    return Status::OK();
  }

  // YQLIndexesVTable::RetrieveData.
  Status Indexes(const YcqlVirtualTableInputPB& input, qlexpr::QLRowBlock* vtable) const {
    std::unordered_map<std::string, const master::SysTablesEntryPB*> by_id;
    for (const auto& entry : input.tables()) {
      by_id.emplace(entry.id(), &entry.table());
    }
    for (const auto& entry : input.tables()) {
      const auto& table = entry.table();
      const auto& indexed_table_id = IndexedTableId(table);
      if (indexed_table_id.empty()) {
        continue;
      }
      auto it = by_id.find(indexed_table_id);
      if (it == by_id.end()) {
        continue;
      }
      const auto& indexed_table = *it->second;
      Schema indexed_schema;
      RETURN_NOT_OK(SchemaFromPB(indexed_table.schema(), &indexed_schema));

      auto& row = vtable->Extend();
      RETURN_NOT_OK(SetColumnValue("keyspace_name", entry.namespace_name(), &row));
      RETURN_NOT_OK(SetColumnValue("table_name", indexed_table.name(), &row));
      RETURN_NOT_OK(SetColumnValue("index_name", table.name(), &row));
      RETURN_NOT_OK(SetColumnValue("kind", "COMPOSITES", &row));

      // TableInfo::GetIndexInfo.
      auto index_info = [&] {
        for (const auto& index_info_pb : indexed_table.indexes()) {
          if (index_info_pb.table_id() == entry.id()) {
            return qlexpr::IndexInfo(index_info_pb);
          }
        }
        return qlexpr::IndexInfo();
      }();
      auto column_name = [&](size_t i) {
        return index_info.use_mangled_column_name()
            ? qlexpr::YcqlName::DemangleName(index_info.columns()[i].column_name)
            : ColumnName(indexed_schema, index_info.columns()[i].indexed_column_id);
      };
      const auto num_hash = index_info.hash_column_count();
      const auto num_range = index_info.range_column_count();
      std::string target;
      for (size_t i = 0; i < num_hash; ++i) {
        target += column_name(i);
        if (i != num_hash - 1) {
          target += ", ";
        }
      }
      if (num_hash > 1) {
        target = '(' + target + ')';
      }
      for (size_t i = num_hash; i < num_hash + num_range; ++i) {
        target += ", " + column_name(i);
      }
      std::string include;
      for (size_t i = num_hash + num_range; i < index_info.columns().size(); ++i) {
        include += column_name(i);
        if (i != index_info.columns().size() - 1) {
          include += ", ";
        }
      }
      std::string predicate;
      if (index_info.where_predicate_spec()) {
        predicate = PredicateString(
            index_info.where_predicate_spec()->where_expr(), indexed_schema);
      }
      QLValue options;
      options.set_map_value();
      options.add_map_key()->set_string_value("target");
      options.add_map_value()->set_string_value(target);
      if (!include.empty()) {
        options.add_map_key()->set_string_value("include");
        options.add_map_value()->set_string_value(include);
      }
      if (!predicate.empty()) {
        options.add_map_key()->set_string_value("predicate");
        options.add_map_value()->set_string_value(predicate);
      }
      RETURN_NOT_OK(SetColumnValue("options", options.value(), &row));
      RETURN_NOT_OK(SetColumnValue(
          "table_id", VERIFY_RESULT(Uuid::FromHexString(indexed_table_id)), &row));
      RETURN_NOT_OK(SetColumnValue("index_id", VERIFY_RESULT(Uuid::FromHexString(entry.id())),
                                   &row));

      Schema schema;
      RETURN_NOT_OK(SchemaFromPB(table.schema(), &schema));
      const auto& properties = schema.table_properties();
      if (properties.HasNumTablets()) {
        int32_t num_tablets = properties.num_tablets();
        RETURN_NOT_OK(SetColumnValue("tablets", num_tablets, &row));
      }
      QLValue txn;
      txn.set_map_value();
      txn.add_map_key()->set_string_value("enabled");
      txn.add_map_value()->set_string_value(properties.is_transactional() ? "true" : "false");
      if (properties.consistency_level() == YBConsistencyLevel::USER_ENFORCED) {
        txn.add_map_key()->set_string_value("consistency_level");
        txn.add_map_value()->set_string_value("user_enforced");
      }
      RETURN_NOT_OK(SetColumnValue("transactions", txn.value(), &row));
      RETURN_NOT_OK(SetColumnValue("is_unique", IsUniqueIndex(table), &row));
    }
    return Status::OK();
  }

  // QLTypesVTable::RetrieveData.
  Status Types(const YcqlVirtualTableInputPB& input, qlexpr::QLRowBlock* vtable) const {
    for (const auto& entry : input.types()) {
      const auto& type = entry.type();
      auto& row = vtable->Extend();
      RETURN_NOT_OK(SetColumnValue("keyspace_name", entry.namespace_name(), &row));
      RETURN_NOT_OK(SetColumnValue("type_name", type.name(), &row));
      QLValuePB field_names;
      for (const auto& field_name : type.field_names()) {
        field_names.mutable_list_value()->add_elems()->set_string_value(field_name);
      }
      RETURN_NOT_OK(SetColumnValue("field_names", field_names, &row));
      QLValuePB field_types;
      for (const auto& field_type : type.field_types()) {
        field_types.mutable_list_value()->add_elems()->set_string_value(
            QLType::FromQLTypePB(field_type)->ToString());
      }
      RETURN_NOT_OK(SetColumnValue("field_types", field_types, &row));
    }
    return Status::OK();
  }

  // YQLPartitionsVTable::GenerateAndCacheData, without its cache: rows ordered by table id, then
  // partition start.
  Status Partitions(const YcqlVirtualTableInputPB& input, qlexpr::QLRowBlock* vtable) const {
    std::unordered_map<std::string, std::shared_future<Result<IpAddress>>> lookups;
    for (const auto& entry : input.tables()) {
      for (const auto& tablet : entry.tablets()) {
        for (const auto& replica : tablet.replicas()) {
          auto host = master::DesiredHostPort(replica.ts_info(), CloudInfoPB()).host();
          if (!lookups.contains(host)) {
            lookups.emplace(host, resolver_.ResolveFuture(host));
          }
        }
      }
    }
    std::unordered_map<std::string, InetAddress> addresses;
    for (auto& [host, lookup] : lookups) {
      auto address = lookup.get();
      if (!address.ok()) {
        YB_LOG_EVERY_N_SECS(WARNING, 30) << "Unable to resolve host: " << address;
      } else {
        addresses.emplace(host, InetAddress(*address));
      }
    }

    std::map<std::string, const YcqlVirtualTableInputPB::Table*> by_id;
    for (const auto& entry : input.tables()) {
      by_id.emplace(entry.id(), &entry);
    }
    for (const auto& [_, entry] : by_id) {
      std::map<std::string, const master::TabletLocationsPB*> by_start;
      for (const auto& tablet : entry->tablets()) {
        by_start.emplace(tablet.partition().partition_key_start(), &tablet);
      }
      for (const auto& [start, tablet] : by_start) {
        auto& row = vtable->Extend();
        RETURN_NOT_OK(SetColumnValue("keyspace_name", entry->namespace_name(), &row));
        RETURN_NOT_OK(SetColumnValue("table_name", entry->table().name(), &row));
        RETURN_NOT_OK(SetColumnValue("start_key", start, &row));
        RETURN_NOT_OK(SetColumnValue("end_key", tablet->partition().partition_key_end(), &row));
        RETURN_NOT_OK(SetColumnValue(
            "id", VERIFY_RESULT(Uuid::FromHexString(tablet->tablet_id())), &row));
        QLValuePB replica_addresses;
        auto* map_value = replica_addresses.mutable_map_value();
        for (const auto& replica : tablet->replicas()) {
          auto host = master::DesiredHostPort(replica.ts_info(), CloudInfoPB()).host();
          auto address = addresses.find(host);
          if (address != addresses.end()) {
            QLValue::set_inetaddress_value(address->second, map_value->add_keys());
            map_value->add_values()->set_string_value(PeerRole_Name(replica.role()));
          }
        }
        RETURN_NOT_OK(SetColumnValue("replica_addresses", replica_addresses, &row));
      }
    }
    return Status::OK();
  }

  // YQLSizeEstimatesVTable::RetrieveData.
  Status SizeEstimates(const YcqlVirtualTableInputPB& input, qlexpr::QLRowBlock* vtable) const {
    auto cql_hash = [](const std::string& key) {
      uint16_t yb_hash =
          key.empty() ? 0 : dockv::PartitionSchema::DecodeMultiColumnHashValue(key);
      return std::to_string(YBPartition::YBToCqlHashCode(yb_hash));
    };
    for (const auto& entry : input.tables()) {
      for (const auto& tablet : entry.tablets()) {
        auto& row = vtable->Extend();
        RETURN_NOT_OK(SetColumnValue("keyspace_name", entry.namespace_name(), &row));
        RETURN_NOT_OK(SetColumnValue("table_name", entry.table().name(), &row));
        const auto& partition = tablet.partition();
        RETURN_NOT_OK(SetColumnValue(
            "range_start", cql_hash(partition.partition_key_start()), &row));
        RETURN_NOT_OK(SetColumnValue("range_end", cql_hash(partition.partition_key_end()), &row));
        RETURN_NOT_OK(SetColumnValue("mean_partition_size", 0, &row));
        RETURN_NOT_OK(SetColumnValue("partitions_count", 0, &row));
      }
    }
    return Status::OK();
  }

  // YQLAuthRolesVTable::RetrieveData.
  Status Roles(const YcqlVirtualTableInputPB& input, qlexpr::QLRowBlock* vtable) const {
    for (const auto& pb : input.roles()) {
      auto& row = vtable->Extend();
      RETURN_NOT_OK(SetColumnValue("role", pb.role(), &row));
      RETURN_NOT_OK(SetColumnValue("can_login", pb.can_login(), &row));
      RETURN_NOT_OK(SetColumnValue("is_superuser", pb.is_superuser(), &row));
      QLValuePB members;
      auto* list_value = members.mutable_list_value();
      for (const auto& member : pb.member_of()) {
        list_value->add_elems()->set_string_value(member);
      }
      RETURN_NOT_OK(SetColumnValue("member_of", members, &row));
      if (pb.has_salted_hash()) {
        RETURN_NOT_OK(SetColumnValue("salted_hash", pb.salted_hash(), &row));
      }
    }
    return Status::OK();
  }

  // YQLAuthRolePermissionsVTable::RetrieveData.
  Status RolePermissions(const YcqlVirtualTableInputPB& input, qlexpr::QLRowBlock* vtable) const {
    for (const auto& pb : input.roles()) {
      for (const auto& resource : pb.resources()) {
        auto& row = vtable->Extend();
        RETURN_NOT_OK(SetColumnValue("role", pb.role(), &row));
        RETURN_NOT_OK(SetColumnValue("resource", resource.canonical_resource(), &row));
        QLValuePB permissions;
        auto* list_value = permissions.mutable_list_value();
        for (auto permission : resource.permissions()) {
          auto permission_name = PermissionName(static_cast<PermissionType>(permission));
          if (permission_name.empty()) {
            return STATUS_FORMAT(
                InvalidArgument, "Unknown Permission $0",
                PermissionType_Name(static_cast<PermissionType>(permission)));
          }
          list_value->add_elems()->set_string_value(permission_name);
        }
        RETURN_NOT_OK(SetColumnValue("permissions", permissions, &row));
      }
    }
    return Status::OK();
  }

  // YQLAuthResourceRolePermissionsIndexVTable::RetrieveData.
  Status ResourceRolePermissionsIndex(
      const YcqlVirtualTableInputPB& input, qlexpr::QLRowBlock* vtable) const {
    for (const auto& pb : input.roles()) {
      for (const auto& resource : pb.resources()) {
        auto& row = vtable->Extend();
        RETURN_NOT_OK(SetColumnValue("resource", resource.canonical_resource(), &row));
        RETURN_NOT_OK(SetColumnValue("role", pb.role(), &row));
      }
    }
    return Status::OK();
  }

  const std::string namespace_name_;
  const TabletId tablet_id_;
  DnsResolver& resolver_;
  const VirtualTableInputSource source_;
};

}  // namespace

std::unique_ptr<master::YQLVirtualTable> MakeHostVirtualTable(
    const master::SystemTableDef& def, const TabletId& tablet_id,
    const scoped_refptr<MetricEntity>& metric_entity, DnsResolver* resolver,
    VirtualTableInputSource source) {
  return std::make_unique<HostVirtualTable>(
      def, tablet_id, metric_entity, resolver, std::move(source));
}

}  // namespace yb::master_host
