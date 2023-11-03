// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms.rbac;

import com.fasterxml.jackson.annotation.JsonProperty;
import com.yugabyte.yw.common.rbac.Permission;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Set;

@ApiModel(description = "Role form metadata")
public class RoleFormData {

  // This is used only when creating a role. Role name cannot be edited.
  @ApiModelProperty(value = "Name of the role to be created", required = true)
  @JsonProperty("name")
  public String name;

  @ApiModelProperty(value = "Description of the role to be created", required = true)
  @JsonProperty("description")
  public String description;

  @ApiModelProperty(value = "List of permissions given to the role", required = true)
  @JsonProperty("permissionList")
  public Set<Permission> permissionList;
}
