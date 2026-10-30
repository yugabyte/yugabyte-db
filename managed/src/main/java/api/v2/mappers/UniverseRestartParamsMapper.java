package api.v2.mappers;

import api.v2.models.UniverseRestart;
import com.yugabyte.yw.forms.RestartTaskParams;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import org.mapstruct.Mapper;
import org.mapstruct.Mapping;
import org.mapstruct.MappingTarget;
import org.mapstruct.factory.Mappers;

@Mapper(config = CentralConfig.class)
public interface UniverseRestartParamsMapper {
  UniverseRestartParamsMapper INSTANCE = Mappers.getMapper(UniverseRestartParamsMapper.class);

  @Mapping(target = "sleepAfterTServerRestartMillis", source = "sleepAfterTserverRestartMillis")
  @Mapping(target = "upgradeOption", source = "source")
  @Mapping(target = "rollMaxBatchSize", source = "rollMaxBatchSize")
  RestartTaskParams copyToV1RestartTaskParams(
      UniverseRestart source, @MappingTarget RestartTaskParams target);

  @Mapping(target = "sleepAfterTServerRestartMillis", source = "sleepAfterTserverRestartMillis")
  @Mapping(target = "upgradeOption", source = "source")
  @Mapping(target = "rollMaxBatchSize", source = "rollMaxBatchSize")
  UpgradeTaskParams copyToV1UpgradeTaskParams(
      UniverseRestart source, @MappingTarget UpgradeTaskParams target);

  default UpgradeTaskParams.UpgradeOption mapUpgradeOption(UniverseRestart source) {
    if (source.getRollingRestart() == null || Boolean.TRUE.equals(source.getRollingRestart())) {
      return UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE;
    }
    return UpgradeTaskParams.UpgradeOption.NON_ROLLING_UPGRADE;
  }

  default com.yugabyte.yw.forms.RollMaxBatchSize mapRollMaxBatchSize(
      api.v2.models.RollMaxBatchSize source) {
    if (source == null) {
      return null;
    }
    com.yugabyte.yw.forms.RollMaxBatchSize target = new com.yugabyte.yw.forms.RollMaxBatchSize();
    if (source.getPrimaryBatchSize() != null) {
      target.setPrimaryBatchSize(source.getPrimaryBatchSize().intValue());
    }
    if (source.getReadReplicaBatchSize() != null) {
      target.setReadReplicaBatchSize(source.getReadReplicaBatchSize().intValue());
    }
    return target;
  }
}
