// Copyright (c) YugabyteDB, Inc.

package api.v2.mappers;

import api.v2.models.ImageBundleMetadata;
import org.mapstruct.Mapper;
import org.mapstruct.Mapping;
import org.mapstruct.factory.Mappers;

@Mapper(config = CentralConfig.class)
public interface ImageBundleMetadataMapper {

  ImageBundleMetadataMapper INSTANCE = Mappers.getMapper(ImageBundleMetadataMapper.class);

  @Mapping(
      target = "type",
      expression =
          "java(source.getType() == null ? null :"
              + " api.v2.models.ImageBundleMetadata.TypeEnum.fromValue(source.getType().name()))")
  ImageBundleMetadata toApi(com.yugabyte.yw.models.ImageBundle.Metadata source);
}
