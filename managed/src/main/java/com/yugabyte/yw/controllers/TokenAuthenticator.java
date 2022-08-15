// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import static play.mvc.Http.Status.UNAUTHORIZED;

import com.google.common.collect.ImmutableSet;
import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.user.UserService;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.Users.Role;
import com.yugabyte.yw.models.extended.UserWithFeatures;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.time.Duration;
import org.pac4j.core.profile.CommonProfile;
import org.pac4j.core.profile.ProfileManager;
import org.pac4j.play.PlayWebContext;
import org.pac4j.play.store.PlaySessionStore;
import play.mvc.Action;
import play.mvc.Http;
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
          "/schedules/page",
          "/fetch_package");
  public static final String COOKIE_AUTH_TOKEN = "authToken";
  public static final String AUTH_TOKEN_HEADER = "X-AUTH-TOKEN";
  public static final String COOKIE_API_TOKEN = "apiToken";
  public static final String API_TOKEN_HEADER = "X-AUTH-YW-API-TOKEN";
  public static final String API_JWT_HEADER = "X-AUTH-YW-API-JWT";
  public static final String COOKIE_PLAY_SESSION = "PLAY_SESSION";

  private final Config config;

  private final PlaySessionStore playSessionStore;

  private final UserService userService;

  private final RuntimeConfigFactory runtimeConfigFactory;

  private final JWTVerifier jwtVerifier;

  @Inject
  public TokenAuthenticator(
      Config config,
      PlaySessionStore playSessionStore,
      UserService userService,
      RuntimeConfigFactory runtimeConfigFactory,
      JWTVerifier jwtVerifier) {
    this.config = config;
    this.playSessionStore = playSessionStore;
    this.userService = userService;
    this.runtimeConfigFactory = runtimeConfigFactory;
    this.jwtVerifier = jwtVerifier;
  }

  private Users getCurrentAuthenticatedUser(Http.Context ctx) {
    String token;
    Users user = null;
    boolean useOAuth = runtimeConfigFactory.globalRuntimeConf().getBoolean("yb.security.use_oauth");
    Http.Cookie cookieValue = ctx.request().cookie(COOKIE_PLAY_SESSION);
    if (useOAuth) {
      final PlayWebContext context = new PlayWebContext(ctx, playSessionStore);
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
        token = fetchToken(ctx, false /* isApiToken */);
        user = Users.authWithToken(token, getAuthTokenExpiry());
        if (user != null && !user.getRole().equals(Role.SuperAdmin)) {
          user = null; // We want to only allow SuperAdmins access.
        }
      }
    } else {
      token = fetchToken(ctx, false /* isApiToken */);
      user = Users.authWithToken(token, getAuthTokenExpiry());
    }
    if (user == null && cookieValue == null) {
      token = fetchToken(ctx, true /* isApiToken */);
      if (token == null) {
        UUID userUuid = jwtVerifier.verify(ctx, API_JWT_HEADER);
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
  public CompletionStage<Result> call(Http.Context ctx) {
    String path = ctx.request().path();
    String endPoint = "";
    String requestType = ctx.request().method();
    Pattern pattern = Pattern.compile(".*/customers/([a-zA-Z0-9-]+)(/.*)?");
    Matcher matcher = pattern.matcher(path);
    UUID custUUID = null;
    String patternForUUID =
        "[0-9a-fA-F]{8}-[0-9a-fA-F]{4}" + "-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}";
    String patternForHost = ".+:[0-9]{4,5}";

    // Allow for disabling authentication on proxy endpoint so that
    // Prometheus can scrape database nodes.
    if (Pattern.matches(
            String.format(
                "^.*/universes/%s/proxy/%s/(metrics|prometheus-metrics)$",
                patternForUUID, patternForHost),
            path)
        && !config.getBoolean("yb.security.enable_auth_for_proxy_metrics")) {
      return delegate.call(ctx);
    }

    if (matcher.find()) {
      custUUID = UUID.fromString(matcher.group(1));
      endPoint = ((endPoint = matcher.group(2)) != null) ? endPoint : "";
    }
    Customer cust;
    Users user = getCurrentAuthenticatedUser(ctx);

    if (user != null) {
      cust = Customer.get(user.customerUUID);
    } else {
      return CompletableFuture.completedFuture(Results.forbidden("Unable To Authenticate User"));
    }

    // Some authenticated calls don't actually need to be authenticated
    // (e.g. /metadata/column_types). Only check auth_token is valid in that case.
    if (cust != null && (custUUID == null || custUUID.equals(cust.uuid))) {
      if (!checkAccessLevel(endPoint, user, requestType)) {
        return CompletableFuture.completedFuture(Results.forbidden("User doesn't have access"));
      }
      // TODO: withUsername returns new request that is ignored. Maybe a bug.
      ctx.request().withUsername(user.getEmail());
      ctx.args.put("customer", cust);
      ctx.args.put("user", userService.getUserWithFeatures(cust, user));
    } else {
      // Send Forbidden Response if Authentication Fails.
      return CompletableFuture.completedFuture(Results.forbidden("Unable To Authenticate User"));
    }
    return delegate.call(ctx);
  }

  public boolean superAdminAuthentication(Http.Context ctx) {
    String token = fetchToken(ctx, true);
    Users user;
    if (token != null) {
      user = Users.authWithApiToken(token);
    } else {
      token = fetchToken(ctx, false);
      user = Users.authWithToken(token, getAuthTokenExpiry());
    }
    if (user != null) {
      boolean isSuperAdmin = user.getRole() == Role.SuperAdmin;
      if (isSuperAdmin) {
        // So we can audit any super admin actions.
        // If there is a use case also lookup customer and put it in context
        UserWithFeatures superAdmin = new UserWithFeatures().setUser(user);
        ctx.args.put("user", superAdmin);
      }
      return isSuperAdmin;
    }
    return false;
  }

  // TODO: Consider changing to a method annotation
  public void superAdminOrThrow(Http.Context ctx) {
    if (!superAdminAuthentication(ctx)) {
      throw new PlatformServiceException(
          UNAUTHORIZED, "Only Super Admins can perform this operation.");
    }
  }

  private static String fetchToken(Http.Context ctx, boolean isApiToken) {
    String header, cookie;
    if (isApiToken) {
      header = API_TOKEN_HEADER;
      cookie = COOKIE_API_TOKEN;
    } else {
      header = AUTH_TOKEN_HEADER;
      cookie = COOKIE_AUTH_TOKEN;
    }
    Optional<String> headerValueOp = ctx.request().header(header);
    Http.Cookie cookieValue = ctx.request().cookie(cookie);

    if (headerValueOp.isPresent()) {
      return headerValueOp.get();
    }
    if (cookieValue != null) {
      // If we are accessing authenticated pages, the auth token would be in the cookie
      return cookieValue.value();
    }
    return null;
  }

  // Check role, and if the API call is accessible.
  private boolean checkAccessLevel(String endPoint, Users user, String requestType) {
    // Users should be allowed to change their password.
    // Even admin users should not be allowed to change another
    // user's password.
    if (endPoint.endsWith("/change_password")) {
      UUID userUUID = UUID.fromString(endPoint.split("/")[2]);
      return userUUID.equals(user.uuid);
    }

    // All users have access to get, metrics and setting an API token.
    if (requestType.equals("GET") || endPoint.equals("/metrics") || endPoint.equals("/api_token")) {
      return true;
    }
    // Also some read requests are using POST query, because of complex body.
    if (requestType.equals("POST") && READ_POST_ENDPOINTS.contains(endPoint)) {
      return true;
    }
    // If the user is readonly, then don't get any further access.
    if (user.getRole() == Role.ReadOnly) {
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
        || endPoint.contains("/schedules")
        || endPoint.endsWith("/restore")) {
      return true;
    }
    // If the user is backupAdmin, they don't get further access.
    return user.getRole() != Role.BackupAdmin;
    // If the user has reached here, they have complete access.
  }

  private Duration getAuthTokenExpiry() {
    return config.getDuration("yb.authtoken.token_expiry");
  }
}
