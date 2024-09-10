// Copyright (c) YugaByte, Inc.

package api.v2.mappers;

import api.v2.models.GflagMetadata;
import com.yugabyte.yw.forms.ybc.YbcGflags.YbcGflagsMetadata;
import java.util.Collection;
import java.util.List;
import org.mapstruct.Mapper;
import org.mapstruct.Mapping;
import org.mapstruct.factory.Mappers;

@Mapper(config = CentralConfig.class)
public interface GflagsMetadataMapper {
  GflagsMetadataMapper INSTANCE = Mappers.getMapper(GflagsMetadataMapper.class);

  @Mapping(source = "flagName", target = "name")
  @Mapping(source = "defaultValue", target = "_default")
  GflagMetadata toGflagMetadata(YbcGflagsMetadata metadata);

  List<GflagMetadata> toGflagMetadataList(Collection<YbcGflagsMetadata> metadataList);
}
