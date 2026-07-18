package api.v2.mappers;

import api.v2.models.UniverseRollbackUpgradeReq;
import com.yugabyte.yw.forms.RollbackUpgradeParams;
import org.mapstruct.Mapper;
import org.mapstruct.Mapping;
import org.mapstruct.MappingTarget;
import org.mapstruct.factory.Mappers;

@Mapper(config = CentralConfig.class)
public interface UniverseRollbackUpgradeMapper {
  UniverseRollbackUpgradeMapper INSTANCE = Mappers.getMapper(UniverseRollbackUpgradeMapper.class);

  @Mapping(target = "sleepAfterTServerRestartMillis", source = "sleepAfterTserverRestartMillis")
  @Mapping(target = "upgradeOption", source = "source")
  public RollbackUpgradeParams copyToV1RollbackUpgradeParams(
      UniverseRollbackUpgradeReq source, @MappingTarget RollbackUpgradeParams target);

  default RollbackUpgradeParams.UpgradeOption mapUpgradeOption(UniverseRollbackUpgradeReq source) {
    return source.getRollingUpgrade()
        ? RollbackUpgradeParams.UpgradeOption.ROLLING_UPGRADE
        : RollbackUpgradeParams.UpgradeOption.NON_ROLLING_UPGRADE;
  }
}
