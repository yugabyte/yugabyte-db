// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.google.inject.Inject;
import com.yugabyte.yw.common.ApiResponse;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.forms.CustomerLoginFormData;
import com.yugabyte.yw.forms.CustomerRegisterFormData;
import com.yugabyte.yw.models.Customer;

import org.apache.commons.io.input.ReversedLinesFileReader;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import play.Configuration;
import play.Environment;
import play.data.Form;
import play.data.FormFactory;
import play.libs.Json;
import play.mvc.*;

import java.io.File;
import java.io.IOException;
import java.util.UUID;
import javax.persistence.PersistenceException;

public class SessionController extends Controller {
  public static final Logger LOG = LoggerFactory.getLogger(SessionController.class);

  @Inject
  FormFactory formFactory;

  @Inject
  Configuration appConfig;

  @Inject
  ConfigHelper configHelper;


  @Inject
  Environment environment;


  public static final String AUTH_TOKEN = "authToken";
  public static final String API_TOKEN = "apiToken";
  public static final String CUSTOMER_UUID = "customerUUID";
  private static final Integer FOREVER = 2147483647;

  public Result login() {
    Form<CustomerLoginFormData> formData = formFactory.form(CustomerLoginFormData.class).bindFromRequest();
    ObjectNode responseJson = Json.newObject();

    if (formData.hasErrors()) {
      responseJson.set("error", formData.errorsAsJson());
      return badRequest(responseJson);
    }

    CustomerLoginFormData data = formData.get();
    Customer cust = Customer.authWithPassword(data.email.toLowerCase(), data.password);

    if (cust == null) {
      responseJson.put("error", "Invalid Customer Credentials");
      return unauthorized(responseJson);
    }

    String authToken = cust.createAuthToken();
    ObjectNode authTokenJson = Json.newObject();
    authTokenJson.put(AUTH_TOKEN, authToken);
    authTokenJson.put(CUSTOMER_UUID, cust.uuid.toString());
    response().setCookie(Http.Cookie.builder(AUTH_TOKEN, authToken).withSecure(ctx().request().secure()).build());
    return ok(authTokenJson);
  }

  @With(TokenAuthenticator.class)
  public Result api_token(UUID customerUUID) {
    Customer cust = (Customer) Http.Context.current().args.get("customer");

    if (cust == null) {
      return ApiResponse.error(BAD_REQUEST, "Could not find customer from given credentials.");
    }

    String apiToken = cust.upsertApiToken();
    ObjectNode apiTokenJson = Json.newObject();
    apiTokenJson.put(API_TOKEN, apiToken);
    response().setCookie(Http.Cookie.builder(API_TOKEN, apiToken).withSecure(ctx().request().secure()).withMaxAge(FOREVER).build());
    return ok(apiTokenJson);
  }

  public Result register() {
    Form<CustomerRegisterFormData> formData = formFactory.form(CustomerRegisterFormData.class).bindFromRequest();

    if (formData.hasErrors()) {
      return ApiResponse.error(BAD_REQUEST, formData.errorsAsJson());
    }
    boolean multiTenant = appConfig.getBoolean("yb.multiTenant", false);
    int customerCount = Customer.find.all().size();
    if (!multiTenant && customerCount >= 1) {
      return ApiResponse.error(BAD_REQUEST, "Cannot register multiple accounts in Single tenancy.");
    }

    CustomerRegisterFormData data = formData.get();
    try {
      Customer cust = Customer.create(data.code, data.name, data.email, data.password);
      if (cust == null) {
        return ApiResponse.error(INTERNAL_SERVER_ERROR, "Unable to register the customer");
      }

      String authToken = cust.createAuthToken();
      ObjectNode authTokenJson = Json.newObject();
      authTokenJson.put(AUTH_TOKEN, authToken);
      authTokenJson.put(CUSTOMER_UUID, cust.uuid.toString());
      response().setCookie(Http.Cookie.builder(AUTH_TOKEN, authToken).withSecure(ctx().request().secure()).build());
      return ok(authTokenJson);
    } catch (PersistenceException pe) {
      return ApiResponse.error(INTERNAL_SERVER_ERROR, "Customer already registered.");
    }
  }

  @With(TokenAuthenticator.class)
  public Result logout() {
    response().discardCookie(AUTH_TOKEN);
    Customer cust = (Customer) Http.Context.current().args.get("customer");
    if (cust != null) {
      cust.deleteAuthToken();
    }
    return ok();
  }

  public Result getUITheme() {
    try {
      return Results.ok(environment.resourceAsStream("theme/theme.css"));
    } catch (NullPointerException ne) {
      return ApiResponse.error(BAD_REQUEST, "Theme file doesn't exists.");
    }
  }

  public Result customerCount() {
    int customerCount = Customer.find.all().size();
    ObjectNode response = Json.newObject();
    response.put("count", customerCount);
    return ApiResponse.success(response);
  }

  public Result appVersion() {
    return ApiResponse.success(configHelper.getConfig(ConfigHelper.ConfigType.SoftwareVersion));
  }

  @With(TokenAuthenticator.class)
  public Result getLogs(Integer maxLines) {
    String logDir = appConfig.getString("application.home", ".");
    File file = new File(String.format("%s/logs/application.log", logDir));
    // TODO(bogdan): This is not really pagination friendly as it re-reads everything all the time..
    // TODO(bogdan): Need to figure out if there's a rotation-friendly log-reader..
    try {
      ReversedLinesFileReader reader = new ReversedLinesFileReader(file);
      int index = 0;
      ObjectNode result = Json.newObject();
      ArrayNode lines = Json.newArray();
      while (index++ < maxLines) {
        String line = reader.readLine();
        if (line != null) {
          lines.add(line);
        } else {
          // No more lines.
          break;
        }
      }
      result.put("lines", lines);

      return ApiResponse.success(result);
    } catch (IOException ex) {
      LOG.error("Log file open failed.", ex);
      return ApiResponse.error(INTERNAL_SERVER_ERROR, "Could not open log file with error " + ex.getMessage());
    }
  }
}
