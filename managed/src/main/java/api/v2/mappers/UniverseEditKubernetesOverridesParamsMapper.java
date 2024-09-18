package api.v2.mappers;

import api.v2.models.UniverseEditKubernetesOverrides;
import com.yugabyte.yw.forms.KubernetesOverridesUpgradeParams;
import org.mapstruct.Mapper;
import org.mapstruct.Mapping;
import org.mapstruct.MappingTarget;
import org.mapstruct.factory.Mappers;

@Mapper(config = CentralConfig.class)
public interface UniverseEditKubernetesOverridesParamsMapper {
  UniverseEditKubernetesOverridesParamsMapper INSTANCE =
      Mappers.getMapper(UniverseEditKubernetesOverridesParamsMapper.class);

  @Mapping(source = "overrides", target = "universeOverrides")
  KubernetesOverridesUpgradeParams copyToV1KubernetesOverridesParams(
      UniverseEditKubernetesOverrides source,
      @MappingTarget KubernetesOverridesUpgradeParams target);
}
