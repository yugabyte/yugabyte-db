/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.controllers;

import com.google.inject.Inject;
import com.yugabyte.yw.common.LdapUtil;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.forms.CustomerLoginFormData;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.Users.Role;
import com.yugabyte.yw.models.Users.UserType;
import org.apache.directory.api.ldap.model.exception.LdapException;
import org.jetbrains.annotations.NotNull;
import play.mvc.Http.Status;

public class LoginHandler {
  private final RuntimeConfGetter confGetter;

  private final LdapUtil ldapUtil;

  @Inject
  public LoginHandler(RuntimeConfGetter confGetter, LdapUtil ldapUtil) {
    this.confGetter = confGetter;
    this.ldapUtil = ldapUtil;
  }

  @NotNull
  Users login(CustomerLoginFormData data) {
    boolean useOAuth = this.confGetter.getGlobalConf(GlobalConfKeys.useOauth);
    boolean useLdap = this.confGetter.getGlobalConf(GlobalConfKeys.useLdap);

    Users user = null;
    Users existingUser =
        Users.find.query().where().eq("email", data.getEmail().toLowerCase()).findOne();
    if (existingUser != null) {
      if (existingUser.getUserType() == null || !existingUser.getUserType().equals(UserType.ldap)) {
        user = Users.authWithPassword(data.getEmail().toLowerCase(), data.getPassword());
        if (user == null) {
          throw new PlatformServiceException(Status.UNAUTHORIZED, "Invalid User Credentials.");
        }
      }
    }
    if (useLdap && user == null) {
      try {
        user = ldapUtil.loginWithLdap(data);
      } catch (LdapException e) {
        String errMsg =
            String.format("LDAP error %s authenticating user %s", e.getMessage(), data.getEmail());
        SessionController.LOG.error(errMsg);
        throw new PlatformServiceException(Status.BAD_REQUEST, errMsg);
      }
    }

    if (user == null) {
      throw new PlatformServiceException(Status.UNAUTHORIZED, "Invalid User Credentials.");
    }

    if (useOAuth && !user.getRole().equals(Role.SuperAdmin)) {
      throw new PlatformServiceException(
          Status.UNAUTHORIZED,
          "Only SuperAdmin access permitted via normal login when SSO is enabled.");
    }
    return user;
  }
}
