// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.common.rbac;

import com.fasterxml.jackson.annotation.JsonProperty;
import lombok.AllArgsConstructor;
import lombok.Data;
import lombok.NoArgsConstructor;

@Data
@AllArgsConstructor
@NoArgsConstructor
public class Permission {
  @JsonProperty("resourceType")
  public PermissionInfo.ResourceType resourceType;

  @JsonProperty("action")
  public PermissionInfo.Action action;
}
