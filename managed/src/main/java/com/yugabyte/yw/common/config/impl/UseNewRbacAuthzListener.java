/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.common.config.impl;

import com.yugabyte.yw.common.config.RuntimeConfigChangeListener;
import com.yugabyte.yw.common.rbac.RoleBindingUtil;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.rbac.ResourceGroup;
import com.yugabyte.yw.models.rbac.Role;
import com.yugabyte.yw.models.rbac.RoleBinding;
import com.yugabyte.yw.models.rbac.RoleBinding.RoleBindingType;
import db.migration.default_.common.R__Sync_System_Roles;
import io.ebean.annotation.Transactional;
import java.util.List;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Singleton
public class UseNewRbacAuthzListener implements RuntimeConfigChangeListener {

  private final RoleBindingUtil roleBindingUtil;

  @Inject
  public UseNewRbacAuthzListener(RoleBindingUtil roleBindingUtil) {
    this.roleBindingUtil = roleBindingUtil;
  }

  public String getKeyPath() {
    return "yb.rbac.use_new_authz";
  }

  @Override
  @Transactional()
  public void processGlobal() {
    // Ensure all the built-in roles are present for each customer.
    // This sync is required for the following case:
    // Start YBA, add new customers, then turn on "yb.rbac.use_new_authz" runtime flag.
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
        // If it is not empty, it implies this user has already been setup with a role binding by
        // the admin, so we should not add any more role bindings.
        List<RoleBinding> roleBindingsForUser = RoleBinding.getAll(user.getUuid());
        if (roleBindingsForUser.isEmpty()) {
          // Create a system default resource group for the user if a role binding with the role
          // doesn't exist.
          ResourceGroup resourceGroup =
              ResourceGroup.getSystemDefaultResourceGroup(customer.getUuid(), user);
          // Create a single role binding for the user.
          RoleBinding createdRoleBinding =
              roleBindingUtil.createRoleBinding(
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
    }
  }
}
