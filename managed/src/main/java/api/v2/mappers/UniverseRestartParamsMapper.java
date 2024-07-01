package api.v2.mappers;

import api.v2.models.UniverseRestart;
import com.yugabyte.yw.forms.RestartTaskParams;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import org.mapstruct.Mapper;
import org.mapstruct.Mapping;
import org.mapstruct.MappingTarget;
import org.mapstruct.control.DeepClone;
import org.mapstruct.factory.Mappers;

@Mapper(config = CentralConfig.class, mappingControl = DeepClone.class)
public interface UniverseRestartParamsMapper {
  UniverseRestartParamsMapper INSTANCE = Mappers.getMapper(UniverseRestartParamsMapper.class);

  @Mapping(target = "sleepAfterTServerRestartMillis", source = "sleepAfterTserverRestartMillis")
  RestartTaskParams copyToV1RestartTaskParams(
      UniverseRestart source, @MappingTarget RestartTaskParams target);

  @Mapping(target = "sleepAfterTServerRestartMillis", source = "sleepAfterTserverRestartMillis")
  UpgradeTaskParams copyToV1UpgradeTaskParams(
      UniverseRestart source, @MappingTarget UpgradeTaskParams target);
}
