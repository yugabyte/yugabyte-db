// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.controllers.handlers;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.RedactingService;
import com.yugabyte.yw.common.alerts.AlertConfigurationService;
import com.yugabyte.yw.common.alerts.AlertDestinationService;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.pa.PACollectorSync;
import com.yugabyte.yw.common.password.PasswordPolicyService;
import com.yugabyte.yw.common.rbac.RoleBindingUtil;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.rbac.ResourceGroup;
import com.yugabyte.yw.models.rbac.Role;
import com.yugabyte.yw.models.rbac.RoleBinding;
import com.yugabyte.yw.models.rbac.RoleBinding.RoleBindingType;
import db.migration.default_.common.R__Sync_System_Roles;
import io.ebean.annotation.Transactional;
import lombok.extern.slf4j.Slf4j;

/** Creates a customer together with its primary user. Shared by the v1 and v2 register APIs. */
@Slf4j
@Singleton
public class CustomerRegistrationHandler {

  private final PasswordPolicyService passwordPolicyService;
  private final AlertDestinationService alertDestinationService;
  private final AlertConfigurationService alertConfigurationService;
  private final RoleBindingUtil roleBindingUtil;
  private final RuntimeConfGetter confGetter;
  private final PACollectorSync paCollectorSync;

  @Inject
  public CustomerRegistrationHandler(
      PasswordPolicyService passwordPolicyService,
      AlertDestinationService alertDestinationService,
      AlertConfigurationService alertConfigurationService,
      RoleBindingUtil roleBindingUtil,
      RuntimeConfGetter confGetter,
      PACollectorSync paCollectorSync) {
    this.passwordPolicyService = passwordPolicyService;
    this.alertDestinationService = alertDestinationService;
    this.alertConfigurationService = alertConfigurationService;
    this.roleBindingUtil = roleBindingUtil;
    this.confGetter = confGetter;
    this.paCollectorSync = paCollectorSync;
  }

  /**
   * Creates the customer and its primary user with the given role. Callers decide whether the
   * caller may register a customer and which role the user gets.
   *
   * @return the primary user; its customer is {@code user.getCustomerUUID()}
   */
  @Transactional
  public Users register(String code, String name, String email, String password, Users.Role role) {
    Customer cust = Customer.create(code, name);
    passwordPolicyService.checkPasswordPolicy(cust.getUuid(), password);

    alertDestinationService.createDefaultDestination(cust.getUuid());
    alertConfigurationService.createDefaultConfigs(cust);

    Users user = Users.createPrimary(email, password, role, cust.getUuid());

    // Sync all the built-in roles when a new customer is created.
    R__Sync_System_Roles.syncSystemRoles(cust.getUuid());

    if (confGetter.getGlobalConf(GlobalConfKeys.useNewRbacAuthz)) {
      Role newRbacRole = Role.get(cust.getUuid(), role.name());

      // Now add the role binding for the above user.
      ResourceGroup resourceGroup =
          ResourceGroup.getSystemDefaultResourceGroup(cust.getUuid(), user);
      // Create a single role binding for the user.
      RoleBinding createdRoleBinding =
          roleBindingUtil.createRoleBinding(
              user.getUuid(), newRbacRole.getRoleUUID(), RoleBindingType.System, resourceGroup);

      log.info(
          "Created new system role binding for user '{}' (email '{}') of new customer '{}', "
              + "with role '{}' (name '{}'), and default role binding '{}'.",
          user.getUuid(),
          RedactingService.SECRET_REPLACEMENT,
          cust.getUuid(),
          newRbacRole.getRoleUUID(),
          newRbacRole.getName(),
          createdRoleBinding.toString());
    }

    // Have to call it here, because customer only present inside the same transaction.
    paCollectorSync.initialize(cust);
    return user;
  }
}
