// Copyright (c) YugabyteDB, Inc.

package api.v2.mappers;

import api.v2.models.ImageBundleRegionInfo;
import org.mapstruct.Mapper;
import org.mapstruct.factory.Mappers;

@Mapper(config = CentralConfig.class)
public interface ImageBundleRegionInfoMapper {

  ImageBundleRegionInfoMapper INSTANCE = Mappers.getMapper(ImageBundleRegionInfoMapper.class);

  ImageBundleRegionInfo toApi(com.yugabyte.yw.models.ImageBundleDetails.BundleInfo source);
}
