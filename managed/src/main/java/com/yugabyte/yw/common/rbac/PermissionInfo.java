package com.yugabyte.yw.common.rbac;

import com.fasterxml.jackson.annotation.JsonProperty;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import java.util.HashSet;
import java.util.UUID;
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
    UNIVERSE("rbac/universe_resource_permissions.json"),

    DEFAULT("rbac/default_resource_permissions.json");

    @Getter public String permissionFilePath;

    public boolean isValidResource(UUID customerUUID, UUID resourceUUID) {
      try {
        Customer customer = Customer.getOrBadRequest(customerUUID);
        switch (this) {
          case UNIVERSE:
            Universe.getOrBadRequest(resourceUUID, customer);
            break;
          case DEFAULT:
            // Nothing to check explicitly for DEFAULT resources.
            break;
          default:
            break;
        }
        return true;
      } catch (Exception e) {
        return false;
      }
    }
  }

  public enum Permission {
    CREATE,
    READ,
    UPDATE,
    DELETE,
    PAUSE_RESUME
  }

  @JsonProperty("resource_type")
  public ResourceType resourceType;

  @JsonProperty("permission")
  public Permission permission;

  @JsonProperty("description")
  public String description;

  @JsonProperty("prerequisite_permissions")
  public HashSet<PermissionInfoIdentifier> prerequisitePermissions;
}
