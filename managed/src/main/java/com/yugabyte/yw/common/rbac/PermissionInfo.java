// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.common.rbac;

import com.fasterxml.jackson.annotation.JsonProperty;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.rbac.Role;
import java.util.HashSet;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.AllArgsConstructor;
import lombok.Data;
import lombok.Getter;
import lombok.NoArgsConstructor;

@Data
@AllArgsConstructor
@NoArgsConstructor
public class PermissionInfo {

  @AllArgsConstructor
  public enum ResourceType {
    UNIVERSE("rbac/available_resource_permissions/universeResourcePermissions.json"),
    ROLE("rbac/available_resource_permissions/roleResourcePermissions.json"),
    USER("rbac/available_resource_permissions/userResourcePermissions.json"),
    OTHER("rbac/available_resource_permissions/otherResourcePermissions.json");

    @Getter public String permissionFilePath;

    public boolean isValidResource(UUID customerUUID, UUID resourceUUID) {
      try {
        Customer customer = Customer.getOrBadRequest(customerUUID);
        switch (this) {
          case UNIVERSE:
            Universe.getOrBadRequest(resourceUUID, customer);
            break;
          case ROLE:
            Role.getOrBadRequest(customerUUID, resourceUUID);
            break;
          case USER:
            Users.getOrBadRequest(customerUUID, customerUUID);
            break;
          case OTHER:
            // Nothing to check explicitly for OTHER resources.
            break;
          default:
            break;
        }
        return true;
      } catch (Exception e) {
        return false;
      }
    }

    public Set<UUID> getAllResourcesUUID(UUID customerUUID) {
      Customer customer = Customer.getOrBadRequest(customerUUID);
      Set<UUID> allResourcesUUID = new HashSet<>();
      switch (this) {
        case UNIVERSE:
          allResourcesUUID = Universe.getAllUUIDs(customer);
          break;
        case ROLE:
          allResourcesUUID =
              Role.getAll(customerUUID).stream().map(Role::getRoleUUID).collect(Collectors.toSet());
          break;
        case USER:
          allResourcesUUID =
              Users.getAll(customerUUID).stream().map(Users::getUuid).collect(Collectors.toSet());
          break;
        case OTHER:
          // Nothing to check explicitly for OTHER resources.
          break;
        default:
          break;
      }
      return allResourcesUUID;
    }
  }

  public enum Action {
    CREATE,
    READ,
    UPDATE,
    DELETE,
    PAUSE_RESUME,
    BACKUP_RESTORE,
    UPDATE_ROLE_BINDINGS,
    UPDATE_PROFILE,
    SUPER_ADMIN_ACTIONS,
    XCLUSTER
  }

  @JsonProperty("resourceType")
  public ResourceType resourceType;

  @JsonProperty("action")
  public Action action;

  // Human readable name for the permission. Used for UI.
  @JsonProperty("name")
  public String name;

  @JsonProperty("description")
  public String description;

  // Some permissions like UNIVERSE.CREATE are not applicable on particular resources, but are a
  // generic permission. In such cases, "permissionValidOnResource" will be false.
  @JsonProperty("permissionValidOnResource")
  public boolean permissionValidOnResource;

  @JsonProperty("prerequisitePermissions")
  public HashSet<Permission> prerequisitePermissions;
}
