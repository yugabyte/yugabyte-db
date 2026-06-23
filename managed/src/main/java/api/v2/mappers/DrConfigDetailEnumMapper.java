// Copyright (c) YugabyteDB, Inc.

package api.v2.mappers;

import api.v2.models.DrConfigReplicationDetailStatus;
import api.v2.models.TableRelationType;
import api.v2.models.TableType;
import com.yugabyte.yw.models.XClusterNamespaceConfig.Status;
import com.yugabyte.yw.models.XClusterTableConfig;
import com.yugabyte.yw.models.XClusterTableConfig.ReplicationStatusError;
import org.mapstruct.Mapper;
import org.mapstruct.ReportingPolicy;
import org.mapstruct.ValueMapping;
import org.mapstruct.ValueMappings;
import org.yb.master.MasterTypes.RelationType;

@Mapper(unmappedSourcePolicy = ReportingPolicy.ERROR)
public interface DrConfigDetailEnumMapper {

  @ValueMappings({
    @ValueMapping(target = "VALIDATED", source = "Validated"),
    @ValueMapping(target = "REPLICATION_RUNNING", source = "Running"),
    @ValueMapping(target = "UPDATING", source = "Updating"),
    @ValueMapping(target = "WARNING", source = "Warning"),
    @ValueMapping(target = "ERROR", source = "Error"),
    @ValueMapping(target = "REPLICATION_BOOTSTRAPPING", source = "Bootstrapping"),
    @ValueMapping(target = "REPLICATION_FAILED", source = "Failed"),
    @ValueMapping(target = "UNABLE_TO_FETCH", source = "UnableToFetch"),
    @ValueMapping(target = "DROPPED_FROM_SOURCE", source = "DroppedFromSource"),
    @ValueMapping(target = "DROPPED_FROM_TARGET", source = "DroppedFromTarget"),
    @ValueMapping(target = "EXTRA_TABLE_ON_SOURCE", source = "ExtraTableOnSource"),
    @ValueMapping(target = "EXTRA_TABLE_ON_TARGET", source = "ExtraTableOnTarget")
  })
  DrConfigReplicationDetailStatus toTableDetailStatus(XClusterTableConfig.Status status);

  @ValueMappings({
    @ValueMapping(target = "VALIDATED", source = "Validated"),
    @ValueMapping(target = "REPLICATION_RUNNING", source = "Running"),
    @ValueMapping(target = "UPDATING", source = "Updating"),
    @ValueMapping(target = "WARNING", source = "Warning"),
    @ValueMapping(target = "ERROR", source = "Error"),
    @ValueMapping(target = "REPLICATION_BOOTSTRAPPING", source = "Bootstrapping"),
    @ValueMapping(target = "REPLICATION_FAILED", source = "Failed")
  })
  DrConfigReplicationDetailStatus toDbDetailStatus(Status status);

  default TableType toTableType(org.yb.CommonTypes.TableType tableType) {
    if (tableType == null) {
      return null;
    }

    return switch (tableType) {
      case PGSQL_TABLE_TYPE -> TableType.YSQL;
      case YQL_TABLE_TYPE -> TableType.YCQL;
      default -> TableType.UNKNOWN;
    };
  }

  default TableRelationType toTableRelationType(RelationType relationType) {
    if (relationType == null) {
      return null;
    }

    return TableRelationType.fromValue(relationType.name());
  }

  default String mapReplicationStatusError(ReplicationStatusError error) {
    return error == null ? null : error.toString();
  }
}
