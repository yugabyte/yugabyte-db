// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.Util.getRandomPassword;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.password.PasswordPolicyService;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.common.rbac.RoleBindingUtil;
import com.yugabyte.yw.common.rbac.RoleResourceDefinition;
import com.yugabyte.yw.common.rbac.RoleUtil;
import com.yugabyte.yw.common.user.UserService;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.forms.UserProfileFormData;
import com.yugabyte.yw.forms.UserRegisterFormData;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.Users.UserType;
import com.yugabyte.yw.models.extended.UserWithFeatures;
import com.yugabyte.yw.models.rbac.ResourceGroup;
import com.yugabyte.yw.models.rbac.Role;
import com.yugabyte.yw.models.rbac.RoleBinding;
import com.yugabyte.yw.models.rbac.RoleBinding.RoleBindingType;
import com.yugabyte.yw.rbac.annotations.AuthzPath;
import com.yugabyte.yw.rbac.annotations.PermissionAttribute;
import com.yugabyte.yw.rbac.annotations.RequiredPermissionOnResource;
import com.yugabyte.yw.rbac.annotations.Resource;
import com.yugabyte.yw.rbac.enums.SourceType;
import io.ebean.annotation.Transactional;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiImplicitParam;
import io.swagger.annotations.ApiImplicitParams;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.util.*;
import java.util.stream.Collectors;
import javax.inject.Inject;
import org.apache.commons.lang3.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.data.Form;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;

