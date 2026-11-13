package api.v2.mappers;

import api.v2.models.UniverseEditKubernetesOverrides;
import com.yugabyte.yw.forms.KubernetesOverridesUpgradeParams;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import org.mapstruct.Mapper;
import org.mapstruct.Mapping;
import org.mapstruct.MappingTarget;
import org.mapstruct.factory.Mappers;

@Mapper(config = CentralConfig.class, uses = RollMaxBatchSizeMapper.class)
public interface UniverseEditKubernetesOverridesParamsMapper {
  UniverseEditKubernetesOverridesParamsMapper INSTANCE =
      Mappers.getMapper(UniverseEditKubernetesOverridesParamsMapper.class);

  @Mapping(source = "overrides", target = "universeOverrides")
  @Mapping(target = "sleepAfterTServerRestartMillis", source = "sleepAfterTserverRestartMillis")
  @Mapping(target = "upgradeOption", source = "source")
  KubernetesOverridesUpgradeParams copyToV1KubernetesOverridesParams(
      UniverseEditKubernetesOverrides source,
      @MappingTarget KubernetesOverridesUpgradeParams target);

  default UpgradeTaskParams.UpgradeOption mapUpgradeOption(UniverseEditKubernetesOverrides source) {
    if (source.getRollingUpgrade() == null || Boolean.TRUE.equals(source.getRollingUpgrade())) {
      return UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE;
    }
    return UpgradeTaskParams.UpgradeOption.NON_ROLLING_UPGRADE;
  }
}
