// Copyright (c) YugaByte, Inc.

package db.migration.default_.common;

import io.ebean.DB;
import io.ebean.SqlRow;
import java.util.Date;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.codec.digest.MurmurHash3;
import org.apache.commons.lang3.StringUtils;
import org.flywaydb.core.api.migration.BaseJavaMigration;
import org.flywaydb.core.api.migration.Context;

@Slf4j
public class R__Sync_System_Roles extends BaseJavaMigration {

  public void migrate(Context context) {
    DB.execute(R__Sync_System_Roles::syncSystemRoles);
  }

  public static void syncSystemRoles() {
    log.debug("Inside Migration R__Sync_System_Roles");
    String customersSql = "SELECT uuid from customer";
    List<UUID> customerUUIDList = DB.sqlQuery(customersSql).mapToScalar(UUID.class).findList();

    // Check that the built-in roles updated permissions list is synced for each customer.
    for (UUID customerUUID : customerUUIDList) {
      log.debug("Inside Migration R__Sync_System_Roles, customer UUID = {}.", customerUUID);
      Map<String, String> predefinedSystemRolesMap = getPredefinedSystemRolesMap();
      // For each of the defined built-in roles, verify that the DB also has the same permissions
      // list.
      for (String usersRole : predefinedSystemRolesMap.keySet()) {
        String checkRoleSql = "SELECT * from Role WHERE name = ? AND customer_uuid = ?";
        List<SqlRow> checkRoleOutput =
            DB.sqlQuery(checkRoleSql).setParameter(usersRole).setParameter(customerUUID).findList();
        java.sql.Timestamp currTimestamp = new java.sql.Timestamp(new Date().getTime());
        if (checkRoleOutput.size() == 0) {
          // Case 1: Probably the first time running this migration, or a new built-in role was
          // added. Create this new role in the DB.
          String insertRoleSql =
              "INSERT INTO Role (role_uuid, customer_uuid, name, created_on, updated_on, "
                  + "role_type, permission_details, description) "
                  + "VALUES (?, ?, ?, ?, ?, ?, ?::json_alias, ?)";
          DB.sqlUpdate(insertRoleSql)
              .setParameter(UUID.randomUUID())
              .setParameter(customerUUID)
              .setParameter(usersRole)
              .setParameter(currTimestamp)
              .setParameter(currTimestamp)
              .setParameter("System")
              .setParameter(predefinedSystemRolesMap.get(usersRole))
              .setParameter(getRoleDescription(usersRole))
              .execute();
          log.debug(
              "Inside Migration R__Sync_System_Roles, inserted new role = {}, permissions = {}.",
              usersRole,
              predefinedSystemRolesMap.get(usersRole));
        }
        if (checkRoleOutput.size() == 1) {
          // Case 2: Valid case when only one row exists for that built-in role in that customer.
          if (!predefinedSystemRolesMap
                  .get(usersRole)
                  .equals(checkRoleOutput.get(0).getString("permission_details"))
              || !getRoleDescription(usersRole)
                  .equals(checkRoleOutput.get(0).getString("description"))) {
            // Update the permissions list and description in the DB if it doesn't match.
            String updateRoleSql =
                "UPDATE Role "
                    + "set permission_details = ?::json_alias, updated_on = ?, description = ? "
                    + "WHERE name = ? AND customer_uuid = ?";
            DB.sqlUpdate(updateRoleSql)
                .setParameter(predefinedSystemRolesMap.get(usersRole))
                .setParameter(currTimestamp)
                .setParameter(getRoleDescription(usersRole))
                .setParameter(usersRole)
                .setParameter(customerUUID)
                .execute();
            log.debug(
                "Inside Migration R__Sync_System_Roles, updated existing role = {} with "
                    + "existing permission_details = {}, existing description = {} to "
                    + "new permission_details = {}, new description = {}.",
                usersRole,
                checkRoleOutput.get(0).getString("permission_details"),
                checkRoleOutput.get(0).getString("description"),
                predefinedSystemRolesMap.get(usersRole),
                getRoleDescription(usersRole));
          }
        }
      }
    }
  }

