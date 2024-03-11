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

import static com.yugabyte.yw.common.Util.getRandomPassword;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.nimbusds.jwt.JWT;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.rbac.RoleBindingUtil;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.OidcGroupToYbaRoles;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.Users.UserType;
import com.yugabyte.yw.models.rbac.Role;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import org.pac4j.core.profile.CommonProfile;
import org.pac4j.core.profile.ProfileManager;
import org.pac4j.oidc.profile.OidcProfile;
import org.pac4j.play.PlayWebContext;
import org.pac4j.play.store.PlaySessionStore;
import play.Environment;
import play.mvc.Http.Request;
import play.mvc.Http.Status;
import play.mvc.Result;
import play.mvc.Results;

@Singleton
@Slf4j
public class ThirdPartyLoginHandler {

  private final Environment environment;
  private final PlaySessionStore sessionStore;
  private final RuntimeConfGetter confGetter;
  private final RoleBindingUtil roleBindingUtil;
  private final ApiHelper apiHelper;

  private final String MS_MEMBEROF_API = "https://graph.microsoft.com/v1.0/users/%s/memberOf";

  @Inject
  public ThirdPartyLoginHandler(
      Environment environment,
      PlaySessionStore sessionStore,
      RuntimeConfGetter confGetter,
      RoleBindingUtil roleBindingUtil,
      ApiHelper apiHelper) {
    this.environment = environment;
    this.sessionStore = sessionStore;
    this.confGetter = confGetter;
    this.roleBindingUtil = roleBindingUtil;
    this.apiHelper = apiHelper;
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

  /**
   * Creates new user on OIDC login
   *
   * @return the newly created user
   */
  public Users findUserByEmailOrCreateNewUser(Request request, String email) {
    int customerCount = Customer.getAll().size();
    boolean multiTenant = confGetter.getStaticConf().getBoolean("yb.multiTenant");
    if (multiTenant || customerCount > 1) {
      throw new PlatformServiceException(
          BAD_REQUEST, "SSO login not supported on multi tenant environment");
    }
    UUID custUUID = Customer.find.query().findOne().getUuid();
    Set<UUID> rolesSet = getRolesFromGroupMemberships(request, custUUID);
    Users.Role userRole = confGetter.getGlobalConf(GlobalConfKeys.oidcDefaultRole);

    // calculate final system role to be assigned to user
    for (UUID roleUUID : rolesSet) {
      Role role = Role.find.byId(roleUUID);
      Users.Role systemRole = Users.Role.valueOf(role.getName());
      userRole = Users.Role.union(systemRole, userRole);
    }

    // update role if existing user or create new user
    Users user = Users.getByEmail(email);
    if (user != null) {
      user.setRole(userRole);
      user.setUserType(UserType.oidc);
    } else {
      user = Users.create(email, getRandomPassword(), userRole, custUUID, false, UserType.oidc);
    }

    // add role bindings if RBAC is on
    boolean useNewAuthz = confGetter.getGlobalConf(GlobalConfKeys.useNewRbacAuthz);
    if (useNewAuthz) {
      roleBindingUtil.createRoleBindingsForSystemRole(user);
    }

    user.save();
    return user;
  }

  /**
   * Extract list of groups from ID token. Return the list of mapped roles.
   *
   * @return List of role UUIDs
   */
  private Set<UUID> getRolesFromGroupMemberships(Request request, UUID custUUID) {
    Set<UUID> roles = new HashSet<>();
    try {
      OidcProfile profile = (OidcProfile) getProfile(request);
      JWT idToken = profile.getIdToken();
      List<String> groups;

      // If the IdP is Azure we need to fetch groups from Microsoft endpoint since group names are
      // not returned in ID token
      if (isIdpAzure(idToken.getJWTClaimsSet().getIssuer())) {
        groups =
            getMsGroupsList(
                idToken.getJWTClaimsSet().getStringClaim("oid"),
                profile.getAccessToken().toAuthorizationHeader());
      } else {
        groups = idToken.getJWTClaimsSet().getStringListClaim("groups");
      }
      log.info("List of user's groups = {}", groups.toString());

      for (String group : groups) {
        OidcGroupToYbaRoles entity =
            OidcGroupToYbaRoles.find
                .query()
                .where()
                .eq("customer_uuid", custUUID)
                .eq("group_name", group.toLowerCase())
                .findOne();
        if (entity != null) {
          roles.addAll(entity.getYbaRoles());
        }
      }
    } catch (Exception e) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Error in getting roles from group mapping: " + e.getMessage());
    }
    return roles;
  }

  private boolean isIdpAzure(String issuer) {
    final String V1 = "sts.windows.net";
    final String V2 = "login.microsoftonline.com";
    return issuer.contains(V1) || issuer.contains(V2);
  }

  /**
   * Hit the Microsoft endpoint to fetch the list of user's groups.
   *
   * @return The list of group names.
   */
  private List<String> getMsGroupsList(String userID, String authHeader) {
    String url = String.format(MS_MEMBEROF_API, userID);
    Map<String, String> headers = new HashMap<>();
    headers.put("Authorization", authHeader);
    JsonNode result = apiHelper.getRequest(url, headers);
    List<String> groups = new ArrayList<>();
    log.trace("Result from microsoft endpoint = {}", result.toPrettyString());
    if (result.has("error")) {
      log.error(
          "Fetching group membership from MicroSoft failed with the following error: {}\n"
              + "user will be created with default role.",
          result.get("error").toPrettyString());
      return groups;
    }

    Iterator<JsonNode> elements = result.get("value").elements();
    while (elements.hasNext()) {
      JsonNode element = elements.next();
      JsonNode displayNameNode = element.get("displayName");
      if (displayNameNode != null && displayNameNode.isTextual()) {
        groups.add(displayNameNode.asText());
      }
    }
    return groups;
  }

  public String getEmailFromCtx(Request request) {
    CommonProfile profile = this.getProfile(request);
    String emailAttr = confGetter.getGlobalConf(GlobalConfKeys.oidcEmailAttribute);
    String email;
    if (emailAttr != null && StringUtils.isNotBlank(emailAttr)) {
      email = (String) profile.getAttribute(emailAttr);
      if (email == null) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Unable to fetch email. Please check the email attribute specified.");
      }
    } else {
      email = profile.getEmail();
      // If email is null then the token doesn't have the 'email' claim.
      // Need to specify the email attribute in the OIDC config.
      if (email == null) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            "Unable to fetch email. Please specify the email attribute in the OIDC config.");
      }
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
