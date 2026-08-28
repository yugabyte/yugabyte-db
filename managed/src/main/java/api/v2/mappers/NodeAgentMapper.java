// Copyright (c) YugabyteDB, Inc.

package api.v2.mappers;

import api.v2.models.NodeAgentUpgradeSpec;
import com.yugabyte.yw.commissioner.tasks.UpgradeNodeAgent;
import org.mapstruct.Mapper;
import org.mapstruct.Mapping;
import org.mapstruct.factory.Mappers;

@Mapper(config = CentralConfig.class)
public interface NodeAgentMapper {
  NodeAgentMapper INSTANCE = Mappers.getMapper(NodeAgentMapper.class);

  // Field certificateUuid is resolved from certificateName in NodeAgentHandler.
  @Mapping(target = "universeUUID", ignore = true)
  @Mapping(target = "certificateUuid", ignore = true)
  UpgradeNodeAgent.Params toUpgradeNodeAgentParams(NodeAgentUpgradeSpec spec);
}
