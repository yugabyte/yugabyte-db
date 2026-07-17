// Copyright (c) YugabyteDB, Inc.

package api.v2.mappers;

import api.v2.models.Restore;
import api.v2.models.RestoreInfo;
import api.v2.models.RestoreKeyspaceInfo;
import com.yugabyte.yw.common.backuprestore.BackupUtil;
import com.yugabyte.yw.models.RestoreKeyspace;
import com.yugabyte.yw.models.Universe;
import org.mapstruct.AfterMapping;
import org.mapstruct.Mapper;
import org.mapstruct.Mapping;
import org.mapstruct.MappingTarget;
import org.mapstruct.factory.Mappers;

@Mapper(
    config = CentralConfig.class,
    uses = {DateTimeMapper.class, RestoreEnumMapper.class})
public interface RestoreMapper {

  RestoreMapper INSTANCE = Mappers.getMapper(RestoreMapper.class);

  @Mapping(target = "info", source = ".")
  Restore toRestore(com.yugabyte.yw.models.Restore source);

  @Mapping(target = "uuid", source = "restoreUUID")
  @Mapping(target = "customerUuid", source = "customerUUID")
  @Mapping(target = "universeUuid", source = "universeUUID")
  @Mapping(target = "sourceUniverseUuid", source = "sourceUniverseUUID")
  @Mapping(target = "universeName", ignore = true)
  @Mapping(target = "isSourceUniversePresent", ignore = true)
  RestoreInfo toRestoreInfo(com.yugabyte.yw.models.Restore source);

  // Derived fields that require universe lookups. Use safe (non-throwing) lookups so a missing
  // target/source universe yields empty/false rather than surfacing an error.
  @AfterMapping
  default void fillDerivedRestoreInfo(
      @MappingTarget RestoreInfo target, com.yugabyte.yw.models.Restore source) {
    target.setUniverseName(
        Universe.maybeGet(source.getUniverseUUID()).map(Universe::getName).orElse(""));
    target.setIsSourceUniversePresent(
        source.getSourceUniverseUUID() != null
            && BackupUtil.checkIfUniverseExists(source.getSourceUniverseUUID()));
  }

  @Mapping(target = "restoreUuid", source = "restoreUUID")
  RestoreKeyspaceInfo toRestoreKeyspaceInfo(RestoreKeyspace source);
}
