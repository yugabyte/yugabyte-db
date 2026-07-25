package api.v2.mappers;

import api.v2.models.UniverseUpdateProxyConfig;
import com.yugabyte.yw.forms.ProxyConfigUpdateParams;
import org.mapstruct.Mapper;
import org.mapstruct.MappingTarget;
import org.mapstruct.factory.Mappers;

@Mapper(
    config = CentralConfig.class,
    uses = {ClusterMapper.class})
public interface UniverseUpdateProxyConfigParamsMapper {
  UniverseUpdateProxyConfigParamsMapper INSTANCE =
      Mappers.getMapper(UniverseUpdateProxyConfigParamsMapper.class);

  ProxyConfigUpdateParams copyToV1ProxyConfigUpdateParams(
      UniverseUpdateProxyConfig source, @MappingTarget ProxyConfigUpdateParams target);
}
