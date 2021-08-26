// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.models.Users.Role;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.inject.Inject;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.password.PasswordPolicyService;
import com.yugabyte.yw.forms.UserRegisterFormData;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Users;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiImplicitParam;
import io.swagger.annotations.ApiImplicitParams;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.io.IOException;
import java.io.InputStream;
import java.util.List;
import java.util.UUID;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.Environment;
import play.data.Form;
import play.libs.Json;
import play.mvc.Result;

@Api(
    value = "User management",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class UsersController extends AuthenticatedController {

  public static final Logger LOG = LoggerFactory.getLogger(UsersController.class);
  @Inject protected RuntimeConfigFactory runtimeConfigFactory;

  @Inject Environment environment;

  @Inject PasswordPolicyService passwordPolicyService;

  /**
   * GET endpoint for listing the provider User.
   *
   * @return JSON response with user.
   */
  @ApiOperation(value = "Get a user's details", nickname = "getUserDetails", response = Users.class)
  public Result index(UUID customerUUID, UUID userUUID) {
    Customer.getOrBadRequest(customerUUID);
    Users user = Users.getOrBadRequest(userUUID);
    return PlatformResults.withData(user);
  }

  /**
   * GET endpoint for listing all available Users for a customer
   *
   * @return JSON response with users belonging to the customer.
   */
  @ApiOperation(
      value = "List all users",
      nickname = "listUsers",
      response = Users.class,
      responseContainer = "List")
  public Result list(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);
    List<Users> users = Users.getAll(customerUUID);
    return PlatformResults.withData(users);
  }

  /**
   * POST endpoint for creating new Users.
   *
   * @return JSON response of newly created user.
   */
  @ApiOperation(value = "Create a user", nickname = "createUser", response = Users.class)
  @ApiImplicitParams({
    @ApiImplicitParam(
        name = "User",
        value = "Details of the new user",
        required = true,
        dataType = "com.yugabyte.yw.forms.UserRegisterFormData",
        paramType = "body")
  })
  public Result create(UUID customerUUID) {

    Customer.getOrBadRequest(customerUUID);
    Form<UserRegisterFormData> form =
        formFactory.getFormDataOrBadRequest(UserRegisterFormData.class);

    UserRegisterFormData formData = form.get();
    passwordPolicyService.checkPasswordPolicy(customerUUID, formData.getPassword());
    Users user =
        Users.create(formData.getEmail(), formData.getPassword(), formData.getRole(), customerUUID);
    updateFeatures(user);
    auditService().createAuditEntry(ctx(), request(), Json.toJson(formData));
    return PlatformResults.withData(user);
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
  public Result delete(UUID customerUUID, UUID userUUID) {
    Customer.getOrBadRequest(customerUUID);
    Users user = Users.getOrBadRequest(userUUID);
    if (!user.customerUUID.equals(customerUUID)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "User UUID %s does not belong to customer %s",
              userUUID.toString(), customerUUID.toString()));
    }
    if (user.getIsPrimary()) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "Cannot delete primary user %s for customer %s",
              userUUID.toString(), customerUUID.toString()));
    }
    if (user.delete()) {
      auditService().createAuditEntry(ctx(), request());
      return YBPSuccess.empty();
    } else {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Unable to delete user UUID: " + userUUID);
    }
  }

  /**
   * PUT endpoint for changing the role of an existing user.
   *
   * @return JSON response on whether role change was successful or not.
   */
  @ApiOperation(
      value = "Change a user's role",
      nickname = "updateUserRole",
      response = YBPSuccess.class)
  public Result changeRole(UUID customerUUID, UUID userUUID, String role) {
    Customer.getOrBadRequest(customerUUID);
    Users user = Users.getOrBadRequest(userUUID);
    if (!user.customerUUID.equals(customerUUID)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "User UUID %s does not belong to customer %s",
              userUUID.toString(), customerUUID.toString()));
    }
    if (Role.SuperAdmin == user.getRole()) {
      throw new PlatformServiceException(BAD_REQUEST, "Can't change super admin role.");
    }
    user.setRole(Role.valueOf(role));
    user.save();
    updateFeatures(user);
    auditService().createAuditEntry(ctx(), request());
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
  public Result changePassword(UUID customerUUID, UUID userUUID) {
    Customer.getOrBadRequest(customerUUID);
    Users user = Users.getOrBadRequest(userUUID);
    if (!user.customerUUID.equals(customerUUID)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "User UUID %s does not belong to customer %s",
              userUUID.toString(), customerUUID.toString()));
    }

    Form<UserRegisterFormData> form =
        formFactory.getFormDataOrBadRequest(UserRegisterFormData.class);

    UserRegisterFormData formData = form.get();
    passwordPolicyService.checkPasswordPolicy(customerUUID, formData.getPassword());
    if (formData.getEmail().equals(user.email)) {
      if (formData.getPassword().equals(formData.getConfirmPassword())) {
        user.setPassword(formData.getPassword());
        user.save();
        return YBPSuccess.empty();
      }
    }
    throw new PlatformServiceException(BAD_REQUEST, "Invalid user credentials.");
  }

  private void updateFeatures(Users user) {
    Customer customer = Customer.getOrBadRequest(user.customerUUID);
    try {
      String configFile = user.getRole().getFeaturesFile();
      if (customer.code.equals("cloud")) {
        configFile = "cloudFeatureConfig.json";
      }
      if (configFile == null) {
        user.setFeatures(Json.newObject());
        user.save();
        return;
      }
      InputStream featureStream = environment.resourceAsStream(configFile);
      ObjectMapper mapper = new ObjectMapper();
      JsonNode features = mapper.readTree(featureStream);
      user.upsertFeatures(features);
    } catch (IOException e) {
      LOG.error("Failed to parse sample feature config file for OSS mode.");
    }
  }
}
