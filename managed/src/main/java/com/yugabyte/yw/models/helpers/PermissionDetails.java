package com.yugabyte.yw.models.helpers;

import com.yugabyte.yw.common.rbac.PermissionInfoIdentifier;
import java.util.Set;
import lombok.AllArgsConstructor;
import lombok.Data;
import lombok.NoArgsConstructor;

@Data
@AllArgsConstructor
@NoArgsConstructor
public class PermissionDetails {
  Set<PermissionInfoIdentifier> permissionList;
}
