// Copyright (c) Yugabyte, Inc.

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
@AllArgsConstructor
@NoArgsConstructor
@ToString
public class ResourceGroup {

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

  private Set<ResourceDefinition> resourceDefinitionSet = new HashSet<>();

  public static ResourceGroup getSystemDefaultResourceGroup() {
    ResourceGroup defaultResourceGroup = new ResourceGroup();
    for (ResourceType resourceType : ResourceType.values()) {
      ResourceDefinition resourceDefinition =
          ResourceDefinition.builder().resourceType(resourceType).allowAll(true).build();
      defaultResourceGroup.resourceDefinitionSet.add(resourceDefinition);
    }
    return defaultResourceGroup;
  }
}
