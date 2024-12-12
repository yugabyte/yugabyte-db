package db.migration.default_.postgres;

import com.yugabyte.yw.models.migrations.V383.*;
import com.yugabyte.yw.models.migrations.V383.RoleBinding.RoleBindingType;
import db.migration.default_.common.R__Sync_System_Roles;
import java.util.List;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.flywaydb.core.api.migration.BaseJavaMigration;
import org.flywaydb.core.api.migration.Context;

@Slf4j
public class V383__Enable_Rbac extends BaseJavaMigration {
  @Override
  public void migrate(Context context) {
    log.debug("Starting migration V383_Enable_Rbac");
    addDefaultRoleBindings();
    log.debug("Ending migration V383_Enable_Rbac");
  }

  public void addDefaultRoleBindings() {
    // Ensure all the built-in roles are present for each customer.
    // This sync is required for the following case:
    // Start YBA, add new customers, then turn on "yb.rbac.use_new_authz" runtime
    // flag.
    R__Sync_System_Roles.syncSystemRoles();

    List<Customer> customerList = Customer.getAll();
    for (Customer customer : customerList) {
      // Go through each customer.
      List<Users> usersList = Users.getAll(customer.getUuid());
      for (Users user : usersList) {
        // Go through each user in customer.
        Users.Role usersRole = user.getRole();
        Role newRbacRole = Role.get(customer.getUuid(), usersRole.name());
        // Get all role bindings for user with any role.
        // If it is not empty, it implies this user has already been setup with a role
        // binding by
        // the admin, so we should not add any more role bindings.
        List<RoleBinding> roleBindingsForUser = RoleBinding.getAll(user.getUuid());
        if (roleBindingsForUser.isEmpty()) {
          // Create a system default resource group for the user if a role binding with
          // the role
          // doesn't exist.
          ResourceGroup resourceGroup =
              ResourceGroup.getSystemDefaultResourceGroup(
                  customer.getUuid(), user.getUuid(), user.getRole());
          // Create a single role binding for the user.
          RoleBinding createdRoleBinding =
              createRoleBinding(
                  user.getUuid(), newRbacRole.getRoleUUID(), RoleBindingType.System, resourceGroup);

          log.info(
              "Created system role binding for user '{}' (email '{}') of customer '{}', "
                  + "with role '{}' (name '{}'), and default role binding '{}'.",
              user.getUuid(),
              user.getEmail(),
              customer.getUuid(),
              newRbacRole.getRoleUUID(),
              newRbacRole.getName(),
              createdRoleBinding.toString());
        }
      }
      // Add role bindings for all group mappings.
      GroupMappingInfo.find
          .query()
          .where()
          .eq("customer_uuid", customer.getUuid())
          .findList()
          .forEach(group -> createSystemRoleBindingsForGroup(group));
    }
  }

  private static void createSystemRoleBindingsForGroup(GroupMappingInfo info) {
    Role role = Role.get(info.getCustomerUUID(), info.getRoleUUID());
    // Once RBAC is on, all members of a group will be assigned the ConnectOnly role
    // on login, so don't need group role binding for it.
    if (role.getName().equals("ConnectOnly")) {
      return;
    }
    ResourceGroup rg = new ResourceGroup();
    rg.getResourceDefinitionSet()
        .addAll(ResourceGroup.getResourceDefinitionsForNonConnectOnlyRoles(info.getCustomerUUID()));
    RoleBinding.create(info, RoleBindingType.System, role, rg);
  }

  private RoleBinding createRoleBinding(
      UUID userUUID, UUID roleUUID, RoleBindingType type, ResourceGroup resourceGroup) {
    Users user = Users.get(userUUID);
    Role role = Role.get(user.getCustomerUUID(), roleUUID);
    log.info(
        "Creating {} RoleBinding with user UUID '{}', role UUID {} on resource group {}.",
        type,
        userUUID,
        roleUUID,
        resourceGroup);
    return RoleBinding.create(user, type, role, resourceGroup);
  }
}
