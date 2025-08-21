package api.v2.mappers;

import api.v2.models.UniverseQueryLogsExport;
import com.yugabyte.yw.forms.QueryLogConfigParams;
import com.yugabyte.yw.forms.SoftwareUpgradeParams;
import org.mapstruct.Mapper;
import org.mapstruct.Mapping;
import org.mapstruct.MappingTarget;
import org.mapstruct.factory.Mappers;

@Mapper(config = CentralConfig.class)
public interface UniverseQueryLogsExportMapper {
  UniverseQueryLogsExportMapper INSTANCE = Mappers.getMapper(UniverseQueryLogsExportMapper.class);

  @Mapping(target = "upgradeOption", source = "source")
  @Mapping(target = "sleepAfterTServerRestartMillis", source = "sleepAfterTserverRestartMillis")
  QueryLogConfigParams copyToV1QueryLogConfigParams(
      UniverseQueryLogsExport source, @MappingTarget QueryLogConfigParams target);

  default SoftwareUpgradeParams.UpgradeOption mapUpgradeOption(UniverseQueryLogsExport source) {
    return source.getRollingUpgrade()
        ? SoftwareUpgradeParams.UpgradeOption.ROLLING_UPGRADE
        : SoftwareUpgradeParams.UpgradeOption.NON_ROLLING_UPGRADE;
  }
}
