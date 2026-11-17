// Copyright (c) YugabyteDB, Inc.
package api.v2.mappers;

import org.mapstruct.Mapper;
import org.mapstruct.factory.Mappers;

@Mapper(config = CentralConfig.class)
public interface RollMaxBatchSizeMapper {
  RollMaxBatchSizeMapper INSTANCE = Mappers.getMapper(RollMaxBatchSizeMapper.class);

  // Unset v2 sizes are skipped (CentralConfig ignores nulls), so the v1 defaults of 1 apply.
  com.yugabyte.yw.forms.RollMaxBatchSize toV1RollMaxBatchSize(
      api.v2.models.RollMaxBatchSize source);

  api.v2.models.RollMaxBatchSize toV2RollMaxBatchSize(
      com.yugabyte.yw.forms.RollMaxBatchSize source);
}
