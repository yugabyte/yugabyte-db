// Copyright (c) YugaByte, Inc.
package api.v2.mappers;

import api.v2.models.UniverseEditGFlags;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import org.mapstruct.Mapper;
import org.mapstruct.ValueMapping;
import org.mapstruct.ValueMappings;

@Mapper
public interface UpgradeOptionEnumMapper {
  @ValueMappings({
    @ValueMapping(target = "ROLLING_UPGRADE", source = "ROLLING"),
    @ValueMapping(target = "NON_ROLLING_UPGRADE", source = "NON_ROLLING"),
    @ValueMapping(target = "NON_RESTART_UPGRADE", source = "NON_RESTART"),
  })
  UpgradeTaskParams.UpgradeOption toUpgradeOption(UniverseEditGFlags.UpgradeOptionEnum source);
}
