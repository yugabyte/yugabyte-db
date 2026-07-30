package api.v2.mappers;

import api.v2.models.UniverseResizeNodes;
import com.yugabyte.yw.forms.ResizeNodeParams;
import org.mapstruct.Mapper;
import org.mapstruct.Mapping;
import org.mapstruct.MappingTarget;
import org.mapstruct.factory.Mappers;

@Mapper(
    config = CentralConfig.class,
    uses = {ClusterMapper.class})
public interface UniverseResizeNodeParamsMapper {
  UniverseResizeNodeParamsMapper INSTANCE = Mappers.getMapper(UniverseResizeNodeParamsMapper.class);

  @Mapping(target = "sleepAfterTServerRestartMillis", source = "sleepAfterTserverRestartMillis")
  ResizeNodeParams copyToV1ResizeNodeParams(
      UniverseResizeNodes source, @MappingTarget ResizeNodeParams target);
}
