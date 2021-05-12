// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import com.yugabyte.yw.common.ApiResponse;
import com.yugabyte.yw.common.ValidatingFormFactory;
import com.yugabyte.yw.common.password.PasswordPolicyService;
import com.yugabyte.yw.forms.UserRegisterFormData;
import com.yugabyte.yw.forms.YWResults;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Users;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.Environment;
import play.data.Form;
import play.libs.Json;
import play.mvc.Result;

import java.io.IOException;
import java.io.InputStream;
import java.util.List;
import java.util.UUID;

import static com.yugabyte.yw.models.Users.Role;

public class UsersController extends AuthenticatedController {

  public static final Logger LOG = LoggerFactory.getLogger(UsersController.class);

  @Inject
  ValidatingFormFactory formFactory;

  @Inject
  Environment environment;

  @Inject
  PasswordPolicyService passwordPolicyService;

  /**
   * GET endpoint for listing the provider User.
   * @return JSON response with user.
   */
  public Result index(UUID customerUUID, UUID userUUID) {
    Customer customer = Customer.get(customerUUID);
    if (customer == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Customer UUID: " + customerUUID);
    }
    try {
      Users user = Users.get(userUUID);
      return ApiResponse.success(user);
    } catch (Exception e) {
      return ApiResponse.error(INTERNAL_SERVER_ERROR, "Unable to fetch user.");
    }
  }

  /**
   * GET endpoint for listing all available Users for a customer
   * @return JSON response with users belonging to the customer.
   */
  public Result list(UUID customerUUID) {
    Customer customer = Customer.get(customerUUID);
    if (customer == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Customer UUID: " + customerUUID);
    }
    try {
      List<Users> users = Users.getAll(customerUUID);
      return ApiResponse.success(users);
    } catch (Exception e) {
      return ApiResponse.error(INTERNAL_SERVER_ERROR, "Unable to fetch users.");
    }
  }

  /**
   * POST endpoint for creating new Users.
   * @return JSON response of newly created user.
   */
  public Result create(UUID customerUUID) {

    Customer customer = Customer.get(customerUUID);
    if (customer == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Customer UUID: " + customerUUID);
    }
    Form<UserRegisterFormData> form = formFactory
      .getFormDataOrBadRequest(UserRegisterFormData.class);

    UserRegisterFormData formData = form.get();
    Result passwordCheckResult = passwordPolicyService
      .checkPasswordPolicy(customerUUID, formData.getPassword());
    if (passwordCheckResult != null) {
      return passwordCheckResult;
    }
    Users user;
    try {
      user = Users.create(formData.getEmail(), formData.getPassword(),
        formData.getRole(), customerUUID);
      updateFeatures(user);
    } catch (Exception e) {
      return ApiResponse.error(INTERNAL_SERVER_ERROR, "Could not create user");
    }
    auditService().createAuditEntry(ctx(), request(), Json.toJson(formData));
    return ApiResponse.success(user);

  }

  /**
   * DELETE endpoint for deleting an existing user.
   * @return JSON response on whether or not delete user was successful or not.
   */
  public Result delete(UUID customerUUID, UUID userUUID) {
    Customer customer = Customer.get(customerUUID);
    if (customer == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Customer UUID:" + customerUUID);
    }
    Users user = Users.get(userUUID);
    if (user == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid User UUID:" + userUUID);
    }
    if (!user.customerUUID.equals(customerUUID)) {
      return ApiResponse.error(BAD_REQUEST,
          String.format("User UUID %s does not belong to customer %s",
                        userUUID.toString(), customerUUID.toString()));
    }
    if (user.getIsPrimary()) {
      return ApiResponse.error(BAD_REQUEST,
          String.format("Cannot delete primary user %s for customer %s",
                        userUUID.toString(), customerUUID.toString()));
    }
    if (user.delete()) {
      ObjectNode responseJson = Json.newObject();
      responseJson.put("success", true);
      auditService().createAuditEntry(ctx(), request());
      return ApiResponse.success(responseJson);
    } else {
      return ApiResponse.error(INTERNAL_SERVER_ERROR, "Unable to delete User UUID: " + userUUID);
    }
  }

  /**
   * PUT endpoint for changing the role of an existing user.
   * @return JSON response on whether role change was successful or not.
   */
  public Result changeRole(UUID customerUUID, UUID userUUID) {
    Customer customer = Customer.get(customerUUID);
    if (customer == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Customer UUID:" + customerUUID);
    }
    Users user = Users.get(userUUID);
    if (user == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid User UUID:" + userUUID);
    }
    if (!user.customerUUID.equals(customerUUID)) {
      return ApiResponse.error(BAD_REQUEST,
          String.format("User UUID %s does not belong to customer %s",
                        userUUID.toString(), customerUUID.toString()));
    }
    if (request().getQueryString("role") != null) {
        String role = request().getQueryString("role");
        if (Role.SuperAdmin == user.getRole()) {
          return ApiResponse.error(BAD_REQUEST, "Can't change super admin role.");
        }
        try {
          user.setRole(Role.valueOf(role));
          user.save();
          updateFeatures(user);
        } catch (Exception e) {
          return ApiResponse.error(BAD_REQUEST, "Incorrect Role Specified");
        }
    } else {
      return ApiResponse.error(BAD_REQUEST, "Invalid Request");
    }
    auditService().createAuditEntry(ctx(), request());
    return YWResults.YWSuccess.empty();
  }

  /**
   * PUT endpoint for changing the password of an existing user.
   * @return JSON response on whether role change was successful or not.
   */
  public Result changePassword(UUID customerUUID, UUID userUUID) {
    Customer customer = Customer.get(customerUUID);
    if (customer == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Customer UUID:" + customerUUID);
    }
    Users user = Users.get(userUUID);
    if (user == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid User UUID:" + userUUID);
    }
    if (!user.customerUUID.equals(customerUUID)) {
      return ApiResponse.error(BAD_REQUEST,
          String.format("User UUID %s does not belong to customer %s",
                        userUUID.toString(), customerUUID.toString()));
    }

    Form<UserRegisterFormData> form = formFactory
      .getFormDataOrBadRequest(UserRegisterFormData.class);

    UserRegisterFormData formData = form.get();
    Result passwordCheckResult = passwordPolicyService
      .checkPasswordPolicy(customerUUID, formData.getPassword());
    if (passwordCheckResult != null) {
      return passwordCheckResult;
    }
    if (formData.getEmail().equals(user.email)) {
      if (formData.getPassword().equals(formData.getConfirmPassword())) {
        user.setPassword(formData.getPassword());
        user.save();
        return YWResults.YWSuccess.empty();
      }
    }
    return ApiResponse.error(BAD_REQUEST, "Invalid User Credentials.");
  }

  private void updateFeatures(Users user) {
    try {
      Customer customer = Customer.get(user.customerUUID);
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
