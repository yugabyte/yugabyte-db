// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.common.rbac;

import com.fasterxml.jackson.annotation.JsonProperty;
import com.yugabyte.yw.models.rbac.ResourceGroup;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.UUID;
import javax.annotation.Nullable;
import lombok.AllArgsConstructor;
import lombok.Getter;
import lombok.NoArgsConstructor;
import lombok.Setter;
import lombok.ToString;

@ApiModel(description = "Defines the association of Role to Resource Groups.")
@Getter
@Setter
@AllArgsConstructor
@NoArgsConstructor
@ToString
public class RoleResourceDefinition {

  @ApiModelProperty(value = "UUID of the role to attach resource group to.", required = true)
  @JsonProperty("roleUUID")
  private UUID roleUUID;

  @ApiModelProperty(
      value = "Resource group definition for the role. Only applicable for custom roles.",
      required = false)
  @JsonProperty("resourceGroup")
  @Nullable
  private ResourceGroup resourceGroup;
}
