// Copyright (c) YugabyteDB, Inc.

package api.v2.mappers;

import api.v2.models.PitrConfigInfo;
import api.v2.models.PitrConfigSpec;
import org.mapstruct.Mapper;
import org.mapstruct.ReportingPolicy;
import org.mapstruct.ValueMapping;
import org.mapstruct.ValueMappings;
import org.yb.CommonTypes.TableType;
import org.yb.master.CatalogEntityInfo.SysSnapshotEntryPB.State;

/** Explicit v1-to-v2 PITR config enum mappings with compile-time exhaustiveness checks. */
@Mapper(unmappedSourcePolicy = ReportingPolicy.ERROR)
public interface PitrConfigEnumMapper {

  @ValueMappings({
    @ValueMapping(target = "YQL_TABLE_TYPE", source = "YQL_TABLE_TYPE"),
    @ValueMapping(target = "REDIS_TABLE_TYPE", source = "REDIS_TABLE_TYPE"),
    @ValueMapping(target = "PGSQL_TABLE_TYPE", source = "PGSQL_TABLE_TYPE"),
    @ValueMapping(
        target = "TRANSACTION_STATUS_TABLE_TYPE",
        source = "TRANSACTION_STATUS_TABLE_TYPE")
  })
  PitrConfigSpec.TableTypeEnum toTableTypeEnum(TableType tableType);

  @ValueMappings({
    @ValueMapping(target = "UNKNOWN", source = "UNKNOWN"),
    @ValueMapping(target = "CREATING", source = "CREATING"),
    @ValueMapping(target = "COMPLETE", source = "COMPLETE"),
    @ValueMapping(target = "DELETING", source = "DELETING"),
    @ValueMapping(target = "DELETED", source = "DELETED"),
    @ValueMapping(target = "FAILED", source = "FAILED"),
    @ValueMapping(target = "CANCELLED", source = "CANCELLED"),
    @ValueMapping(target = "RESTORING", source = "RESTORING"),
    @ValueMapping(target = "RESTORED", source = "RESTORED")
  })
  PitrConfigInfo.StateEnum toStateEnum(State state);
}
