package com.yugabyte.yw.common.rbac;

import com.fasterxml.jackson.annotation.JsonProperty;
import java.util.HashSet;
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
  }

  public enum Permission {
    CREATE,
    READ,
    UPDATE,
    DELETE,
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
