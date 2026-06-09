// Copyright (c) YugabyteDB, Inc.

package api.v2.mappers;

import api.v2.models.ImageBundleInfo;
import org.mapstruct.Mapper;
import org.mapstruct.factory.Mappers;

@Mapper(config = CentralConfig.class)
public interface ImageBundleInfoMapper {

  ImageBundleInfoMapper INSTANCE = Mappers.getMapper(ImageBundleInfoMapper.class);

  ImageBundleInfo toApi(com.yugabyte.yw.models.ImageBundleDetails.BundleInfo source);
}
