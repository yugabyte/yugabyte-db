// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.common.rbac;

import com.fasterxml.jackson.annotation.JsonProperty;
import com.yugabyte.yw.models.rbac.ResourceGroup;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.UUID;
import lombok.AllArgsConstructor;
import lombok.Getter;
import lombok.NoArgsConstructor;
import lombok.Setter;
import lombok.ToString;

@ApiModel(description = "Role and resource group definition.")
@Getter
@Setter
@AllArgsConstructor
@NoArgsConstructor
@ToString
public class RoleResourceDefinition {

  @ApiModelProperty(value = "UUID of the role to attach resource group to.", required = true)
  @JsonProperty("roleUUID")
  private UUID roleUUID;

  @ApiModelProperty(value = "Resource group definition for the role.", required = true)
  @JsonProperty("resourceGroup")
  private ResourceGroup resourceGroup;
}
