// Copyright (c) Yugabyte, Inc.

package api.v2.mappers;

import api.v2.models.ResourceDefinitionSpec;
import api.v2.models.ResourceGroupSpec;
import com.yugabyte.yw.models.rbac.ResourceGroup;
import com.yugabyte.yw.models.rbac.ResourceGroup.ResourceDefinition;
import org.mapstruct.InheritInverseConfiguration;
import org.mapstruct.Mapper;
import org.mapstruct.Mapping;
import org.mapstruct.factory.Mappers;

@Mapper(config = CentralConfig.class)
public interface ResourceGroupMapper {
  ResourceGroupMapper INSTANCE = Mappers.getMapper(ResourceGroupMapper.class);

  ResourceGroupSpec toV2ResourceGroup(ResourceGroup v1ResourceGroup);

  ResourceGroup toV1ResourceGroup(ResourceGroupSpec v2ResourceGroup);

  @Mapping(target = "resourceUuidSet", source = "resourceUUIDSet")
  ResourceDefinitionSpec toV2ResourceDefinitionSpec(ResourceDefinition v1ResourceDefinition);

  @InheritInverseConfiguration
  ResourceDefinition toV1ResourceDefinitionSpec(ResourceDefinitionSpec v2ResourceDefinition);
}
