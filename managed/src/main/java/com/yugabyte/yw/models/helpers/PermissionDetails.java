// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.models.helpers;

import com.yugabyte.yw.common.rbac.Permission;
import io.swagger.annotations.ApiModelProperty;
import java.util.Set;
import lombok.AllArgsConstructor;
import lombok.Data;
import lombok.NoArgsConstructor;

@Data
@AllArgsConstructor
@NoArgsConstructor
public class PermissionDetails {
  @ApiModelProperty(value = "Set of permissions")
  Set<Permission> permissionList;
}
