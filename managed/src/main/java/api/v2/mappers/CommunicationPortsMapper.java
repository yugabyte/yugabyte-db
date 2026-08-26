// Copyright (c) YugabyteDB, Inc.

package api.v2.mappers;

import api.v2.models.CommunicationPortsSpec;
import com.yugabyte.yw.forms.UniverseTaskParams.CommunicationPorts;
import org.mapstruct.InheritInverseConfiguration;
import org.mapstruct.Mapper;
import org.mapstruct.Mapping;

@Mapper(config = CentralConfig.class)
public interface CommunicationPortsMapper {
  @Mapping(source = "ybControllerRpcPort", target = "ybControllerrRpcPort")
  CommunicationPorts toV1CommunicationPorts(CommunicationPortsSpec v2Ports);

  @InheritInverseConfiguration
  CommunicationPortsSpec toV2CommunicationPorts(CommunicationPorts v1Ports);
}
