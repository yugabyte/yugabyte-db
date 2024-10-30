// Copyright (c) Yugabyte, Inc.

package db.migration.default_.postgres;

import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.models.migrations.V365.GroupMappingInfo;
import com.yugabyte.yw.models.migrations.V365.GroupMappingInfo.GroupType;
import com.yugabyte.yw.models.migrations.V365.LdapDnToYbaRole;
import com.yugabyte.yw.models.migrations.V365.OidcGroupToYbaRoles;
import com.yugabyte.yw.models.migrations.V365.Role;
import com.yugabyte.yw.models.migrations.V365.RoleBinding;
import com.yugabyte.yw.models.migrations.V365.RoleBinding.RoleBindingType;
import com.yugabyte.yw.models.migrations.V365.RuntimeConfigEntry;
import com.yugabyte.yw.models.rbac.ResourceGroup;
import db.migration.default_.common.R__Sync_System_Roles;
import io.ebean.DB;
import java.sql.SQLException;
import lombok.extern.slf4j.Slf4j;
import org.flywaydb.core.api.migration.BaseJavaMigration;
import org.flywaydb.core.api.migration.Context;

// Migration to move all the existing LDAP and OIDC mappings to new group_mapping_info table.
@Slf4j
public class V365__Migrate_Group_Mappings extends BaseJavaMigration {

  @Override
  public void migrate(Context context) throws SQLException {
    DB.execute(V365__Migrate_Group_Mappings::migrateGroupMappings);
  }

  public static void migrateGroupMappings() {
    R__Sync_System_Roles.syncSystemRoles();
    boolean rbacOn = false;
    RuntimeConfigEntry config =
        RuntimeConfigEntry.find
            .query()
            .where()
            .eq("path", GlobalConfKeys.useNewRbacAuthz.getKey())
            .findOne();
    if (config != null && config.getValue().equalsIgnoreCase("true")) {
      rbacOn = true;
    }

    for (LdapDnToYbaRole group : LdapDnToYbaRole.find.all()) {
      log.info("Creating Group Mapping entry for LDAP group with DN: " + group.distinguishedName);
      GroupMappingInfo info =
          GroupMappingInfo.create(
              group.customerUUID,
              Role.get(group.customerUUID, group.ybaRole.name()).getRoleUUID(),
              group.distinguishedName,
              GroupType.LDAP);
      if (rbacOn) {
        log.info("Creating Role binding for LDAP group: " + info.getIdentifier());
        createSystemRoleBindingsForGroup(info);
      }
    }

    for (OidcGroupToYbaRoles group : OidcGroupToYbaRoles.find.all()) {
      log.info("Creating Group Mapping entry for OIDC group: " + group.getGroupName());
      GroupMappingInfo info =
          GroupMappingInfo.create(
              group.getCustomerUUID(), group.ybaRoles.get(0), group.getGroupName(), GroupType.OIDC);
      if (rbacOn) {
        log.info("Creating Role binding for OIDC group: " + group.getGroupName());
        createSystemRoleBindingsForGroup(info);
      }
    }
  }

  private static void createSystemRoleBindingsForGroup(GroupMappingInfo info) {
    Role role = Role.get(info.getCustomerUUID(), info.getRoleUUID());
    // Once RBAC is on, all members of a group will be assigned the ConnectOnly role
    // on login, so don't need group role binding for it.
    if (role.getName().equals("ConnectOnly")) {
      return;
    }
    log.info("Adding system role bindings for group: " + info.getIdentifier());
    ResourceGroup rg = new ResourceGroup();
    rg.getResourceDefinitionSet()
        .addAll(ResourceGroup.getResourceDefinitionsForNonConnectOnlyRoles(info.getCustomerUUID()));
    RoleBinding.create(info, RoleBindingType.System, role, rg);
  }
}
