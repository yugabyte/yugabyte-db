package api.v2.mappers;

import api.v2.models.UniverseSystemdEnableStart;
import com.yugabyte.yw.forms.SystemdUpgradeParams;
import org.mapstruct.Mapper;
import org.mapstruct.Mapping;
import org.mapstruct.MappingTarget;
import org.mapstruct.factory.Mappers;

@Mapper(config = CentralConfig.class)
public interface UniverseSystemdUpgradeMapper {
  UniverseSystemdUpgradeMapper INSTANCE = Mappers.getMapper(UniverseSystemdUpgradeMapper.class);

  @Mapping(target = "sleepAfterTServerRestartMillis", source = "sleepAfterTserverRestartMillis")
  public SystemdUpgradeParams copToV1SystemdUpgradeParams(
      UniverseSystemdEnableStart source, @MappingTarget SystemdUpgradeParams target);
}
