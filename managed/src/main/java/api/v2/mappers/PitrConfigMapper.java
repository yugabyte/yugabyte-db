// Copyright (c) YugabyteDB, Inc.

package api.v2.mappers;

import api.v2.models.PitrConfig;
import api.v2.models.PitrConfigInfo;
import api.v2.models.PitrConfigSpec;
import org.mapstruct.Mapper;
import org.mapstruct.Mapping;
import org.mapstruct.factory.Mappers;

@Mapper(
    config = CentralConfig.class,
    uses = {DateTimeMapper.class, PitrConfigEnumMapper.class})
public interface PitrConfigMapper {

  PitrConfigMapper INSTANCE = Mappers.getMapper(PitrConfigMapper.class);

  @Mapping(target = "spec", source = ".")
  @Mapping(target = "info", source = ".")
  PitrConfig toPitrConfig(com.yugabyte.yw.models.PitrConfig source);

  PitrConfigSpec toPitrConfigSpec(com.yugabyte.yw.models.PitrConfig source);

  @Mapping(target = "customerUuid", source = "customerUUID")
  @Mapping(target = "createdForDr", expression = "java(source.isCreatedForDr())")
  @Mapping(target = "usedForXCluster", expression = "java(source.isUsedForXCluster())")
  @Mapping(target = "disabled", expression = "java(source.isDisabled())")
  PitrConfigInfo toPitrConfigInfo(com.yugabyte.yw.models.PitrConfig source);
}
