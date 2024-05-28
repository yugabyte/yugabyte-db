package api.v2.mappers;

import api.v2.models.CommunicationPortsSpec;
import com.yugabyte.yw.forms.UniverseTaskParams.CommunicationPorts;
import org.mapstruct.Mapper;
import org.mapstruct.Mapping;

@Mapper(config = CentralConfig.class)
public interface CommunicationPortsMapper {
  @Mapping(target = "ybControllerRpcPort", source = "ybControllerrRpcPort")
  CommunicationPortsSpec toV2CommunicationPorts(CommunicationPorts v1Ports);
}
