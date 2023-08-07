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
import com.yugabyte.yw.models.Users;
import org.pac4j.core.profile.CommonProfile;
import org.pac4j.core.profile.ProfileManager;
import org.pac4j.play.PlayWebContext;
import org.pac4j.play.store.PlaySessionStore;
import play.Environment;
import play.mvc.Http.Request;
import play.mvc.Http.Status;
import play.mvc.Result;
import play.mvc.Results;

@Singleton
public class ThirdPartyLoginHandler {

  private final Environment environment;
  private final PlaySessionStore sessionStore;
  private final RuntimeConfigFactory runtimeConfigFactory;

  @Inject
  public ThirdPartyLoginHandler(
      Environment environment,
      PlaySessionStore sessionStore,
      RuntimeConfigFactory runtimeConfigFactory) {
    this.environment = environment;
    this.sessionStore = sessionStore;
    this.runtimeConfigFactory = runtimeConfigFactory;
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

  public Users findUserByEmailOrUnauthorizedErr(Request request, String email) {
    Users user = Users.getByEmail(email);
    if (user == null) {
      invalidateSession(request);
      throw new PlatformServiceException(Status.UNAUTHORIZED, "User not found: " + email);
    }
    return user;
  }

  public String getEmailFromCtx(Request request) {
    CommonProfile profile = this.getProfile(request);
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

  void invalidateSession(Request request) {
    final PlayWebContext context = new PlayWebContext(request, sessionStore);
    final ProfileManager<CommonProfile> profileManager = new ProfileManager<>(context);
    profileManager.logout();
    sessionStore.destroySession(context);
  }

  public CommonProfile getProfile(Request request) {
    final PlayWebContext context = new PlayWebContext(request, sessionStore);
    final ProfileManager<CommonProfile> profileManager = new ProfileManager<>(context);
    return profileManager
        .get(true)
        .orElseThrow(
            () ->
                new PlatformServiceException(
                    Status.INTERNAL_SERVER_ERROR, "Unable to get profile"));
  }
}
