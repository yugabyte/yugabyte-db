// Copyright (c) YugaByte, Inc.

package db.migration.default_.common;

import com.yugabyte.yw.models.migrations.V363.Action;
import com.yugabyte.yw.models.migrations.V363.Permission;
import com.yugabyte.yw.models.migrations.V363.PermissionDetails;
import com.yugabyte.yw.models.migrations.V363.ResourceType;
import com.yugabyte.yw.models.migrations.V363.Role;
import io.ebean.DB;
import java.util.Arrays;
import java.util.Date;
import java.util.List;
import lombok.extern.slf4j.Slf4j;
import org.flywaydb.core.api.migration.BaseJavaMigration;
import org.flywaydb.core.api.migration.Context;
import org.springframework.util.CollectionUtils;

@Slf4j
public class V363__Add_XCluster_Role_Action extends BaseJavaMigration {

  @Override
  public void migrate(Context context) {
    DB.execute(V363__Add_XCluster_Role_Action::addXClusterRoleAction);
  }

  public static void addXClusterRoleAction() {
    // Add XCluster Role Action on custom roles.
    List<Role> roles = Role.getCustomRoles();
    for (Role role : roles) {
      PermissionDetails permissionDetails = role.getPermissionDetails();
      if (permissionDetails != null
          && !CollectionUtils.isEmpty(permissionDetails.getPermissionList())) {
        boolean addXClusterPermission = false;
        for (Permission permission : permissionDetails.getPermissionList()) {
          if (permission.getAction() != null
              && permission.getAction().equals(Action.BACKUP_RESTORE)) {
            addXClusterPermission = true;
            break;
          } else if (permission.getAction() != null
              && Arrays.asList(Action.CREATE, Action.UPDATE, Action.DELETE)
                  .contains(permission.getAction())
              && permission.getResourceType() != null
              && permission.getResourceType().equals(ResourceType.OTHER)) {
            addXClusterPermission = true;
            break;
          }
        }

        if (addXClusterPermission) {
          log.info("updating role: {} with xCluster permission", role.getRoleUUID());
          Permission xClusterPermission = new Permission(ResourceType.UNIVERSE, Action.XCLUSTER);
          permissionDetails.getPermissionList().add(xClusterPermission);
          boolean containsUniverseReadPermission =
              permissionDetails.getPermissionList().stream()
                  .anyMatch(
                      p ->
                          p.getResourceType().equals(ResourceType.UNIVERSE)
                              && p.getAction().equals(Action.READ));
          if (!containsUniverseReadPermission) {
            Permission universeReadPermission = new Permission(ResourceType.UNIVERSE, Action.READ);
            permissionDetails.getPermissionList().add(universeReadPermission);
          }
          role.setPermissionDetails(permissionDetails);
          role.setUpdatedOn(new Date());
          role.save();
        }
      }
    }
  }
}
