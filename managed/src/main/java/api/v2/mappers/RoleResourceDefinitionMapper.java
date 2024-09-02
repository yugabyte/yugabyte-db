// Copyright (c) Yugabyte, Inc.

package api.v2.mappers;

import com.yugabyte.yw.common.rbac.RoleResourceDefinition;
import java.util.List;
import org.mapstruct.InheritInverseConfiguration;
import org.mapstruct.Mapper;
import org.mapstruct.Mapping;
import org.mapstruct.factory.Mappers;

@Mapper(
    config = CentralConfig.class,
    uses = {ResourceGroupMapper.class})
public interface RoleResourceDefinitionMapper {
  RoleResourceDefinitionMapper INSTANCE = Mappers.getMapper(RoleResourceDefinitionMapper.class);

  @Mapping(target = "roleUuid", source = "roleUUID")
  api.v2.models.RoleResourceDefinition toV2RoleResourceDefinition(
      RoleResourceDefinition v1RoleResourceDefinition);

  List<api.v2.models.RoleResourceDefinition> toV2RoleResourceDefinitionList(
      List<RoleResourceDefinition> v1RoleResourceDefinition);

  @InheritInverseConfiguration
  RoleResourceDefinition toV1RoleResourceDefinition(
      api.v2.models.RoleResourceDefinition v2RoleResourceDefinition);

  List<RoleResourceDefinition> toV1RoleResourceDefinitionList(
      List<api.v2.models.RoleResourceDefinition> v2RoleResourceDefinition);
}
