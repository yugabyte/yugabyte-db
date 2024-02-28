// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import static play.mvc.Http.Status.FORBIDDEN;

import com.google.common.collect.ImmutableSet;
import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfigCache;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.user.UserService;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.extended.UserWithFeatures;
import com.yugabyte.yw.models.rbac.Role;
import com.yugabyte.yw.models.rbac.RoleBinding;
import java.time.Duration;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashSet;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import org.pac4j.core.profile.CommonProfile;
import org.pac4j.core.profile.ProfileManager;
import org.pac4j.play.PlayWebContext;
import org.pac4j.play.store.PlaySessionStore;
import play.libs.typedmap.TypedKey;
import play.mvc.Action;
import play.mvc.Http;
import play.mvc.Http.Cookie;
import play.mvc.Result;
import play.mvc.Results;

public class TokenAuthenticator extends Action.Simple {
  public static final Set<String> READ_POST_ENDPOINTS =
      ImmutableSet.of(
          "/alerts/page",
          "/alerts/count",
          "/alert_templates",
          "/alert_configurations/page",
          "/alert_configurations/list",
          "/maintenance_windows/page",
          "/maintenance_windows/list",
          "/backups/page",
          "/restore/page",
          "/schedules/page",
          "/fetch_package",
          "/performance_recommendations/page",
          "/performance_recommendation_state_change/page",
          "/node_agents/page",
          "/login");
  public static final String COOKIE_AUTH_TOKEN = "authToken";
  public static final String AUTH_TOKEN_HEADER = "X-AUTH-TOKEN";
  public static final String COOKIE_API_TOKEN = "apiToken";
  public static final String API_TOKEN_HEADER = "X-AUTH-YW-API-TOKEN";
  public static final String API_JWT_HEADER = "X-AUTH-YW-API-JWT";
  public static final String COOKIE_PLAY_SESSION = "PLAY_SESSION";

  public static final TypedKey<Customer> CUSTOMER = TypedKey.create("customer");

  public static final TypedKey<UserWithFeatures> USER = TypedKey.create("user");

  private static final Set<TypedKey<?>> AUTH_KEYS =
      ImmutableSet.of(
          CUSTOMER,
          USER,
          JWTVerifier.CLIENT_ID_CLAIM,
          JWTVerifier.USER_ID_CLAIM,
          JWTVerifier.CLIENT_TYPE_CLAIM);

  private final Config config;

  private final PlaySessionStore sessionStore;

  private final UserService userService;

  private final RuntimeConfigFactory runtimeConfigFactory;

  private final RuntimeConfigCache runtimeConfigCache;

  private final JWTVerifier jwtVerifier;

  @Inject
  public TokenAuthenticator(
      Config config,
      PlaySessionStore sessionStore,
      UserService userService,
      RuntimeConfigFactory runtimeConfigFactory,
      RuntimeConfigCache runtimeConfigCache,
      JWTVerifier jwtVerifier) {
    this.config = config;
    this.sessionStore = sessionStore;
    this.userService = userService;
    this.runtimeConfigFactory = runtimeConfigFactory;
    this.runtimeConfigCache = runtimeConfigCache;
    this.jwtVerifier = jwtVerifier;
  }

  public Users getCurrentAuthenticatedUser(Http.Request request) {
    String token;
    Users user = null;
    boolean useOAuth = runtimeConfigFactory.globalRuntimeConf().getBoolean("yb.security.use_oauth");
    Optional<Http.Cookie> cookieValue = request.getCookie(COOKIE_PLAY_SESSION);
    if (useOAuth) {
      final PlayWebContext context = new PlayWebContext(request, sessionStore);
      final ProfileManager<CommonProfile> profileManager = new ProfileManager<>(context);
      if (profileManager.isAuthenticated()) {
        String emailAttr =
            runtimeConfigFactory.globalRuntimeConf().getString("yb.security.oidcEmailAttribute");
        String email;
        if (emailAttr.equals("")) {
          email = profileManager.get(true).get().getEmail();
        } else {
          email = (String) profileManager.get(true).get().getAttribute(emailAttr);
        }
        user = Users.getByEmail(email.toLowerCase());
      }
      if (user == null) {
        // Defaulting to regular flow to support dual login.
        token = fetchToken(request, false /* isApiToken */);
        user = Users.authWithToken(token, getAuthTokenExpiry());
        if (user != null && !user.getRole().equals(Users.Role.SuperAdmin)) {
          user = null; // We want to only allow SuperAdmins access.
        }
      }
    } else {
      token = fetchToken(request, false /* isApiToken */);
      user = Users.authWithToken(token, getAuthTokenExpiry());
    }
    if (user == null && !cookieValue.isPresent()) {
      token = fetchToken(request, true /* isApiToken */);
      if (token == null) {
        UUID userUuid = jwtVerifier.verify(request, API_JWT_HEADER);
        if (userUuid != null) {
          user = Users.getOrBadRequest(userUuid);
        }
      } else {
        user = Users.authWithApiToken(token);
      }
    }
    return user;
  }

