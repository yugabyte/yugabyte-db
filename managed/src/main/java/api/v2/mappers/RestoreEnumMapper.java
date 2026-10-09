// Copyright (c) YugabyteDB, Inc.

package api.v2.mappers;

import api.v2.models.RestoreState;
import api.v2.models.TableType;
import com.yugabyte.yw.models.Restore;
import com.yugabyte.yw.models.RestoreKeyspace;
import org.mapstruct.Mapper;
import org.mapstruct.ReportingPolicy;
import org.mapstruct.ValueMapping;
import org.mapstruct.ValueMappings;

/**
 * RestoreState carries x-enum-varnames in the OpenAPI schema (the generated Go constants are
 * package-scoped and Failed/Aborted collided with SupportBundleStatus), so the v2 constant names
 * are prefixed and no longer match the internal Restore.State names.
 */
@Mapper(unmappedSourcePolicy = ReportingPolicy.ERROR)
public interface RestoreEnumMapper {

  @ValueMappings({
    @ValueMapping(target = "RestoreCreated", source = "Created"),
    @ValueMapping(target = "RestoreInProgress", source = "InProgress"),
    @ValueMapping(target = "RestoreCompleted", source = "Completed"),
    @ValueMapping(target = "RestoreFailed", source = "Failed"),
    @ValueMapping(target = "RestoreAborted", source = "Aborted")
  })
  RestoreState toRestoreState(Restore.State state);

  @ValueMappings({
    @ValueMapping(target = "RestoreCreated", source = "Created"),
    @ValueMapping(target = "RestoreInProgress", source = "InProgress"),
    @ValueMapping(target = "RestoreCompleted", source = "Completed"),
    @ValueMapping(target = "RestoreFailed", source = "Failed"),
    @ValueMapping(target = "RestoreAborted", source = "Aborted")
  })
  RestoreState toRestoreState(RestoreKeyspace.State state);

  @ValueMappings({
    @ValueMapping(target = "YQL_TABLE_TYPE", source = "YQL_TABLE_TYPE"),
    @ValueMapping(target = "REDIS_TABLE_TYPE", source = "REDIS_TABLE_TYPE"),
    @ValueMapping(target = "PGSQL_TABLE_TYPE", source = "PGSQL_TABLE_TYPE"),
    @ValueMapping(
        target = "TRANSACTION_STATUS_TABLE_TYPE",
        source = "TRANSACTION_STATUS_TABLE_TYPE")
  })
  TableType toTableType(org.yb.CommonTypes.TableType tableType);
}
