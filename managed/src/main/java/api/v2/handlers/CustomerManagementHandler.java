// Copyright (c) YugabyteDB, Inc.

package api.v2.handlers;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.CONFLICT;
import static play.mvc.Http.Status.FORBIDDEN;
import static play.mvc.Http.Status.UNAUTHORIZED;

import api.v2.models.CustomerRegistration;
import api.v2.models.CustomerRegistrationSpec;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.user.UserService;
import com.yugabyte.yw.controllers.RequestContext;
import com.yugabyte.yw.controllers.TokenAuthenticator;
import com.yugabyte.yw.controllers.handlers.CustomerRegistrationHandler;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Users;
import io.ebean.DB;
import io.ebean.Transaction;
import org.apache.commons.validator.routines.EmailValidator;
import play.mvc.Http;

@Singleton
public class CustomerManagementHandler {

  private final Config config;
  private final RuntimeConfGetter confGetter;
  private final TokenAuthenticator tokenAuthenticator;
  private final CustomerRegistrationHandler customerRegistrationHandler;
  private final UserService userService;

  @Inject
  public CustomerManagementHandler(
      Config config,
      RuntimeConfGetter confGetter,
      TokenAuthenticator tokenAuthenticator,
      CustomerRegistrationHandler customerRegistrationHandler,
      UserService userService) {
    this.config = config;
    this.confGetter = confGetter;
    this.tokenAuthenticator = tokenAuthenticator;
    this.customerRegistrationHandler = customerRegistrationHandler;
    this.userService = userService;
  }

  public CustomerRegistration registerCustomer(
      Http.Request request, CustomerRegistrationSpec spec) {
    if (!EmailValidator.getInstance(true /* allowLocal */).isValid(spec.getEmail())) {
      throw new PlatformServiceException(BAD_REQUEST, "Invalid email: " + spec.getEmail());
    }

    // Explicit, because ebean.mf enhances @Transactional only under com.yugabyte.yw. The
    // registration lock, the checks and the inserts must share this transaction.
    try (Transaction txn = DB.beginTransaction()) {
      boolean firstCustomer = customerRegistrationHandler.countCustomersForRegistration() == 0;
      if (!firstCustomer) {
        if (!config.getBoolean("yb.multiTenant")) {
          throw new PlatformServiceException(
              CONFLICT, "A customer is already registered on this single-tenant YBA instance");
        }
        if (confGetter.getGlobalConf(GlobalConfKeys.useOauth)) {
          throw new PlatformServiceException(
              CONFLICT, "A customer is already registered on this SSO-enabled YBA instance");
        }
        if (!tokenAuthenticator.superAdminAuthentication(request)) {
          if (tokenAuthenticator.getCurrentAuthenticatedUser(request) == null) {
            throw new PlatformServiceException(UNAUTHORIZED, "Unable to authenticate user");
          }
          throw new PlatformServiceException(
              FORBIDDEN, "Only SuperAdmins can register additional customers");
        }
      }

      Users user =
          customerRegistrationHandler.register(
              spec.getCode(),
              spec.getName(),
              spec.getEmail(),
              spec.getPassword(),
              firstCustomer ? Users.Role.SuperAdmin : Users.Role.Admin);
      String apiToken = user.upsertApiToken();
      txn.commit();

      // The first registration has no authenticated user to audit; attribute it to the new user.
      Customer customer = Customer.get(user.getCustomerUUID());
      RequestContext.putIfAbsent(
          TokenAuthenticator.USER, userService.getUserWithFeatures(customer, user));

      return new CustomerRegistration()
          .customerUuid(customer.getUuid())
          .userUuid(user.getUuid())
          .apiToken(apiToken)
          .apiTokenVersion(user.getApiTokenVersion());
    }
  }
}