  @Override
  public CompletionStage<Result> call(Http.Request request) {
    boolean useNewAuthz = runtimeConfigCache.getBoolean(GlobalConfKeys.useNewRbacAuthz.getKey());
    if (useNewAuthz) {
      return delegate.call(request);
    }
    try {
      String endPoint = "";
      String path = request.path();
      String requestType = request.method();
      Pattern pattern = Pattern.compile(".*/customers/([a-zA-Z0-9-]+)(/.*)?");
      Matcher matcher = pattern.matcher(path);
      UUID custUUID = null;
      String patternForUUID =
          "[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}";
      String patternForHost = ".+:[0-9]{4,5}";

      // Allow for disabling authentication on proxy endpoint so that
      // Prometheus can scrape database nodes.
      if (Pattern.matches(
              String.format("^.*/universes/%s/proxy/%s/(.*)$", patternForUUID, patternForHost),
              path)
          && !config.getBoolean("yb.security.enable_auth_for_proxy_metrics")) {
        return delegate.call(request);
      }

      if (matcher.find()) {
        custUUID = UUID.fromString(matcher.group(1));
        endPoint = ((endPoint = matcher.group(2)) != null) ? endPoint : "";
      }
      Customer cust;
      Users user = getCurrentAuthenticatedUser(request);

      if (user != null) {
        cust = Customer.get(user.getCustomerUUID());
      } else {
        return CompletableFuture.completedFuture(
            Results.unauthorized("Unable To Authenticate User"));
      }

      // Some authenticated calls don't actually need to be authenticated
      // (e.g. /metadata/column_types). Only check auth_token is valid in that case.
      if (cust != null && (custUUID == null || custUUID.equals(cust.getUuid()))) {
        if (!checkAccessLevel(endPoint, user, requestType, path)) {
          return CompletableFuture.completedFuture(Results.forbidden("User doesn't have access"));
        }
        RequestContext.put(CUSTOMER, cust);
        RequestContext.put(USER, userService.getUserWithFeatures(cust, user));
      } else {
        // Send Unauthorized Response if Authentication Fails.
        return CompletableFuture.completedFuture(
            Results.unauthorized("Unable To Authenticate User"));
      }
      return delegate.call(request);
    } finally {
      RequestContext.clean(AUTH_KEYS);
    }
  }

  public boolean checkAuthentication(Http.Request request, Set<Users.Role> roles) {
    String token = fetchToken(request, true);
    Users user = null;
    if (token != null) {
      user = Users.authWithApiToken(token);
    } else {
      token = fetchToken(request, false);
      user = Users.authWithToken(token, getAuthTokenExpiry());
    }
    if (user != null) {
      boolean foundRole = false;
      boolean useNewAuthz = runtimeConfigCache.getBoolean(GlobalConfKeys.useNewRbacAuthz.getKey());
      if (!useNewAuthz) {
        if (roles.contains(user.getRole())) {
          // So we can audit any super admin actions.
          // If there is a use case also lookup customer and put it in context
          UserWithFeatures userWithFeatures = new UserWithFeatures().setUser(user);
          RequestContext.put(USER, userWithFeatures);
          foundRole = true;
        }
      } else {
        for (Users.Role usersRole : roles) {
          Role role = Role.get(user.getCustomerUUID(), usersRole.name());
          if (RoleBinding.checkUserHasRole(user.getUuid(), role.getRoleUUID())) {
            UserWithFeatures userWithFeatures = new UserWithFeatures().setUser(user);
            RequestContext.put(USER, userWithFeatures);
            foundRole = true;
          }
        }
      }
      return foundRole;
    }
    return false;
  }

