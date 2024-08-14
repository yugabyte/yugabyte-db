// Copyright (c) YugaByte, Inc.
package api.v2.mappers;

import api.v2.models.AllowedTasksOnFailure;
import api.v2.models.Universe;
import api.v2.models.UniverseInfo;
import api.v2.models.UniverseResourceDetails;
import java.text.ParseException;
import java.text.SimpleDateFormat;
import java.time.ZoneOffset;
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
  // The top-level properties of UniverseResp are mapped in UniverseRespDecorator
  Universe toV2Universe(com.yugabyte.yw.forms.UniverseResp v1UniverseResp);

  // This method is invoked from UniverseRespDecorator.toV2UnverseResp
  @Mapping(target = "allowedTasksOnFailure", source = "allowedTasks")
  UniverseInfo fillV2UniverseInfoFromV1UniverseResp(
      com.yugabyte.yw.forms.UniverseResp v1UniverseResp, @MappingTarget UniverseInfo universeInfo);

  // below methods are used implicitly to generate above mapping
  default java.time.OffsetDateTime parseToOffsetDateTime(String datetime) throws ParseException {
    return new SimpleDateFormat("EEE MMM dd HH:mm:ss zzz yyyy")
        .parse(datetime)
        .toInstant()
        .atOffset(ZoneOffset.UTC);
  }

  @Mapping(target = "taskTypes", source = "taskIds")
  AllowedTasksOnFailure toV2AllowedTasksOnFailure(
      com.yugabyte.yw.forms.AllowedUniverseTasksResp allowedTasks);

  @Mapping(target = "memSizeGb", source = "memSizeGB")
  @Mapping(target = "volumeSizeGb", source = "volumeSizeGB")
  UniverseResourceDetails toV2UniverseResourceDetails(
      com.yugabyte.yw.cloud.UniverseResourceDetails universeResourceDetails);
}
