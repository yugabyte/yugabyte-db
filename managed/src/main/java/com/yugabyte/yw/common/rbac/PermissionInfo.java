package com.yugabyte.yw.common.rbac;

import com.fasterxml.jackson.annotation.JsonProperty;
import java.util.List;
import lombok.AllArgsConstructor;
import lombok.Data;
import lombok.Getter;

@Data
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
    USE,
  }

  @JsonProperty("resource_type")
  public ResourceType resourceType;

  @JsonProperty("permission")
  public Permission permission;

  @JsonProperty("description")
  public String description;

  @JsonProperty("prerequisite_permissions")
  public List<PermissionInfoIdentifier> prerequisitePermissions;
}

@Data
class PermissionInfoIdentifier {
  @JsonProperty("resource_type")
  public PermissionInfo.ResourceType resourceType;

  @JsonProperty("permission")
  public PermissionInfo.Permission permission;
}
