// Copyright (c) YugabyteDB, Inc.

package api.v2.handlers;

import api.v2.models.LoginSpec;
import api.v2.models.Session;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.controllers.LoginHandler;
import com.yugabyte.yw.forms.CustomerLoginFormData;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Users;
import play.mvc.Http;

@Singleton
public class SessionManagementHandler {

  private final LoginHandler loginHandler;

  @Inject
  public SessionManagementHandler(LoginHandler loginHandler) {
    this.loginHandler = loginHandler;
  }

  public Session login(Http.Request request, LoginSpec spec) {
    // Same checks as v1 login: local and LDAP passwords, and local login while SSO is enabled.
    CustomerLoginFormData credentials = new CustomerLoginFormData();
    credentials.setEmail(spec.getEmail());
    credentials.setPassword(spec.getPassword());
    Users user = loginHandler.login(credentials);

    // Audited with the user passed explicitly, as v1 login does. The generated audit reads the user
    // from RequestContext, a ThreadLocal that only TokenAuthenticator clears, and this controller
    // has none: a user put there would stay on the worker thread and be attributed to later
    // requests.
    Audit.create(
        user,
        request.path(),
        request.method(),
        Audit.TargetType.User,
        user.getUuid().toString(),
        Audit.ActionType.Login,
        null,
        null,
        null,
        request.remoteAddress());

    return new Session()
        .authToken(user.createAuthToken())
        .customerUuid(user.getCustomerUUID())
        .userUuid(user.getUuid());
  }
}
