package com.yugabyte.yw.models.rbac;

import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import io.swagger.annotations.ApiModelProperty;
import java.util.HashSet;
import java.util.Set;
import java.util.UUID;
import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Getter;
import lombok.NoArgsConstructor;
import lombok.Setter;
import lombok.ToString;

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
  @ToString
  public static class ResourceDefinition {
    @ApiModelProperty(value = "Resource Type")
    private ResourceType resourceType;

    @ApiModelProperty(value = "Select all resources (including future resources)")
    @Builder.Default
    private boolean allowAll = false;

    @ApiModelProperty(value = "Set of resource uuids")
    @Builder.Default
    private Set<UUID> resourceUUIDSet = new HashSet<>();
  }

  private Set<ResourceDefinition> resourceDefinitionSet;
}
