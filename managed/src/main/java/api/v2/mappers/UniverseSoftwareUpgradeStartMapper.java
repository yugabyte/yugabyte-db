package api.v2.mappers;

import api.v2.models.CanaryUpgradeConfigSpec;
import api.v2.models.SoftwareUpgradeAZStep;
import api.v2.models.UniverseSoftwareUpgradeStart;
import com.yugabyte.yw.forms.AZUpgradeStep;
import com.yugabyte.yw.forms.CanaryUpgradeConfig;
import com.yugabyte.yw.forms.SoftwareUpgradeParams;
import java.util.List;
import java.util.stream.Collectors;
import org.mapstruct.AfterMapping;
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
  @Mapping(target = "runOnlyPrechecks", source = "runOnlyPrechecks")
  @Mapping(target = "canaryUpgradeConfig", ignore = true)
  void copyToV1SoftwareUpgradeParams(
      UniverseSoftwareUpgradeStart source, @MappingTarget SoftwareUpgradeParams target);

  @AfterMapping
  default void mapCanaryUpgradeConfig(
      UniverseSoftwareUpgradeStart source, @MappingTarget SoftwareUpgradeParams target) {
    if (source.getCanaryUpgradeConfig() != null) {
      target.canaryUpgradeConfig = toV1CanaryUpgradeConfig(source.getCanaryUpgradeConfig());
    }
  }

  default SoftwareUpgradeParams.UpgradeOption mapUpgradeOption(
      UniverseSoftwareUpgradeStart source) {
    return source.getRollingUpgrade()
        ? SoftwareUpgradeParams.UpgradeOption.ROLLING_UPGRADE
        : SoftwareUpgradeParams.UpgradeOption.NON_ROLLING_UPGRADE;
  }

  default CanaryUpgradeConfig toV1CanaryUpgradeConfig(CanaryUpgradeConfigSpec spec) {
    if (spec == null) {
      return null;
    }
    CanaryUpgradeConfig config = new CanaryUpgradeConfig();
    config.pauseAfterMasters = Boolean.TRUE.equals(spec.getPauseAfterMasters());
    config.primaryClusterAZSteps = toV1AZUpgradeSteps(spec.getPrimaryClusterAzSteps());
    config.readReplicaClusterAZSteps = toV1AZUpgradeSteps(spec.getReadReplicaClusterAzSteps());
    return config;
  }

  default List<AZUpgradeStep> toV1AZUpgradeSteps(List<SoftwareUpgradeAZStep> steps) {
    if (steps == null || steps.isEmpty()) {
      return null;
    }
    return steps.stream().map(this::toV1AZUpgradeStep).collect(Collectors.toList());
  }

  default AZUpgradeStep toV1AZUpgradeStep(SoftwareUpgradeAZStep step) {
    if (step == null) {
      return null;
    }
    AZUpgradeStep v1 = new AZUpgradeStep();
    v1.azUUID = step.getAzUuid();
    v1.pauseAfterTserverUpgrade = Boolean.TRUE.equals(step.getPauseAfterTserverUpgrade());
    return v1;
  }
}
