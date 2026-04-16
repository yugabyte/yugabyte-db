package api.v2.mappers;

import api.v2.models.UniverseSoftwareUpgradeStart;
import com.yugabyte.yw.forms.SoftwareUpgradeParams;
import org.mapstruct.Mapper;
import org.mapstruct.Mapping;
import org.mapstruct.MappingTarget;
import org.mapstruct.factory.Mappers;

@Mapper(config = CentralConfig.class)
public interface UniverseSoftwareUpgradeStartMapper {
  UniverseSoftwareUpgradeStartMapper INSTANCE =
      Mappers.getMapper(UniverseSoftwareUpgradeStartMapper.class);

  @Mapping(target = "sleepAfterTServerRestartMillis", source = "sleepAfterTserverRestartMillis")
  @Mapping(target = "ybSoftwareVersion", source = "version")
  @Mapping(target = "upgradeOption", source = "source")
  public SoftwareUpgradeParams copyToV1SoftwareUpgradeParams(
      UniverseSoftwareUpgradeStart source, @MappingTarget SoftwareUpgradeParams target);

  default SoftwareUpgradeParams.UpgradeOption mapUpgradeOption(
      UniverseSoftwareUpgradeStart source) {
    return source.getRollingUpgrade()
        ? SoftwareUpgradeParams.UpgradeOption.ROLLING_UPGRADE
        : SoftwareUpgradeParams.UpgradeOption.NON_ROLLING_UPGRADE;
  }
}
