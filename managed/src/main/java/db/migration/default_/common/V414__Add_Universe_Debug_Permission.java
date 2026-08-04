// Copyright (c) YugaByte, Inc.

package db.migration.default_.common;

import com.yugabyte.yw.models.migrations.V414.Action;
import com.yugabyte.yw.models.migrations.V414.Permission;
import com.yugabyte.yw.models.migrations.V414.PermissionDetails;
import com.yugabyte.yw.models.migrations.V414.ResourceType;
import com.yugabyte.yw.models.migrations.V414.Role;
import io.ebean.DB;
import java.util.Date;
import java.util.List;
import lombok.extern.slf4j.Slf4j;
import org.flywaydb.core.api.migration.BaseJavaMigration;
import org.flywaydb.core.api.migration.Context;
import org.springframework.util.CollectionUtils;

@Slf4j
public class V414__Add_Universe_Debug_Permission extends BaseJavaMigration {

  @Override
  public void migrate(Context context) {
    DB.execute(V414__Add_Universe_Debug_Permission::addUniverseDebugPermission);
  }

  public static void addUniverseDebugPermission() {
    // Check if any custom roles have UNIVERSE.UPDATE.
    List<Role> roles = Role.getCustomRoles();
    for (Role role : roles) {
      PermissionDetails permissionDetails = role.getPermissionDetails();
      if (permissionDetails != null
          && !CollectionUtils.isEmpty(permissionDetails.getPermissionList())) {
        boolean addDebugPermission = false;
        for (Permission permission : permissionDetails.getPermissionList()) {
          Permission universeDebugPermission = new Permission(ResourceType.UNIVERSE, Action.UPDATE);
          if (universeDebugPermission.equals(permission)) {
            addDebugPermission = true;
            break;
          }
        }

        // If any custom roles have UNIVERSE.UPDATE, add UNIVERSE.DEBUG permission by default.
        // It is a new prerequisite permission now.
        if (addDebugPermission) {
          log.info(
              "Updating custom role: {}, adding UNIVERSE.DEBUG permission.", role.getRoleUUID());
          Permission universeDebugPermission = new Permission(ResourceType.UNIVERSE, Action.DEBUG);
          permissionDetails.getPermissionList().add(universeDebugPermission);
          role.setPermissionDetails(permissionDetails);
          role.setUpdatedOn(new Date());
          role.save();
        }
      }
    }
  }
}
