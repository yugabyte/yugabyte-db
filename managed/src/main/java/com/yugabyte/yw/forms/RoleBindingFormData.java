// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonProperty;
import com.yugabyte.yw.common.rbac.RoleResourceDefinition;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.List;
import lombok.Getter;
import lombok.Setter;

@ApiModel(description = "Role Binding form metadata")
@Getter
@Setter
public class RoleBindingFormData {

  @ApiModelProperty(value = "List of roles and resource groups defined.", required = true)
  @JsonProperty("roleResourceDefinitions")
  private List<RoleResourceDefinition> roleResourceDefinitions;
}
