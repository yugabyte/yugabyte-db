/*
 * Copyright 2022 YugabyteDB, Inc. and Contributors
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
import com.yugabyte.yw.common.rbac.RoleBindingUtil;
import com.yugabyte.yw.forms.CustomerLoginFormData;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.Users.UserType;
import org.apache.directory.api.ldap.model.exception.LdapException;
import org.jetbrains.annotations.NotNull;
import play.mvc.Http.Status;

public class LoginHandler {
  private final RuntimeConfGetter confGetter;

  private final LdapUtil ldapUtil;

  private final RoleBindingUtil roleBindingUtil;

  @Inject
  public LoginHandler(
      RuntimeConfGetter confGetter, LdapUtil ldapUtil, RoleBindingUtil roleBindingUtil) {
    this.confGetter = confGetter;
    this.ldapUtil = ldapUtil;
    this.roleBindingUtil = roleBindingUtil;
  }

  @NotNull
  Users login(CustomerLoginFormData data) {
    boolean useOAuth = this.confGetter.getGlobalConf(GlobalConfKeys.useOauth);
    boolean useLdap = this.confGetter.getGlobalConf(GlobalConfKeys.useLdap);
    boolean allowLocalLoginWithSso =
        this.confGetter.getGlobalConf(GlobalConfKeys.allowLocalLoginWithSso);

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
    // No LDAP user can pass the gate below, so skipping the bind costs nothing and avoids a great
    // deal: loginWithLdap creates the users and principal rows, rewrites role bindings and commits,
    // and a first-time LDAP user has no row to inspect beforehand. Skipping is therefore also what
    // denies LDAP users here -- they fall to the "Invalid User Credentials" throw below rather than
    // reaching the gate. Remove this condition and they would be provisioned before being rejected.
    if (useLdap && user == null && (!useOAuth || allowLocalLoginWithSso)) {
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

    if (useOAuth
        && !allowLocalLoginWithSso
        && !(RoleBindingUtil.isLocalAccount(user) && roleBindingUtil.isSuperAdmin(user))) {
      throw new PlatformServiceException(
          Status.UNAUTHORIZED,
          "Local login is not permitted for this account. Please sign in with SSO.");
    }
    return user;
  }
}
