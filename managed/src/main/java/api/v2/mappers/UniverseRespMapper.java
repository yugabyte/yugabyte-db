// Copyright (c) YugabyteDB, Inc.
package api.v2.mappers;

import api.v2.models.AllowedTasksOnFailure;
import api.v2.models.Universe;
import api.v2.models.UniverseInfo;
import api.v2.models.UniverseResourceDetails;
import org.mapstruct.Context;
import org.mapstruct.DecoratedWith;
import org.mapstruct.Mapper;
import org.mapstruct.Mapping;
import org.mapstruct.MappingTarget;
import org.mapstruct.factory.Mappers;

@DecoratedWith(UniverseRespDecorator.class)
@Mapper(
    config = CentralConfig.class,
    uses = {
      UniverseDefinitionTaskParamsMapper.class,
      CommunicationPortsMapper.class,
      ClusterMapper.class
    })
public interface UniverseRespMapper {
  UniverseRespMapper INSTANCE = Mappers.getMapper(UniverseRespMapper.class);

  @Mapping(target = "spec", source = "universeDetails.delegate")
  @Mapping(target = "info", source = "universeDetails.delegate")
  // The top-level properties of UniverseResp are mapped in UniverseRespDecorator.
  // A Universe object is passed so decorator can set creationDate from the Date object instead of
  // parsing a date string and assuming a specific locale.
  Universe toV2Universe(
      com.yugabyte.yw.forms.UniverseResp v1UniverseResp,
      @Context com.yugabyte.yw.models.Universe universe);

  @Mapping(target = "allowedTasksOnFailure", source = "allowedTasks")
  @Mapping(target = "creationDate", ignore = true)
  UniverseInfo fillV2UniverseInfoFromV1UniverseResp(
      com.yugabyte.yw.forms.UniverseResp v1UniverseResp, @MappingTarget UniverseInfo universeInfo);

  @Mapping(target = "taskTypes", source = "taskIds")
  AllowedTasksOnFailure toV2AllowedTasksOnFailure(
      com.yugabyte.yw.forms.AllowedUniverseTasksResp allowedTasks);

  @Mapping(target = "memSizeGb", source = "memSizeGB")
  @Mapping(target = "volumeSizeGb", source = "volumeSizeGB")
  UniverseResourceDetails toV2UniverseResourceDetails(
      com.yugabyte.yw.cloud.UniverseResourceDetails universeResourceDetails);
}
