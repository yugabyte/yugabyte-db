// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.common.rbac.routes;

import com.fasterxml.jackson.annotation.JsonProperty;
import java.util.List;
import lombok.AllArgsConstructor;
import lombok.Data;
import lombok.NoArgsConstructor;

@Data
@AllArgsConstructor
@NoArgsConstructor
public class RbacPermissionDefinitionList {
  @JsonProperty("operator")
  Operator operator;

  @JsonProperty("rbacPermissionDefinitionList")
  List<RbacPermissionDefinition> rbacPermissionDefinitionList;
}
