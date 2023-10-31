// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.common.rbac.routes;

import com.fasterxml.jackson.annotation.JsonProperty;
import com.yugabyte.yw.common.rbac.Permission;
import java.util.List;
import lombok.AllArgsConstructor;
import lombok.Data;
import lombok.NoArgsConstructor;

@Data
@AllArgsConstructor
@NoArgsConstructor
public class RbacPermissionDefinition {
  @JsonProperty("operator")
  Operator operator;

  @JsonProperty("rbacPermissionList")
  List<Permission> rbacPermissionList;
}
