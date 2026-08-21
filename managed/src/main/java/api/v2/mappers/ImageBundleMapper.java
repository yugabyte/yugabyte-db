// Copyright (c) YugabyteDB, Inc.

package api.v2.mappers;

import api.v2.models.ImageBundle;
import api.v2.models.ImageBundleInfo;
import api.v2.models.ImageBundleSpec;
import org.mapstruct.Mapper;
import org.mapstruct.Mapping;
import org.mapstruct.factory.Mappers;

@Mapper(
    config = CentralConfig.class,
    uses = {ImageBundleDetailsMapper.class, ImageBundleMetadataMapper.class})
public interface ImageBundleMapper {

  ImageBundleMapper INSTANCE = Mappers.getMapper(ImageBundleMapper.class);

  @Mapping(target = "spec", source = ".")
  @Mapping(target = "info", source = ".")
  ImageBundle toApi(com.yugabyte.yw.models.ImageBundle source);

  @Mapping(target = "details", source = "details")
  ImageBundleSpec toImageBundleSpec(com.yugabyte.yw.models.ImageBundle source);

  @Mapping(target = "metadata", source = "metadata")
  ImageBundleInfo toImageBundleInfo(com.yugabyte.yw.models.ImageBundle source);
}
