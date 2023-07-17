package com.yugabyte.yw.models.rbac;

import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import io.swagger.annotations.ApiModelProperty;
import java.util.Set;
import java.util.UUID;
import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Getter;
import lombok.NoArgsConstructor;
import lombok.Setter;

@Getter
@Setter
@NoArgsConstructor
public class ResourceGroup {

  public ResourceGroup(Set<ResourceDefinition> resourceDefinitionSet) {
    this.resourceDefinitionSet = resourceDefinitionSet;
  }

  @Builder
  @NoArgsConstructor
  @AllArgsConstructor
  @Getter
  @Setter
  public static class ResourceDefinition {
    @ApiModelProperty(value = "Resource Type")
    private ResourceType resourceType;

    @ApiModelProperty(value = "Select all resources(including future resources)")
    private boolean allowAll;

    @ApiModelProperty(value = "Set of resource uuids")
    private Set<UUID> resourceUUIDSet = null;
  }

  private Set<ResourceDefinition> resourceDefinitionSet;
}
