// Copyright (c) YugabyteDB, Inc.

package api.v2.mappers;

import api.v2.models.SupportBundleSizeEstimateResponse;
import org.mapstruct.Mapper;
import org.mapstruct.factory.Mappers;

@Mapper(config = CentralConfig.class)
public interface SupportBundleSizeEstimateMapper {

  SupportBundleSizeEstimateMapper INSTANCE =
      Mappers.getMapper(SupportBundleSizeEstimateMapper.class);

  SupportBundleSizeEstimateResponse toApi(
      com.yugabyte.yw.forms.SupportBundleSizeEstimateResponse source);
}
