// Copyright (c) YugabyteDB, Inc.

package api.v2.mappers;

import api.v2.models.UniverseDetailSubset;
import org.mapstruct.Mapper;
import org.mapstruct.Mapping;
import org.mapstruct.factory.Mappers;

@Mapper(config = CentralConfig.class)
public interface UniverseDetailSubsetMapper {

  UniverseDetailSubsetMapper INSTANCE = Mappers.getMapper(UniverseDetailSubsetMapper.class);

  @Mapping(
      target = "creationDate",
      expression =
          "java(java.time.Instant.ofEpochMilli(source.getCreationDate()).atOffset(java.time.ZoneOffset.UTC))")
  UniverseDetailSubset toV2(com.yugabyte.yw.common.Util.UniverseDetailSubset source);
}
