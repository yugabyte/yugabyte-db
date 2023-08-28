package com.yugabyte.yw.common.rbac;

import com.fasterxml.jackson.annotation.JsonProperty;
import lombok.AllArgsConstructor;
import lombok.Data;
import lombok.NoArgsConstructor;

@Data
@AllArgsConstructor
@NoArgsConstructor
public class PermissionInfoIdentifier {
  @JsonProperty("resource_type")
  public PermissionInfo.ResourceType resourceType;

  @JsonProperty("permission")
  public PermissionInfo.Permission permission;
}