  public static String getRoleDescription(String usersRole) {
    String roleDescription = "";
    switch (usersRole) {
      case "ConnectOnly":
        roleDescription =
            "The ConnectOnly Role provides the basic connection permission for users. "
                + "It only allows users to establish a connection with no additional "
                + "explicit permissions granted.";
        break;
      case "ReadOnly":
        roleDescription =
            "The ReadOnly Role restricts users to view-only permissions. "
                + "Users with this role can view various resources but cannot perform any "
                + "modifications.";
        break;
      case "BackupAdmin":
        roleDescription =
            "The BackupAdmin Role is similar to the ReadOnly role, "
                + "but it also grants users permissions to perform backup and restore operations "
                + "on universes.";
        break;
      case "Admin":
        roleDescription =
            "The Admin Role is a versatile role that provides a comprehensive set of permissions. "
                + "It allows users to create, read, update, and delete various resources.";
        break;
      case "SuperAdmin":
        roleDescription =
            "The SuperAdmin Role encompasses all the privileges of the Admin Role and includes "
                + "additional capabilities. SuperAdmins have the authority to modify "
                + "high-availability settings and runtime flags for resources.";
        break;
      default:
        break;
    }
    return roleDescription;
  }

  public static Map<String, String> getPredefinedSystemRolesMap() {
    Map<String, String> predefinedSystemRolesMap = new HashMap<>();

    String connectOnlyPermissions =
        "{\"permissionList\":["
            + "{\"resourceType\":\"USER\",\"action\":\"READ\"},"
            + "{\"resourceType\":\"USER\",\"action\":\"UPDATE_PROFILE\"}"
            + "]}";
    predefinedSystemRolesMap.put(
        "ConnectOnly", StringUtils.deleteWhitespace(connectOnlyPermissions));

    String readOnlyPermissions =
        "{\"permissionList\":["
            + "{\"resourceType\":\"USER\",\"action\":\"READ\"},"
            + "{\"resourceType\":\"USER\",\"action\":\"UPDATE_PROFILE\"},"
            + "{\"resourceType\":\"ROLE\",\"action\":\"READ\"},"
            + "{\"resourceType\":\"UNIVERSE\",\"action\":\"READ\"},"
            + "{\"resourceType\":\"OTHER\",\"action\":\"READ\"}"
            + "]}";
    predefinedSystemRolesMap.put("ReadOnly", StringUtils.deleteWhitespace(readOnlyPermissions));

    String backupAdminPermissions =
        "{\"permissionList\":["
            + "{\"resourceType\":\"USER\",\"action\":\"READ\"},"
            + "{\"resourceType\":\"USER\",\"action\":\"UPDATE_PROFILE\"},"
            + "{\"resourceType\":\"ROLE\",\"action\":\"READ\"},"
            + "{\"resourceType\":\"UNIVERSE\",\"action\":\"READ\"},"
            + "{\"resourceType\":\"UNIVERSE\",\"action\":\"BACKUP_RESTORE\"},"
            + "{\"resourceType\":\"OTHER\",\"action\":\"READ\"}"
            + "]}";
    predefinedSystemRolesMap.put(
        "BackupAdmin", StringUtils.deleteWhitespace(backupAdminPermissions));

    String adminPermissions =
        "{\"permissionList\":["
            + "{\"resourceType\":\"USER\",\"action\":\"READ\"},"
            + "{\"resourceType\":\"USER\",\"action\":\"CREATE\"},"
            + "{\"resourceType\":\"USER\",\"action\":\"UPDATE_ROLE_BINDINGS\"},"
            + "{\"resourceType\":\"USER\",\"action\":\"UPDATE_PROFILE\"},"
            + "{\"resourceType\":\"USER\",\"action\":\"DELETE\"},"
            + "{\"resourceType\":\"ROLE\",\"action\":\"READ\"},"
            + "{\"resourceType\":\"ROLE\",\"action\":\"CREATE\"},"
            + "{\"resourceType\":\"ROLE\",\"action\":\"UPDATE\"},"
            + "{\"resourceType\":\"ROLE\",\"action\":\"DELETE\"},"
            + "{\"resourceType\":\"UNIVERSE\",\"action\":\"READ\"},"
            + "{\"resourceType\":\"UNIVERSE\",\"action\":\"BACKUP_RESTORE\"},"
            + "{\"resourceType\":\"UNIVERSE\",\"action\":\"CREATE\"},"
            + "{\"resourceType\":\"UNIVERSE\",\"action\":\"UPDATE\"},"
            + "{\"resourceType\":\"UNIVERSE\",\"action\":\"DELETE\"},"
            + "{\"resourceType\":\"UNIVERSE\",\"action\":\"PAUSE_RESUME\"},"
            + "{\"resourceType\":\"OTHER\",\"action\":\"READ\"},"
            + "{\"resourceType\":\"OTHER\",\"action\":\"CREATE\"},"
            + "{\"resourceType\":\"OTHER\",\"action\":\"UPDATE\"},"
            + "{\"resourceType\":\"OTHER\",\"action\":\"DELETE\"}"
            + "]}";
    predefinedSystemRolesMap.put("Admin", StringUtils.deleteWhitespace(adminPermissions));

    String superAdminPermissions =
        "{\"permissionList\":["
            + "{\"resourceType\":\"USER\",\"action\":\"READ\"},"
            + "{\"resourceType\":\"USER\",\"action\":\"CREATE\"},"
            + "{\"resourceType\":\"USER\",\"action\":\"UPDATE_ROLE_BINDINGS\"},"
            + "{\"resourceType\":\"USER\",\"action\":\"UPDATE_PROFILE\"},"
            + "{\"resourceType\":\"USER\",\"action\":\"DELETE\"},"
            + "{\"resourceType\":\"ROLE\",\"action\":\"READ\"},"
            + "{\"resourceType\":\"ROLE\",\"action\":\"CREATE\"},"
            + "{\"resourceType\":\"ROLE\",\"action\":\"UPDATE\"},"
            + "{\"resourceType\":\"ROLE\",\"action\":\"DELETE\"},"
            + "{\"resourceType\":\"UNIVERSE\",\"action\":\"READ\"},"
            + "{\"resourceType\":\"UNIVERSE\",\"action\":\"BACKUP_RESTORE\"},"
            + "{\"resourceType\":\"UNIVERSE\",\"action\":\"CREATE\"},"
            + "{\"resourceType\":\"UNIVERSE\",\"action\":\"UPDATE\"},"
            + "{\"resourceType\":\"UNIVERSE\",\"action\":\"DELETE\"},"
            + "{\"resourceType\":\"UNIVERSE\",\"action\":\"PAUSE_RESUME\"},"
            + "{\"resourceType\":\"OTHER\",\"action\":\"READ\"},"
            + "{\"resourceType\":\"OTHER\",\"action\":\"CREATE\"},"
            + "{\"resourceType\":\"OTHER\",\"action\":\"UPDATE\"},"
            + "{\"resourceType\":\"OTHER\",\"action\":\"DELETE\"},"
            + "{\"resourceType\":\"OTHER\",\"action\":\"SUPER_ADMIN_ACTIONS\"}"
            + "]}";
    predefinedSystemRolesMap.put("SuperAdmin", StringUtils.deleteWhitespace(superAdminPermissions));

    return predefinedSystemRolesMap;
  }

  @Override
  public Integer getChecksum() {
    // This migration will run everytime any new permissions are added to the above map or any of
    // the descriptions are modified for the system defined roles.
    return MurmurHash3.hash32(
        getPredefinedSystemRolesMap().toString()
            + getRoleDescription("ConnectOnly")
            + getRoleDescription("ReadOnly")
            + getRoleDescription("BackupAdmin")
            + getRoleDescription("Admin")
            + getRoleDescription("SuperAdmin"));
  }
}
