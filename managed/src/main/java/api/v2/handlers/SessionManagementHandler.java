// Copyright (c) YugabyteDB, Inc.

package api.v2.handlers;

import api.v2.models.LoginSpec;
import api.v2.models.Session;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.user.UserService;
import com.yugabyte.yw.controllers.LoginHandler;
import com.yugabyte.yw.controllers.RequestContext;
import com.yugabyte.yw.controllers.TokenAuthenticator;
import com.yugabyte.yw.forms.CustomerLoginFormData;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Users;

@Singleton
public class SessionManagementHandler {

  private final LoginHandler loginHandler;
  private final UserService userService;

  @Inject
  public SessionManagementHandler(LoginHandler loginHandler, UserService userService) {
    this.loginHandler = loginHandler;
    this.userService = userService;
  }

  public Session login(LoginSpec spec) {
    // Same checks as v1 login: local and LDAP passwords, and local login while SSO is enabled.
    CustomerLoginFormData credentials = new CustomerLoginFormData();
    credentials.setEmail(spec.getEmail());
    credentials.setPassword(spec.getPassword());
    Users user = loginHandler.login(credentials);

    // No user was authenticated before this request; attribute the audit entry to this one.
    Customer customer = Customer.get(user.getCustomerUUID());
    RequestContext.putIfAbsent(
        TokenAuthenticator.USER, userService.getUserWithFeatures(customer, user));

    return new Session()
        .authToken(user.createAuthToken())
        .customerUuid(customer.getUuid())
        .userUuid(user.getUuid());
  }
}
