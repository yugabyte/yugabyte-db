// Copyright (c) YugabyteDB, Inc.

package api.v2.mappers;

import api.v2.models.ImageBundleDetails;
import org.mapstruct.Mapper;
import org.mapstruct.Mapping;
import org.mapstruct.factory.Mappers;

@Mapper(config = CentralConfig.class, uses = ImageBundleInfoMapper.class)
public interface ImageBundleDetailsMapper {

  ImageBundleDetailsMapper INSTANCE = Mappers.getMapper(ImageBundleDetailsMapper.class);

  @Mapping(
      target = "arch",
      expression =
          "java(source.getArch() == null ? null :"
              + " api.v2.models.ImageBundleDetails.ArchEnum.fromValue(source.getArch().name()))")
  ImageBundleDetails toApi(com.yugabyte.yw.models.ImageBundleDetails source);
}
