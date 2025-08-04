// Copyright (c) YugaByte, Inc.

package api.v2.mappers;

import com.yugabyte.yw.cloud.UniverseResourceDetails;
import org.mapstruct.Mapper;
import org.mapstruct.Mapping;
import org.mapstruct.factory.Mappers;

@Mapper(config = CentralConfig.class)
public interface UniverseResourceDetailsMapper {
  UniverseResourceDetailsMapper INSTANCE = Mappers.getMapper(UniverseResourceDetailsMapper.class);

  @Mapping(target = "memSizeGb", source = "memSizeGB")
  @Mapping(target = "volumeSizeGb", source = "volumeSizeGB")
  api.v2.models.UniverseResourceDetails toV2UniverseResourceDetails(
      UniverseResourceDetails v1UniverseResourceDetails);

  @Mapping(target = "memSizeGB", source = "memSizeGb")
  @Mapping(target = "volumeSizeGB", source = "volumeSizeGb")
  UniverseResourceDetails toV1UniverseResourceDetails(
      api.v2.models.UniverseResourceDetails v2UniverseResourceDetails);
}
