// Copyright (c) YugabyteDB, Inc.

package api.v2.mappers;

import api.v2.models.ImageBundle;
import org.mapstruct.Mapper;
import org.mapstruct.factory.Mappers;

@Mapper(
    config = CentralConfig.class,
    uses = {ImageBundleDetailsMapper.class, ImageBundleMetadataMapper.class})
public interface ImageBundleMapper {

  ImageBundleMapper INSTANCE = Mappers.getMapper(ImageBundleMapper.class);

  ImageBundle toApi(com.yugabyte.yw.models.ImageBundle source);
}
