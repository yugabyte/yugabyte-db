package api.v2.mappers;

import api.v2.models.UniverseThirdPartySoftwareUpgradeStart;
import com.yugabyte.yw.forms.ThirdpartySoftwareUpgradeParams;
import org.mapstruct.Mapper;
import org.mapstruct.Mapping;
import org.mapstruct.MappingTarget;
import org.mapstruct.factory.Mappers;

@Mapper
public interface UniverseThirdPartySoftwareUpgradeMapper {
  UniverseThirdPartySoftwareUpgradeMapper INSTANCE =
      Mappers.getMapper(UniverseThirdPartySoftwareUpgradeMapper.class);

  @Mapping(target = "sleepAfterTServerRestartMillis", source = "sleepAfterTserverRestartMillis")
  @Mapping(target = "upgradeOption", source = "source")
  public ThirdpartySoftwareUpgradeParams copyToV1ThirdpartySoftwareUpgradeParams(
      UniverseThirdPartySoftwareUpgradeStart source,
      @MappingTarget ThirdpartySoftwareUpgradeParams target);

  // Explicitly map the UpgradeOption to ROLLING_UPGRADE
  default ThirdpartySoftwareUpgradeParams.UpgradeOption mapUpgradeOption(
      UniverseThirdPartySoftwareUpgradeStart source) {
    return ThirdpartySoftwareUpgradeParams.UpgradeOption.ROLLING_UPGRADE;
  }
}