@Api(
    value = "User management",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class UsersController extends AuthenticatedController {

  public static final Logger LOG = LoggerFactory.getLogger(UsersController.class);

  @Inject private RuntimeConfGetter confGetter;

  private final PasswordPolicyService passwordPolicyService;
  private final UserService userService;
  private final TokenAuthenticator tokenAuthenticator;
  private final RuntimeConfigFactory runtimeConfigFactory;
  private final RoleUtil roleUtil;
  private final RoleBindingUtil roleBindingUtil;

  @Inject
  public UsersController(
      PasswordPolicyService passwordPolicyService,
      UserService userService,
      TokenAuthenticator tokenAuthenticator,
      RuntimeConfigFactory runtimeConfigFactory,
      RoleUtil roleUtil,
      RoleBindingUtil roleBindingUtil) {
    this.passwordPolicyService = passwordPolicyService;
    this.userService = userService;
    this.tokenAuthenticator = tokenAuthenticator;
    this.runtimeConfigFactory = runtimeConfigFactory;
    this.roleUtil = roleUtil;
    this.roleBindingUtil = roleBindingUtil;
  }

  /**
   * GET endpoint for listing the provider User.
   *
   * @return JSON response with user.
   */
  @ApiOperation(
      value = "Get a user's details",
      nickname = "getUserDetails",
      response = UserWithFeatures.class)
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.USER, action = Action.READ),
        resourceLocation = @Resource(path = Util.USERS, sourceType = SourceType.ENDPOINT))
  })
  public Result index(UUID customerUUID, UUID userUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Users user = Users.getOrBadRequest(customerUUID, userUUID);
    return PlatformResults.withData(userService.getUserWithFeatures(customer, user));
  }

  /**
   * GET endpoint for listing all available Users for a customer
   *
   * @return JSON response with users belonging to the customer.
   */
  @ApiOperation(
      value = "List all users",
      nickname = "listUsers",
      response = UserWithFeatures.class,
      responseContainer = "List")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.USER, action = Action.READ),
        resourceLocation = @Resource(path = Util.USERS, sourceType = SourceType.ENDPOINT),
        checkOnlyPermission = true)
  })
  public Result list(UUID customerUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    UserWithFeatures u = RequestContext.get(TokenAuthenticator.USER);
    Set<UUID> resourceUUIDs =
        roleBindingUtil.getResourceUuids(u.getUser().getUuid(), ResourceType.USER, Action.READ);
    List<Users> users = Users.getAll(customerUUID);
    List<UserWithFeatures> userWithFeaturesList =
        users.stream()
            .filter(user -> resourceUUIDs.contains(user.getUuid()))
            .map(user -> userService.getUserWithFeatures(customer, user))
            .collect(Collectors.toList());
    return PlatformResults.withData(userWithFeaturesList);
  }

  /**
   * POST endpoint for creating new Users.
   *
   * @return JSON response of newly created user.
   */
  @ApiOperation(value = "Create a user", nickname = "createUser", response = UserWithFeatures.class)
  @ApiImplicitParams({
    @ApiImplicitParam(
        name = "User",
        value = "Details of the new user",
        required = true,
        dataType = "com.yugabyte.yw.forms.UserRegisterFormData",
        paramType = "body")
  })
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.USER, action = Action.CREATE),
        resourceLocation = @Resource(path = Util.USERS, sourceType = SourceType.ENDPOINT)),
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.ROLE, action = Action.READ),
        resourceLocation = @Resource(path = Util.ROLE, sourceType = SourceType.ENDPOINT))
  })
  @Transactional
  public Result create(UUID customerUUID, Http.Request request) {
    Customer customer = Customer.getOrBadRequest(customerUUID);

    JsonNode requestBody = request.body().asJson();
    UserRegisterFormData formData =
        formFactory.getFormDataOrBadRequest(requestBody, UserRegisterFormData.class);

    if (confGetter.getGlobalConf(GlobalConfKeys.useOauth)) {

      if (confGetter.getGlobalConf(GlobalConfKeys.enableOidcAutoCreateUser)) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Manual user creation not allowed with OIDC setup!");
      }

      formData.setPassword(getRandomPassword()); // Password is not used.
    }

    if (formData.getRole() == Users.Role.SuperAdmin) {
      throw new PlatformServiceException(BAD_REQUEST, "Cannot create an user as SuperAdmin");
    }

    // Validate password.
    passwordPolicyService.checkPasswordPolicy(customerUUID, formData.getPassword());

    boolean useNewAuthz =
        runtimeConfigFactory.globalRuntimeConf().getBoolean("yb.rbac.use_new_authz");
    Users user;

    if (!useNewAuthz) {
      LOG.debug("Using old authz RBAC model to create user.");
      user =
          Users.create(
              formData.getEmail(), formData.getPassword(), formData.getRole(), customerUUID, false);
    } else {
      LOG.debug("Using new authz RBAC model to create user.");

      // Case 1:
      // Check the role field. Original API use case. To be deprecated.
      if (formData.getRole() != null && formData.getRoleResourceDefinitions() == null) {
        // Create the user.
        user =
            Users.create(
                formData.getEmail(),
                formData.getPassword(),
                formData.getRole(),
                customerUUID,
                false);
        roleBindingUtil.createRoleBindingsForSystemRole(user);
      }

      // Case 2:
      // Check the role and resource definitions list field. New RBAC APIs use case. To be
      // standardized.
      else if (formData.getRole() == null && formData.getRoleResourceDefinitions() != null) {
        // Validate the roles and resource group definitions given.
        roleBindingUtil.validateRoles(customerUUID, formData.getRoleResourceDefinitions());
        roleBindingUtil.validateResourceGroups(customerUUID, formData.getRoleResourceDefinitions());

        // Create the user.
        user =
            Users.create(
                formData.getEmail(),
                formData.getPassword(),
                Users.Role.ConnectOnly,
                customerUUID,
                false);

        // Populate all the system default resource groups for all system defined roles.
        roleBindingUtil.populateSystemRoleResourceGroups(
            customerUUID, user.getUuid(), formData.getRoleResourceDefinitions());
        // Add all the role bindings for the user.
        List<RoleBinding> createdRoleBindings =
            roleBindingUtil.setUserRoleBindings(
                user.getUuid(), formData.getRoleResourceDefinitions(), RoleBindingType.Custom);

        LOG.info(
            "Created user '{}', email '{}', custom role bindings '{}'.",
            user.getUuid(),
            user.getEmail(),
            createdRoleBindings.toString());
      }

      // Case 3:
      // When both or none of ('role', 'roleResourceDefinitions') are given by the user. Wrong API
      // usage.
      else {
        String errMsg =
            String.format(
                "Any one of the fields 'role' or 'roleResourceDefinitions' should be defined. "
                    + "Instead got 'role' = '%s' and 'roleResourceDefinitions' = '%s'.",
                formData.getRole(), formData.getRoleResourceDefinitions().toString());
        LOG.error(errMsg);
        throw new PlatformServiceException(BAD_REQUEST, errMsg);
      }
    }
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.User,
            Objects.toString(user.getUuid(), null),
            Audit.ActionType.Create,
            Json.toJson(formData));
    return PlatformResults.withData(userService.getUserWithFeatures(customer, user));
  }

  /**
   * DELETE endpoint for deleting an existing user.
   *
   * @return JSON response on whether or not delete user was successful or not.
   */
  @ApiOperation(
      value = "Delete a user",
      nickname = "deleteUser",
      notes = "Deletes the specified user. Note that you can't delete a customer's primary user.",
      response = YBPSuccess.class)
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.USER, action = Action.DELETE),
        resourceLocation = @Resource(path = Util.USERS, sourceType = SourceType.ENDPOINT))
  })
  public Result delete(UUID customerUUID, UUID userUUID, Http.Request request) {
    Users user = Users.getOrBadRequest(customerUUID, userUUID);
    if (user.isPrimary()) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "Cannot delete primary user %s for customer %s", userUUID.toString(), customerUUID));
    }
    if (user.delete()) {
      auditService()
          .createAuditEntry(
              request, Audit.TargetType.User, userUUID.toString(), Audit.ActionType.Delete);
      return YBPSuccess.empty();
    } else {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Unable to delete user UUID: " + userUUID);
    }
  }

  /**
   * GET endpoint for retrieving the OIDC auth token for a given user.
   *
   * @return JSON response with users OIDC auth token.
   */
  @ApiOperation(
      value = "Retrieve OIDC auth token",
      nickname = "retrieveOIDCAuthToken",
      response = Users.UserOIDCAuthToken.class)
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.USER, action = Action.READ),
        resourceLocation = @Resource(path = Util.USERS, sourceType = SourceType.ENDPOINT))
  })
  public Result retrieveOidcAuthToken(UUID customerUUID, UUID userUuid, Http.Request request) {
    if (!confGetter.getGlobalConf(GlobalConfKeys.oidcFeatureEnhancements)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "yb.security.oidc_feature_enhancements flag is not enabled.");
    }
    Customer.getOrBadRequest(customerUUID);
    Users user = Users.getOrBadRequest(customerUUID, userUuid);

    Users.UserOIDCAuthToken token = new Users.UserOIDCAuthToken(user.getUnmakedOidcJwtAuthToken());
    auditService()
        .createAuditEntry(
            request, Audit.TargetType.User, userUuid.toString(), Audit.ActionType.SetSecurity);
    return PlatformResults.withData(token);
  }

  /**
   * PUT endpoint for changing the role of an existing user.
   *
   * @return JSON response on whether role change was successful or not.
   */
  @ApiOperation(
      value = "Change a user's role",
      notes = "Deprecated. Use this method instead: setRoleBinding.",
      nickname = "updateUserRole",
      response = YBPSuccess.class)
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(
                resourceType = ResourceType.USER,
                action = Action.UPDATE_ROLE_BINDINGS),
        resourceLocation = @Resource(path = Util.USERS, sourceType = SourceType.ENDPOINT)),
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.ROLE, action = Action.READ),
        resourceLocation = @Resource(path = Util.ROLE, sourceType = SourceType.ENDPOINT))
  })
  @Deprecated
  @Transactional
  public Result changeRole(UUID customerUUID, UUID userUUID, String role, Http.Request request) {
    Users user = Users.getOrBadRequest(customerUUID, userUUID);
    if (UserType.ldap == user.getUserType() && user.isLdapSpecifiedRole()) {
      throw new PlatformServiceException(BAD_REQUEST, "Cannot change role for LDAP user.");
    }
    // If OIDC is setup, the IdP should be the single source of truth for users' roles
    if (user.getUserType().equals(UserType.oidc)
        && confGetter.getGlobalConf(GlobalConfKeys.enableOidcAutoCreateUser)) {
      throw new PlatformServiceException(BAD_REQUEST, "Cannot change role for OIDC user.");
    }
    if (Users.Role.SuperAdmin == user.getRole()) {
      throw new PlatformServiceException(BAD_REQUEST, "Cannot change super admin role.");
    }

    Users.Role userRole = null;
    try {
      userRole = Users.Role.valueOf(role);
    } catch (IllegalArgumentException ex) {
      throw new PlatformServiceException(BAD_REQUEST, "Role name provided is not supported");
    }

    if (userRole == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Role name provided is not supported");
    } else if (userRole == Users.Role.SuperAdmin) {
      throw new PlatformServiceException(BAD_REQUEST, "Cannot edit the user role to SuperAdmin");
    }

    user.setRole(userRole);
    user.save();

    boolean useNewAuthz =
        runtimeConfigFactory.globalRuntimeConf().getBoolean("yb.rbac.use_new_authz");
    if (useNewAuthz) {
      LOG.debug("Using new authz RBAC model to edit user role.");
      // Get the built-in role UUID by name.
      Role newRbacRole = Role.getOrBadRequest(customerUUID, role);
      // Need to define all available resources in resource group as default.
      ResourceGroup resourceGroup = ResourceGroup.getSystemDefaultResourceGroup(customerUUID, user);
      // Create a single role binding for the user.
      List<RoleBinding> createdRoleBindings =
          roleBindingUtil.setUserRoleBindings(
              user.getUuid(),
              Arrays.asList(new RoleResourceDefinition(newRbacRole.getRoleUUID(), resourceGroup)),
              RoleBindingType.Custom);

      LOG.info(
          "Changed user '{}' with email '{}' to role '{}', with default role bindings '{}'.",
          user.getUuid(),
          user.getEmail(),
          role,
          createdRoleBindings.toString());
    }

    auditService()
        .createAuditEntryWithReqBody(
            request, Audit.TargetType.User, userUUID.toString(), Audit.ActionType.ChangeUserRole);
    return YBPSuccess.empty();
  }

  /**
   * PUT endpoint for changing the password of an existing user.
   *
   * @return JSON response on whether role change was successful or not.
   */
  @ApiOperation(
      value = "Change a user's password",
      nickname = "updateUserPassword",
      response = YBPSuccess.class)
  @ApiImplicitParams({
    @ApiImplicitParam(
        name = "Users",
        value = "User data containing the new password",
        required = true,
        dataType = "com.yugabyte.yw.forms.UserRegisterFormData",
        paramType = "body")
  })
  @AuthzPath(
      @RequiredPermissionOnResource(
          requiredPermission =
              @PermissionAttribute(
                  resourceType = ResourceType.USER,
                  action = Action.UPDATE_PROFILE),
          resourceLocation = @Resource(path = Util.USERS, sourceType = SourceType.ENDPOINT)))
  public Result changePassword(UUID customerUUID, UUID userUUID, Http.Request request) {
    Users user = Users.getOrBadRequest(customerUUID, userUUID);
    if (UserType.ldap == user.getUserType()) {
      throw new PlatformServiceException(BAD_REQUEST, "Can't change password for LDAP user.");
    }

    if (!checkUpdateProfileAccessForPasswordChange(userUUID, request)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Only the User can change his/her own password.");
    }

    Form<UserRegisterFormData> form =
        formFactory.getFormDataOrBadRequest(request, UserRegisterFormData.class);

    UserRegisterFormData formData = form.get();
    passwordPolicyService.checkPasswordPolicy(customerUUID, formData.getPassword());
    if (formData.getEmail().equals(user.getEmail())) {
      if (formData.getPassword().equals(formData.getConfirmPassword())) {
        user.setPassword(formData.getPassword());
        user.save();
        auditService()
            .createAuditEntry(
                request,
                Audit.TargetType.User,
                userUUID.toString(),
                Audit.ActionType.ChangeUserPassword);
        return YBPSuccess.empty();
      }
    }
    throw new PlatformServiceException(BAD_REQUEST, "Invalid user credentials.");
  }

  private Users getLoggedInUser(Http.Request request) {
    Users user = tokenAuthenticator.getCurrentAuthenticatedUser(request);
    return user;
  }

  private boolean checkUpdateProfileAccessForPasswordChange(UUID userUUID, Http.Request request) {
    Users user = getLoggedInUser(request);

    if (user == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Unable To Authenticate User");
    }
    return userUUID.equals(user.getUuid());
  }

  /**
   * PUT endpoint for updating the user profile.
   *
   * @return JSON response of the updated User.
   */
  @ApiOperation(
      value = "Update a user's profile",
      nickname = "UpdateUserProfile",
      response = Users.class)
  @ApiImplicitParams({
    @ApiImplicitParam(
        name = "Users",
        value = "User data in profile to be updated",
        required = true,
        dataType = "com.yugabyte.yw.forms.UserProfileFormData",
        paramType = "body")
  })
  @AuthzPath(
      @RequiredPermissionOnResource(
          requiredPermission =
              @PermissionAttribute(
                  resourceType = ResourceType.USER,
                  action = Action.UPDATE_PROFILE),
          resourceLocation = @Resource(path = Util.USERS, sourceType = SourceType.ENDPOINT)))
  public Result updateProfile(UUID customerUUID, UUID userUUID, Http.Request request) {

    Users user = Users.getOrBadRequest(customerUUID, userUUID);
    Form<UserProfileFormData> form =
        formFactory.getFormDataOrBadRequest(request, UserProfileFormData.class);

    UserProfileFormData formData = form.get();
    boolean useNewAuthz =
        runtimeConfigFactory.globalRuntimeConf().getBoolean("yb.rbac.use_new_authz");
    Users loggedInUser = getLoggedInUser(request);

    // Password validation for both old RBAC and new RBAC is same.
    if (StringUtils.isNotEmpty(formData.getPassword())) {
      if (UserType.ldap == user.getUserType()) {
        throw new PlatformServiceException(BAD_REQUEST, "Can't change password for LDAP user.");
      }

      if (!checkUpdateProfileAccessForPasswordChange(userUUID, request)) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Only the User can change his/her own password.");
      }

      passwordPolicyService.checkPasswordPolicy(customerUUID, formData.getPassword());
      if (!formData.getPassword().equals(formData.getConfirmPassword())) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Password and confirm password do not match.");
      }
      user.setPassword(formData.getPassword());
    }

    if (useNewAuthz) {
      // Timezone validation for new RBAC.
      // No explicit validation required as the API is already authorized with the required
      // permissions.
      if (formData.getTimezone() != null && !formData.getTimezone().equals(user.getTimezone())) {
        user.setTimezone(formData.getTimezone());
      }
    } else {
      // Timezone validation for old RBAC.
      if (loggedInUser.getRole().compareTo(Users.Role.BackupAdmin) <= 0
          && !formData.getTimezone().equals(user.getTimezone())) {
        throw new PlatformServiceException(
            BAD_REQUEST, "ConnectOnly/ReadOnly/BackupAdmin users can't change their timezone");
      }

      // Set the timezone if edited to a correct value.
      if (formData.getTimezone() != null && !formData.getTimezone().equals(user.getTimezone())) {
        user.setTimezone(formData.getTimezone());
      }
    }

    if (useNewAuthz) {
      // Role validation for new RBAC.
      if (formData.getRole() != null && formData.getRole() != user.getRole()) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            "Wrong API to change users role when new authz is enabled. "
                + "Use setRoleBinding API(/customers/:cUUID/rbac/role_binding/:userUUID) instead");
      }
    } else {
      // Role validation for old RBAC.
      if (loggedInUser.getRole().compareTo(Users.Role.BackupAdmin) <= 0
          && formData.getRole() != user.getRole()) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            "ConnectOnly/ReadOnly/BackupAdmin users can't change their assigned roles");
      }
      if (formData.getRole() != user.getRole()) {
        if (Users.Role.SuperAdmin == user.getRole()) {
          throw new PlatformServiceException(BAD_REQUEST, "Can't change super admin role.");
        }

        if (formData.getRole() == Users.Role.SuperAdmin) {
          throw new PlatformServiceException(
              BAD_REQUEST, "Can't Assign the role of SuperAdmin to another user.");
        }

        if (user.getUserType() == UserType.ldap && user.isLdapSpecifiedRole() == true) {
          throw new PlatformServiceException(BAD_REQUEST, "Cannot change role for LDAP user.");
        }
        user.setRole(formData.getRole());
      }
    }
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.User,
            userUUID.toString(),
            Audit.ActionType.Update,
            Json.toJson(formData));
    user.save();
    return ok(Json.toJson(user));
  }
}
