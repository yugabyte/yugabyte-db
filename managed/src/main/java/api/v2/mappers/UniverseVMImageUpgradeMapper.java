package api.v2.mappers;

import api.v2.models.UniverseVMImageUpgradeSpec;
import api.v2.models.VMImageClusterSpec;
import com.yugabyte.yw.forms.VMImageUpgradeParams;
import org.mapstruct.Mapper;
import org.mapstruct.Mapping;
import org.mapstruct.MappingTarget;
import org.mapstruct.factory.Mappers;

@Mapper(config = CentralConfig.class)
public interface UniverseVMImageUpgradeMapper {
  UniverseVMImageUpgradeMapper INSTANCE = Mappers.getMapper(UniverseVMImageUpgradeMapper.class);

  @Mapping(source = "forceUpgrade", target = "forceVMImageUpgrade")
  @Mapping(target = "sleepAfterTServerRestartMillis", source = "sleepAfterTserverRestartMillis")
  @Mapping(target = "upgradeOption", constant = "ROLLING_UPGRADE")
  VMImageUpgradeParams copyToV1VMImageUpgradeParams(
      UniverseVMImageUpgradeSpec source, @MappingTarget VMImageUpgradeParams target);

  VMImageUpgradeParams.ImageBundleUpgradeInfo toImageBundleInfo(
      VMImageClusterSpec vMImageClusterSpec);
}
