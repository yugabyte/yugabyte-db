// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.models.rbac;

import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.models.Users;
import io.swagger.annotations.ApiModelProperty;
import java.util.Arrays;
import java.util.HashSet;
import java.util.Set;
import java.util.UUID;
import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.EqualsAndHashCode;
import lombok.Getter;
import lombok.NoArgsConstructor;
import lombok.Setter;
import lombok.ToString;
import lombok.extern.slf4j.Slf4j;

@Getter
@Setter
@AllArgsConstructor
@NoArgsConstructor
@ToString
@Slf4j
@EqualsAndHashCode
public class ResourceGroup {

  @Builder
  @NoArgsConstructor
  @AllArgsConstructor
  @Getter
  @Setter
  @ToString
  @EqualsAndHashCode
  public static class ResourceDefinition {
    @ApiModelProperty(value = "Resource Type")
    private ResourceType resourceType;

    @ApiModelProperty(value = "Select all resources (including future resources)")
    @Builder.Default
    private boolean allowAll = false;

    @ApiModelProperty(value = "Set of resource uuids")
    @Builder.Default
    private Set<UUID> resourceUUIDSet = new HashSet<>();

    public ResourceDefinition clone() {
      return ResourceDefinition.builder()
          .resourceType(this.resourceType)
          .allowAll(this.allowAll)
          .resourceUUIDSet(new HashSet<>(this.resourceUUIDSet))
          .build();
    }
  }

  private Set<ResourceDefinition> resourceDefinitionSet = new HashSet<>();

  public static ResourceGroup getSystemDefaultResourceGroup(UUID customerUUID, Users user) {
    return getSystemDefaultResourceGroup(customerUUID, user.getUuid(), user.getRole());
  }

  public static ResourceGroup getSystemDefaultResourceGroup(
      UUID customerUUID, UUID userUUID, Users.Role usersRole) {
    ResourceGroup defaultResourceGroup = new ResourceGroup();
    ResourceDefinition resourceDefinition;
    switch (usersRole) {
      case ConnectOnly:
        // For connect only role, user should have access to only his user profile, nothing else.
        resourceDefinition =
            ResourceDefinition.builder()
                .resourceType(ResourceType.USER)
                .allowAll(false)
                .resourceUUIDSet(new HashSet<>(Arrays.asList(userUUID)))
                .build();
        defaultResourceGroup.resourceDefinitionSet.add(resourceDefinition);
        break;
      default:
        // For all other built-in roles, we can default to all resource types in the resource group.
        for (ResourceType resourceType : ResourceType.values()) {
          if (resourceType.equals(ResourceType.OTHER)) {
            resourceDefinition =
                ResourceDefinition.builder()
                    .resourceType(resourceType)
                    .allowAll(false)
                    .resourceUUIDSet(new HashSet<>(Arrays.asList(customerUUID)))
                    .build();
          } else {
            resourceDefinition =
                ResourceDefinition.builder().resourceType(resourceType).allowAll(true).build();
          }
          defaultResourceGroup.resourceDefinitionSet.add(resourceDefinition);
        }
        break;
    }
    return defaultResourceGroup;
  }

  /**
   * This method removes the resources in place from a resource group for efficiency. Works on a
   * best effort basis as we don't want to halt resource deletion for role binding cleanup. Need to
   * see if we want to keep it this way.
   *
   * @param resourceGroup
   * @param resourceType
   * @param resourceUUID
   * @return
   */
  public static void removeResource(
      ResourceGroup resourceGroup, ResourceType resourceType, UUID resourceUUID) {
    for (ResourceDefinition resourceDefinition : resourceGroup.getResourceDefinitionSet()) {
      if (resourceType.equals(resourceDefinition.getResourceType())) {
        try {
          resourceDefinition.getResourceUUIDSet().remove(resourceUUID);
        } catch (Exception e) {
          log.warn(
              "Failed to remove resource '{}' from resource UUID set '{}', continuing.",
              resourceUUID,
              resourceDefinition.getResourceUUIDSet());
        }
      }
    }
  }
}
