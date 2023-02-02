/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.controllers.handlers;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.user.UserService;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Users;
import org.pac4j.core.profile.CommonProfile;
import org.pac4j.core.profile.ProfileManager;
import org.pac4j.play.PlayWebContext;
import org.pac4j.play.store.PlaySessionStore;
import play.Environment;
import play.mvc.Http.Context;
import play.mvc.Http.Cookie;
import play.mvc.Http.Status;
import play.mvc.Result;
import play.mvc.Results;

@Singleton
public class ThirdPartyLoginHandler {

  private final Environment environment;
  private final PlaySessionStore playSessionStore;
  private final RuntimeConfigFactory runtimeConfigFactory;
  private final UserService userService;

  @Inject
  public ThirdPartyLoginHandler(
      Environment environment,
      PlaySessionStore playSessionStore,
      RuntimeConfigFactory runtimeConfigFactory,
      UserService userService) {
    this.environment = environment;
    this.playSessionStore = playSessionStore;
    this.runtimeConfigFactory = runtimeConfigFactory;
    this.userService = userService;
  }

  public Result redirectTo(String originUrl) {
    String redirectTo = originUrl != null ? originUrl : "/";
    if (environment.isDev()) {
      // TODO(Shashank): investigate of this is necessary
      return Results.redirect("http://localhost:3000" + redirectTo);
    } else {
      return Results.redirect(redirectTo);
    }
  }

  public Users findUserByEmailOrUnauthorizedErr(Context ctx, String email) {
    Users user = Users.getByEmail(email);
    if (user == null) {
      invalidateSession(ctx);
      throw new PlatformServiceException(Status.UNAUTHORIZED, "User not found: " + email);
    }
    return user;
  }

  public String getEmailFromCtx(Context ctx) {
    CommonProfile profile = this.getProfile(ctx);
    String emailAttr =
        runtimeConfigFactory.globalRuntimeConf().getString("yb.security.oidcEmailAttribute");
    String email;
    if (emailAttr.equals("")) {
      email = profile.getEmail();
    } else {
      email = (String) profile.getAttribute(emailAttr);
    }
    return email.toLowerCase();
  }

  void invalidateSession(Context ctx) {
    final PlayWebContext context = new PlayWebContext(ctx, playSessionStore);
    final ProfileManager<CommonProfile> profileManager = new ProfileManager<>(context);
    profileManager.logout();
    playSessionStore.destroySession(context);
  }

  CommonProfile getProfile(Context ctx) {
    final PlayWebContext context = new PlayWebContext(ctx, playSessionStore);
    final ProfileManager<CommonProfile> profileManager = new ProfileManager<>(context);
    return profileManager
        .get(true)
        .orElseThrow(
            () ->
                new PlatformServiceException(
                    Status.INTERNAL_SERVER_ERROR, "Unable to get profile"));
  }

  public void onLoginSuccess(Context ctx, Users user) {
    Customer cust = Customer.get(user.customerUUID);
    ctx.args.put("customer", cust);
    ctx.args.put("user", userService.getUserWithFeatures(cust, user));
    ctx.response()
        .setCookie(
            Cookie.builder("customerId", cust.uuid.toString())
                .withSecure(ctx.request().secure())
                .build());
    ctx.response()
        .setCookie(
            Cookie.builder("userId", user.uuid.toString())
                .withSecure(ctx.request().secure())
                .build());
  }
}
