// Copyright (c) Yugabyte, Inc.

package api.v2.mappers;

import com.yugabyte.yw.models.rbac.ResourceGroup;
import com.yugabyte.yw.models.rbac.ResourceGroup.ResourceDefinition;
import org.mapstruct.InheritInverseConfiguration;
import org.mapstruct.Mapper;
import org.mapstruct.Mapping;
import org.mapstruct.factory.Mappers;

@Mapper(config = CentralConfig.class)
public interface ResourceGroupMapper {
  ResourceGroupMapper INSTANCE = Mappers.getMapper(ResourceGroupMapper.class);

  api.v2.models.ResourceGroup toV2ResourceGroup(ResourceGroup v1ResourceGroup);

  ResourceGroup toV1ResourceGroup(api.v2.models.ResourceGroup v2ResourceGroup);

  @Mapping(target = "resourceUuidSet", source = "resourceUUIDSet")
  api.v2.models.ResourceDefinition toV2ResourceDefinition(ResourceDefinition v1ResourceDefinition);

  @InheritInverseConfiguration
  ResourceDefinition toV1ResourceDefinition(api.v2.models.ResourceDefinition v2ResourceDefinition);
}
