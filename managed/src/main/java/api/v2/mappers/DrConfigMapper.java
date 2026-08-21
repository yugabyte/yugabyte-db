// Copyright (c) YugabyteDB, Inc.

package api.v2.mappers;

import api.v2.models.DrConfig;
import api.v2.models.DrConfigBootstrapBackupParams;
import api.v2.models.DrConfigBootstrapParams;
import api.v2.models.DrConfigInfo;
import api.v2.models.DrConfigSpec;
import api.v2.models.DrConfigWebhook;
import com.yugabyte.yw.forms.DrConfigGetResp;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData.BootstrapParams.BootstrapBackupParams;
import com.yugabyte.yw.forms.XClusterConfigRestartFormData.RestartBootstrapParams;
import com.yugabyte.yw.forms.webhook.GetWebhookResponse;
import com.yugabyte.yw.models.XClusterConfig.ConfigType;
import java.util.List;
import org.mapstruct.AfterMapping;
import org.mapstruct.Mapper;
import org.mapstruct.Mapping;
import org.mapstruct.MappingTarget;
import org.mapstruct.factory.Mappers;

@Mapper(
    config = CentralConfig.class,
    uses = {DateTimeMapper.class, DrConfigEnumMapper.class})
public interface DrConfigMapper {

  DrConfigMapper INSTANCE = Mappers.getMapper(DrConfigMapper.class);

  @Mapping(target = "spec", source = ".")
  @Mapping(target = "info", source = ".")
  DrConfig toDrConfig(DrConfigGetResp source);

  @Mapping(target = "type", ignore = true)
  DrConfigSpec toDrConfigSpec(DrConfigGetResp source);

  @Mapping(target = "primaryUniverseActive", ignore = true)
  @Mapping(target = "drReplicaUniverseActive", ignore = true)
  @Mapping(target = "state", source = ".")
  DrConfigInfo toDrConfigInfo(DrConfigGetResp source);

  @Mapping(target = "backupRequestParams", source = "backupRequestParams")
  DrConfigBootstrapParams toBootstrapParams(RestartBootstrapParams source);

  @Mapping(target = "storageConfigUuid", source = "storageConfigUUID")
  DrConfigBootstrapBackupParams toBootstrapBackupParams(BootstrapBackupParams source);

  DrConfigWebhook toWebhook(GetWebhookResponse source);

  @AfterMapping
  default void mapDrConfigSpecExtras(@MappingTarget DrConfigSpec target, DrConfigGetResp source) {
    target.setType(DrConfigEnumMapper.INSTANCE.toDrConfigSpecType(source));
    if (target.getWebhooks() == null) {
      target.setWebhooks(List.of());
    }

    if (source.getType() == ConfigType.Db) {
      target.setTables(null);
    } else {
      target.setDbs(null);
    }
  }

  @AfterMapping
  default void mapDrConfigInfoExtras(@MappingTarget DrConfigInfo target, DrConfigGetResp source) {
    target.setPrimaryUniverseActive(source.isPrimaryUniverseActive());
    target.setDrReplicaUniverseActive(source.isDrReplicaUniverseActive());
  }
}
