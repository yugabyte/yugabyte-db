// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.forms.rbac;

import com.fasterxml.jackson.annotation.JsonProperty;
import com.yugabyte.yw.common.rbac.PermissionInfo;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Set;
import java.util.UUID;
import lombok.AllArgsConstructor;
import lombok.Getter;
import lombok.NoArgsConstructor;
import lombok.Setter;
import lombok.ToString;

@ApiModel(description = "Permissions that a user has on a resource.")
@Getter
@Setter
@ToString
@AllArgsConstructor
@NoArgsConstructor
public class ResourcePermissionData {

  @ApiModelProperty(value = "Type of the YBA resource.")
  @JsonProperty("resourceType")
  private ResourceType resourceType;

  @ApiModelProperty(value = "Resource UUID of the type.")
  @JsonProperty("resourceUUID")
  private UUID resourceUUID;

  @ApiModelProperty(value = "Set of actions allowed on the resource with type.")
  @JsonProperty("actions")
  private Set<PermissionInfo.Action> actions;
}
