// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.models.Users.Role;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.inject.Inject;
import com.yugabyte.yw.common.YWServiceException;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.password.PasswordPolicyService;
import com.yugabyte.yw.forms.UserRegisterFormData;
import com.yugabyte.yw.forms.YWResults;
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

@Api(value = "Users", authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
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
  @ApiOperation(value = "User detail by UUID", response = Users.class, nickname = "usersDetail")
  public Result index(UUID customerUUID, UUID userUUID) {
    Customer.getOrBadRequest(customerUUID);
    Users user = Users.getOrBadRequest(userUUID);
    return YWResults.withData(user);
  }

  /**
   * GET endpoint for listing all available Users for a customer
   *
   * @return JSON response with users belonging to the customer.
   */
  @ApiOperation(
      value = "List of Users",
      response = Users.class,
      responseContainer = "List",
      nickname = "ListOfUsers")
  public Result list(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);
    List<Users> users = Users.getAll(customerUUID);
    return YWResults.withData(users);
  }

  /**
   * POST endpoint for creating new Users.
   *
   * @return JSON response of newly created user.
   */
  @ApiOperation(value = "Create User", response = Users.class, nickname = "createUsers")
  @ApiImplicitParams({
    @ApiImplicitParam(
        name = "User",
        value = "Users data to be created",
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
    return YWResults.withData(user);
  }

  /**
   * DELETE endpoint for deleting an existing user.
   *
   * @return JSON response on whether or not delete user was successful or not.
   */
  @ApiOperation(
      value = "Delete customer",
      response = YWResults.YWSuccess.class,
      nickname = "deleteUsers")
  public Result delete(UUID customerUUID, UUID userUUID) {
    Customer.getOrBadRequest(customerUUID);
    Users user = Users.getOrBadRequest(userUUID);
    if (!user.customerUUID.equals(customerUUID)) {
      throw new YWServiceException(
          BAD_REQUEST,
          String.format(
              "User UUID %s does not belong to customer %s",
              userUUID.toString(), customerUUID.toString()));
    }
    if (user.getIsPrimary()) {
      throw new YWServiceException(
          BAD_REQUEST,
          String.format(
              "Cannot delete primary user %s for customer %s",
              userUUID.toString(), customerUUID.toString()));
    }
    if (user.delete()) {
      auditService().createAuditEntry(ctx(), request());
      return YWResults.YWSuccess.empty();
    } else {
      throw new YWServiceException(
          INTERNAL_SERVER_ERROR, "Unable to delete User UUID: " + userUUID);
    }
  }

  /**
   * PUT endpoint for changing the role of an existing user.
   *
   * @return JSON response on whether role change was successful or not.
   */
  @ApiOperation(value = "Change role of user by UUID", response = YWResults.YWSuccess.class)
  public Result changeRole(UUID customerUUID, UUID userUUID, String role) {
    Customer.getOrBadRequest(customerUUID);
    Users user = Users.getOrBadRequest(userUUID);
    if (!user.customerUUID.equals(customerUUID)) {
      throw new YWServiceException(
          BAD_REQUEST,
          String.format(
              "User UUID %s does not belong to customer %s",
              userUUID.toString(), customerUUID.toString()));
    }
    if (Role.SuperAdmin == user.getRole()) {
      throw new YWServiceException(BAD_REQUEST, "Can't change super admin role.");
    }
    user.setRole(Role.valueOf(role));
    user.save();
    updateFeatures(user);
    auditService().createAuditEntry(ctx(), request());
    return YWResults.YWSuccess.empty();
  }

  /**
   * PUT endpoint for changing the password of an existing user.
   *
   * @return JSON response on whether role change was successful or not.
   */
  @ApiOperation(value = "Chnage password of User", response = YWResults.YWSuccess.class)
  @ApiImplicitParams({
    @ApiImplicitParam(
        name = "Users",
        value = "Users data to be updated",
        required = true,
        dataType = "com.yugabyte.yw.forms.UserRegisterFormData",
        paramType = "body")
  })
  public Result changePassword(UUID customerUUID, UUID userUUID) {
    Customer.getOrBadRequest(customerUUID);
    Users user = Users.getOrBadRequest(userUUID);
    if (!user.customerUUID.equals(customerUUID)) {
      throw new YWServiceException(
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
        return YWResults.YWSuccess.empty();
      }
    }
    throw new YWServiceException(BAD_REQUEST, "Invalid User Credentials.");
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