  public boolean superAdminAuthentication(Http.Request request) {
    return checkAuthentication(
        request, new HashSet<>(Collections.singletonList(Users.Role.SuperAdmin)));
  }

  // Calls that require admin authentication should allow
  // both admins and super-admins.
  public boolean adminAuthentication(Http.Request request) {
    return checkAuthentication(
        request, new HashSet<>(Arrays.asList(Users.Role.Admin, Users.Role.SuperAdmin)));
  }

  public void adminOrThrow(Http.Request request) {
    if (!adminAuthentication(request)) {
      throw new PlatformServiceException(FORBIDDEN, "Only Admins can perform this operation.");
    }
  }

  // TODO: Consider changing to a method annotation
  public void superAdminOrThrow(Http.Request request) {
    if (!superAdminAuthentication(request)) {
      throw new PlatformServiceException(
          FORBIDDEN, "Only Super Admins can perform this operation.");
    }
  }

  private static String fetchToken(Http.Request request, boolean isApiToken) {
    String header, cookie;
    if (isApiToken) {
      header = API_TOKEN_HEADER;
      cookie = COOKIE_API_TOKEN;
    } else {
      header = AUTH_TOKEN_HEADER;
      cookie = COOKIE_AUTH_TOKEN;
    }
    Optional<String> headerValueOp = request.header(header);
    Optional<Http.Cookie> cookieValue = request.getCookie(cookie);

    if (headerValueOp.isPresent()) {
      return headerValueOp.get();
    }
    // If we are accessing authenticated pages, the auth token would be in the cookie
    return cookieValue.map(Cookie::value).orElse(null);
  }

  // Check role, and if the API call is accessible.
  private boolean checkAccessLevel(String endPoint, Users user, String requestType, String path) {
    // Users should be allowed to change their password.
    // Even admin users should not be allowed to change another
    // user's password.
    if (endPoint.endsWith("/change_password")) {
      UUID userUUID = UUID.fromString(endPoint.split("/")[2]);
      return userUUID.equals(user.getUuid());
    }

    if (requestType.equals("GET")
        && (endPoint.endsWith("/users/" + user.getUuid())
            || path.endsWith("/customers/" + user.getCustomerUUID()))) {
      return true;
    }

    // If the user is ConnectOnly, then don't get any further access
    if (user.getRole() == Users.Role.ConnectOnly) {
      return false;
    }

    // Allow only superadmins to change LDAP Group Mappings.
    if (endPoint.endsWith("/ldap_mappings") && requestType.equals("PUT")) {
      return user.getRole() == Users.Role.SuperAdmin;
    }

    // All users have access to get, metrics and setting an API token.
    if (requestType.equals("GET") || endPoint.equals("/metrics") || endPoint.equals("/api_token")) {
      return true;
    }
    // Also some read requests are using POST query, because of complex body.
    if (requestType.equals("POST") && READ_POST_ENDPOINTS.contains(endPoint)) {
      return true;
    }

    if (endPoint.endsWith("/update_profile")) return true;

    // If the user is readonly, then don't get any further access.
    if (user.getRole() == Users.Role.ReadOnly) {
      return false;
    }
    // All users other than read only get access to backup endpoints.
    if (endPoint.endsWith("/create_backup")
        || endPoint.endsWith("/multi_table_backup")
        || endPoint.endsWith("/restore")) {
      return true;
    }
    // Enable New backup and restore endPoints for backup admins.
    if (endPoint.contains("/backups")
        || endPoint.endsWith("create_backup_schedule")
        || endPoint.endsWith("create_backup_schedule_async")
        || endPoint.contains("/schedules")
        || endPoint.endsWith("/restore")) {
      return true;
    }
    // If the user is backupAdmin, they don't get further access.
    return user.getRole() != Users.Role.BackupAdmin;
    // If the user has reached here, they have complete access.
  }

  private Duration getAuthTokenExpiry() {
    return config.getDuration("yb.authtoken.token_expiry");
  }
}
