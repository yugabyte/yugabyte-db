// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.controllers.handlers;

import com.google.common.annotations.VisibleForTesting;
import com.google.inject.Inject;
import com.google.inject.Provider;
import com.google.inject.Singleton;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.RedactingService;
import com.yugabyte.yw.common.alerts.AlertConfigurationService;
import com.yugabyte.yw.common.alerts.AlertDestinationService;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.pa.PACollectorSync;
import com.yugabyte.yw.common.password.PasswordPolicyService;
import com.yugabyte.yw.common.rbac.RoleBindingUtil;
import com.yugabyte.yw.controllers.TokenAuthenticator;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.rbac.ResourceGroup;
import com.yugabyte.yw.models.rbac.Role;
import com.yugabyte.yw.models.rbac.RoleBinding;
import com.yugabyte.yw.models.rbac.RoleBinding.RoleBindingType;
import db.migration.default_.common.R__Sync_System_Roles;
import io.ebean.DB;
import io.ebean.annotation.Transactional;
import java.util.Optional;
import lombok.extern.slf4j.Slf4j;
import play.mvc.Http;

/** Creates a customer together with its primary user. Shared by the v1 and v2 register APIs. */
@Slf4j
@Singleton
public class CustomerRegistrationHandler {

  // Key of the advisory lock that serializes first registrations.
  @VisibleForTesting public static final long REGISTRATION_LOCK_KEY = 0x5942415265676973L;

  private final PasswordPolicyService passwordPolicyService;
  private final AlertDestinationService alertDestinationService;
  private final AlertConfigurationService alertConfigurationService;
  private final RoleBindingUtil roleBindingUtil;
  private final RuntimeConfGetter confGetter;
  private final PACollectorSync paCollectorSync;
  private final Config config;
  // TokenAuthenticator caches token lookups for 10 minutes, so this singleton takes a fresh one per
  // call, as Play does for its actions.
  private final Provider<TokenAuthenticator> tokenAuthenticatorProvider;

  @Inject
  public CustomerRegistrationHandler(
      PasswordPolicyService passwordPolicyService,
      AlertDestinationService alertDestinationService,
      AlertConfigurationService alertConfigurationService,
      RoleBindingUtil roleBindingUtil,
      RuntimeConfGetter confGetter,
      PACollectorSync paCollectorSync,
      Config config,
      Provider<TokenAuthenticator> tokenAuthenticatorProvider) {
    this.passwordPolicyService = passwordPolicyService;
    this.alertDestinationService = alertDestinationService;
    this.alertConfigurationService = alertConfigurationService;
    this.roleBindingUtil = roleBindingUtil;
    this.confGetter = confGetter;
    this.paCollectorSync = paCollectorSync;
    this.config = config;
    this.tokenAuthenticatorProvider = tokenAuthenticatorProvider;
  }

  /**
   * Returns the number of customers for a registration to decide on. Without a lock, two concurrent
   * first registrations both count zero customers and both create a SuperAdmin, so while there are
   * no customers this takes a lock held until the caller's transaction ends, and counts again. The
   * caller must create the customer in that same transaction.
   *
   * <p>Once a customer exists no registration can become the first one, so none waits on the lock.
   * A waiter holds a pooled DB connection, and concurrent registrations on a populated instance
   * would otherwise exhaust the pool.
   */
  public int countCustomersForRegistration() {
    if (DB.currentTransaction() == null) {
      throw new IllegalStateException("Customer registration must run in a transaction");
    }
    int count = Customer.find.query().findCount();
    if (count > 0) {
      return count;
    }
    DB.sqlQuery("SELECT pg_advisory_xact_lock(:key)")
        .setParameter("key", REGISTRATION_LOCK_KEY)
        .findOne();
    return Customer.find.query().findCount();
  }

  /**
   * Why a request may not register a customer. The v1 and v2 APIs map these to their own errors.
   */
  public enum Refusal {
    SINGLE_TENANT,
    SSO_ENABLED,
    NOT_AUTHENTICATED,
    NOT_SUPER_ADMIN
  }

  /**
   * Checks whether the request may register a customer when {@code customerCount} customers exist.
   * The first customer can always be registered. Further ones only on a multi-tenant instance
   * without SSO, and only by a SuperAdmin, who is then set as the request's user.
   */
  public Optional<Refusal> checkAllowed(Http.Request request, int customerCount) {
    if (customerCount == 0) {
      return Optional.empty();
    }
    if (!config.getBoolean("yb.multiTenant")) {
      return Optional.of(Refusal.SINGLE_TENANT);
    }
    if (confGetter.getGlobalConf(GlobalConfKeys.useOauth)) {
      return Optional.of(Refusal.SSO_ENABLED);
    }
    TokenAuthenticator tokenAuthenticator = tokenAuthenticatorProvider.get();
    if (tokenAuthenticator.superAdminAuthentication(request)) {
      return Optional.empty();
    }
    return Optional.of(
        tokenAuthenticator.getCurrentAuthenticatedUser(request) == null
            ? Refusal.NOT_AUTHENTICATED
            : Refusal.NOT_SUPER_ADMIN);
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
