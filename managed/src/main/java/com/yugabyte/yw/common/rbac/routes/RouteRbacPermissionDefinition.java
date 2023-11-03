// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.common.rbac.routes;

import com.fasterxml.jackson.annotation.JsonProperty;
import lombok.AllArgsConstructor;
import lombok.Data;
import lombok.NoArgsConstructor;

@Data
@AllArgsConstructor
@NoArgsConstructor
public class RouteRbacPermissionDefinition {

  @JsonProperty("requestType")
  String requestType;

  @JsonProperty("endpoint")
  String endpoint;

  @JsonProperty("rbacPermissionDefinitions")
  RbacPermissionDefinitionList rbacPermissionDefinitions;
}
