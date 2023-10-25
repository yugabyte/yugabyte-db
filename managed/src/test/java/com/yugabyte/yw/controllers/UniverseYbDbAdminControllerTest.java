/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertAuditEntry;
import static com.yugabyte.yw.common.AssertHelper.assertBadRequest;
import static com.yugabyte.yw.common.AssertHelper.assertErrorResponse;
import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static org.junit.Assert.assertEquals;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.when;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableMap;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.controllers.handlers.UniverseYbDbAdminHandler;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.UUID;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import org.apache.commons.lang3.StringUtils;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mockito;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;
import play.test.Helpers;

@RunWith(JUnitParamsRunner.class)
public class UniverseYbDbAdminControllerTest extends UniverseControllerTestBase {

  @Test
  public void testRunQueryWithInvalidUniverse() throws Exception {
    // Setting insecure mode.
    ConfigHelper configHelper = new ConfigHelper();
    configHelper.loadConfigToDB(
        ConfigHelper.ConfigType.Security, ImmutableMap.of("level", "insecure"));

    Customer c2 = ModelFactory.testCustomer("tc2", "Test Customer 2");
    Universe u = createUniverse(c2.getId());
    ObjectNode bodyJson = Json.newObject();
    String url =
        "/api/customers/" + customer.getUuid() + "/universes/" + u.getUniverseUUID() + "/run_query";
    Http.RequestBuilder request =
        Helpers.fakeRequest("POST", url)
            .header("X-AUTH-TOKEN", authToken)
            .bodyJson(bodyJson)
            .header("Origin", "https://" + UniverseYbDbAdminHandler.LEARN_DOMAIN_NAME);
    Result result = routeWithYWErrHandler(request);
    assertBadRequest(
        result,
        String.format(
            "Universe %s doesn't belong to Customer %s", u.getUniverseUUID(), customer.getUuid()));
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  // @formatter:off
  @Parameters({
    // cloud customer, normal username
    "true,  foo, foo, baz, baz, true, true,",
    // not cloud customer
    "false, foo, foo, baz, baz, false, false, Invalid Customer type",
    // cloud customer, double quotes in username
    "true,  foo, foo, ba\"z, baz, false, false, Invalid username",
    // cloud customer, username surrounded by double quotes
    "true,  foo, foo, \"baz\", baz, true, true,",
    // cloud customer, username surrounded by double quotes + double quotes inside
    "true,  foo, foo, \"ba\"z\", baz, false, false, Invalid username",
    // cloud customer, backslash in username
    "true,  foo, foo, ba\\z, baz, true, true,",
    // cloud customer, only YSQL user
    "true, foo,, baz, baz, true, false,",
    // cloud customer, only YCQL user
    "true,, foo, baz, baz, false, true,",
    // cloud customer, neither YSQL nor YCQL user
    "true,,, baz, baz, false, false, Need to provide YSQL and/or YCQL username.",
  })
  // @formatter:on
  public void testCreateUserInDB(
      boolean isCloudCustomer,
      String ysqlAdminUsername,
      String ycqlAdminUsername,
      String username,
      String password,
      boolean ysqlProcessed,
      boolean ycqlProcessed,
      String responseError) {
    Universe u = createUniverse(customer.getId());
    if (isCloudCustomer) {
      when(mockRuntimeConfig.getBoolean("yb.cloud.enabled")).thenReturn(true);
    }
    ObjectNode bodyJson =
        Json.newObject()
            .put("ycqlAdminUsername", ycqlAdminUsername)
            .put("ysqlAdminUsername", ysqlAdminUsername)
            .put("ycqlAdminPassword", "bar")
            .put("ysqlAdminPassword", "bar")
            .put("dbName", "test")
            .put("username", username)
            .put("password", password);
    String url =
        "/api/customers/"
            + customer.getUuid()
            + "/universes/"
            + u.getUniverseUUID()
            + "/create_db_credentials";
    if (ycqlProcessed || ysqlProcessed) {
      Result result = doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson);
      Mockito.verify(mockYcqlQueryExecutor, times(ycqlProcessed ? 1 : 0)).createUser(any(), any());
      Mockito.verify(mockYsqlQueryExecutor, times(ysqlProcessed ? 1 : 0)).createUser(any(), any());
      assertOk(result);
      assertAuditEntry(1, customer.getUuid());
    } else {
      Result result =
          assertPlatformException(
              () -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson));
      Mockito.verifyNoMoreInteractions(mockYcqlQueryExecutor, mockYsqlQueryExecutor);
      assertErrorResponse(result, responseError);
      assertAuditEntry(0, customer.getUuid());
    }
  }

  @Test
  // @formatter:off
  @Parameters({
    // cloud customer, normal username
    "true,  baz, baz, baz, baz, true, true,",
    // not cloud customer
    "false, baz, baz, baz, baz, false, false, Invalid Customer type",
    // cloud customer, double quotes in username
    "true,  ba\"z, baz, baz, baz, false, false, Invalid username",
    // cloud customer, usernames surrounded by double quotes
    "true,  \"baz\", baz, \"baz\", baz, true, true,",
    // cloud customer, double quotes in username which surrounded by double quotes
    "true,  \"ba\"z\", baz, baz, baz, false, false, Invalid username",
    // cloud customer, backslash in username
    "true,  ba\\z, baz, baz, baz, true, true,",
    // cloud customer, only YSQL user
    "true,  baz, baz,,, true, false,",
    // cloud customer, only YSQL user, YCQL user is set as ""
    "true,  baz, baz, \"\", baz, true, false,",
    // cloud customer, only YCQL user
    "true,,, baz, baz, false, true,",
    // cloud customer, only YCQL user, YSQL user is set as ""
    "true, \"\", baz, baz, baz, false, true,",
    // cloud customer, neither YSQL nor YCQL user
    "true,,,,, false, false, Need to provide YSQL and/or YCQL username.",
    // non-cloud customer but change in password of default user
    "false, yugabyte, Admin@321, cassandra, Admin@321, true, true,"
  })
  // @formatter:on
  public void testSetDatabaseCredentials(
      boolean isCloudCustomer,
      String ysqlAdminUsername,
      String ysqlPassword,
      String ycqlAdminUsername,
      String ycqlPassword,
      boolean ysqlProcessed,
      boolean ycqlProcessed,
      String responseError) {
    Universe u = createUniverse(customer.getId());
    UniverseDefinitionTaskParams details = u.getUniverseDetails();
    UniverseDefinitionTaskParams.UserIntent userIntent = details.getPrimaryCluster().userIntent;
    userIntent.enableYSQLAuth = true;
    userIntent.enableYCQLAuth = true;
    details.upsertPrimaryCluster(userIntent, null);
    u.setUniverseDetails(details);
    u.save();
    if (isCloudCustomer) {
      when(mockRuntimeConfig.getBoolean("yb.cloud.enabled")).thenReturn(true);
    }
    ObjectNode bodyJson =
        Json.newObject()
            .put("ycqlAdminUsername", ycqlAdminUsername)
            .put("ysqlAdminUsername", ysqlAdminUsername)
            .put("ycqlCurrAdminPassword", "Admin@123")
            .put("ysqlCurrAdminPassword", "Admin@123")
            .put("ycqlAdminPassword", ycqlPassword)
            .put("ysqlAdminPassword", ysqlPassword)
            .put("dbName", "test");
    String url =
        "/api/customers/"
            + customer.getUuid()
            + "/universes/"
            + u.getUniverseUUID()
            + "/update_db_credentials";
    if (ycqlProcessed || ysqlProcessed) {
      Result result = doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson);
      Mockito.verify(mockYcqlQueryExecutor, times(ycqlProcessed ? 1 : 0))
          .updateAdminPassword(any(), any());
      Mockito.verify(mockYsqlQueryExecutor, times(ysqlProcessed ? 1 : 0))
          .updateAdminPassword(any(), any());
      assertOk(result);
      assertAuditEntry(1, customer.getUuid());
    } else {
      Result result =
          assertPlatformException(
              () -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson));
      Mockito.verifyNoMoreInteractions(mockYcqlQueryExecutor, mockYsqlQueryExecutor);
      assertErrorResponse(result, responseError);
      assertAuditEntry(0, customer.getUuid());
    }
  }

  @Test
  public void testSetOldDatabaseCredentials() {
    Universe u = createUniverse(customer.getId());
    UniverseDefinitionTaskParams details = u.getUniverseDetails();
    UniverseDefinitionTaskParams.UserIntent userIntent = details.getPrimaryCluster().userIntent;
    userIntent.enableYSQLAuth = true;
    userIntent.enableYCQLAuth = true;
    details.upsertPrimaryCluster(userIntent, null);
    u.setUniverseDetails(details);
    u.save();
    ObjectNode bodyJson =
        Json.newObject()
            .put("ycqlAdminUsername", "cassandra")
            .put("ysqlAdminUsername", "yugabyte")
            .put("ycqlCurrAdminPassword", "Admin@123")
            .put("ysqlCurrAdminPassword", "Admin@123")
            .put("ycqlAdminPassword", "Admin@123")
            .put("ysqlAdminPassword", "Admin@123")
            .put("dbName", "test");
    String url =
        "/api/customers/"
            + customer.getUuid()
            + "/universes/"
            + u.getUniverseUUID()
            + "/update_db_credentials";

    // Test if provided new and old ycql password are same.
    Result result =
        assertPlatformException(
            () -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson));
    Mockito.verifyNoMoreInteractions(mockYcqlQueryExecutor, mockYsqlQueryExecutor);
    assertErrorResponse(result, "Please provide new YCQL password.");
    assertAuditEntry(0, customer.getUuid());
    bodyJson.put("ycqlAdminPassword", "Admin@321");
    // Test if provided new and old ysql password are same.
    result =
        assertPlatformException(
            () -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson));
    Mockito.verifyNoMoreInteractions(mockYcqlQueryExecutor, mockYsqlQueryExecutor);
    assertErrorResponse(result, "Please provide new YSQL password.");
    assertAuditEntry(0, customer.getUuid());
    bodyJson.put("ysqlAdminPassword", "Admin@321");
    // Test that checks passes when new passwords are provided.
    result = doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson);
    Mockito.verify(mockYcqlQueryExecutor, times(1)).updateAdminPassword(any(), any());
    Mockito.verify(mockYsqlQueryExecutor, times(1)).updateAdminPassword(any(), any());
    assertOk(result);
    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  // @formatter:off
  @Parameters({
    // not insecure, wrong origin, wrong ybmode => failure
    "false,,, false",
    // insecure, wrong origin, wrong ybmode => failure
    "true,,, false",
    // insecure, correct origin, wrong ybmode => failure
    "true, https://learn.yugabyte.com,, false",
    // insecure, correct origin, wrong ybmode => failure
    "true, https://learn.yugabyte.com, PLATFORM, false",
    // insecure, correct origin, correct ybmode => success
    "true, https://learn.yugabyte.com, OSS, true",
  })
  // @formatter:on
  public void testRunQuery_ValidPlatform(
      boolean insecure, String origin, String ybmode, boolean isGoodResult) throws Exception {
    Universe u = createUniverse(customer.getId());

    if (insecure) {
      ConfigHelper configHelper = new ConfigHelper();
      configHelper.loadConfigToDB(
          ConfigHelper.ConfigType.Security, ImmutableMap.of("level", "insecure"));
    }
    Config customConfig = mock(Config.class);
    when(customConfig.getString("yb.mode")).thenReturn(ybmode == null ? "" : ybmode);
    UniverseYbDbAdminHandler handler = app.injector().instanceOf(UniverseYbDbAdminHandler.class);
    handler.setAppConfig(customConfig);

    ObjectNode bodyJson =
        Json.newObject().put("query", "select * from product limit 1").put("db_name", "demo");
    when(mockYsqlQueryExecutor.executeQuery(any(), any()))
        .thenReturn(Json.newObject().put("foo", "bar"));

    String url =
        "/api/customers/" + customer.getUuid() + "/universes/" + u.getUniverseUUID() + "/run_query";
    Http.RequestBuilder request =
        Helpers.fakeRequest("POST", url).header("X-AUTH-TOKEN", authToken).bodyJson(bodyJson);
    if (!StringUtils.isEmpty(origin)) {
      request = request.header("Origin", origin);
    }
    Result result = routeWithYWErrHandler(request);

    JsonNode json = Json.parse(contentAsString(result));
    if (isGoodResult) {
      assertOk(result);
      assertEquals("bar", json.get("foo").asText());
      assertAuditEntry(1, customer.getUuid());
    } else {
      assertBadRequest(result, UniverseYbDbAdminHandler.RUN_QUERY_ISNT_ALLOWED);
      assertAuditEntry(0, customer.getUuid());
    }
  }

  @Test
  public void testConfigureYSQL() {
    Universe universe = createUniverse(customer.getId());
    updateUniverseAPIDetails(universe, false, false, true, false);
    ObjectNode bodyJson = Json.newObject().put("enableYSQL", true).put("enableYSQLAuth", false);
    String url =
        "/api/customers/"
            + customer.getUuid()
            + "/universes/"
            + universe.getUniverseUUID()
            + "/configure/ysql";
    Result result =
        assertPlatformException(
            () -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson));
    assertBadRequest(result, "Cannot enable YSQL if it was disabled earlier.");
    updateUniverseAPIDetails(universe, true, false, true, false);
    bodyJson.put("enableYSQL", true).put("ysqlPassword", "Admin@123");
    result =
        assertPlatformException(
            () -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson));
    assertBadRequest(result, "Cannot set password while YSQL auth is disabled.");
    bodyJson.put("enableYSQLAuth", true);
    bodyJson.remove("ysqlPassword");
    result =
        assertPlatformException(
            () -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson));
    assertBadRequest(result, "Required password to configure YSQL auth.");
    bodyJson.put("ysqlPassword", "Admin@123");
    UUID taskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.CreateUniverse);
    when(mockCommissioner.submit(any(), any())).thenReturn(taskUUID);
    result = doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson);
    assertOk(result);
  }

  @Test
  public void testConfigureYCQL() {
    Universe universe = createUniverse(customer.getId());
    updateUniverseAPIDetails(universe, true, true, false, false);
    ObjectNode bodyJson = Json.newObject().put("enableYCQL", false).put("enableYCQLAuth", true);
    String url =
        "/api/customers/"
            + customer.getUuid()
            + "/universes/"
            + universe.getUniverseUUID()
            + "/configure/ycql";
    Result result =
        assertPlatformException(
            () -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson));
    assertBadRequest(result, "Cannot enable YCQL auth when API is disabled.");
    updateUniverseAPIDetails(universe, false, false, true, false);
    bodyJson.put("enableYCQL", true).put("enableYCQLAuth", false).put("ycqlPassword", "Admin@123");
    result =
        assertPlatformException(
            () -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson));
    assertBadRequest(result, "Cannot set password while YCQL auth is disabled.");
    bodyJson.put("enableYCQLAuth", true);
    bodyJson.remove("ycqlPassword");
    result =
        assertPlatformException(
            () -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson));
    assertBadRequest(result, "Required password to configure YCQL auth.");
    bodyJson.put("ycqlPassword", "Admin@123");
    UUID taskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.CreateUniverse);
    when(mockCommissioner.submit(any(), any())).thenReturn(taskUUID);
    result = doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson);
    assertOk(result);
  }

  private void updateUniverseAPIDetails(
      Universe universe,
      boolean enableYSQL,
      boolean enableYSQLAuth,
      boolean enableYCQL,
      boolean enableYCQLAuth) {
    UniverseDefinitionTaskParams details = universe.getUniverseDetails();
    UniverseDefinitionTaskParams.UserIntent userIntent = details.getPrimaryCluster().userIntent;
    userIntent.enableYSQL = enableYSQL;
    userIntent.enableYSQLAuth = enableYSQLAuth;
    userIntent.enableYCQL = enableYCQL;
    userIntent.enableYCQLAuth = enableYCQLAuth;
    details.upsertPrimaryCluster(userIntent, null);
    universe.setUniverseDetails(details);
    universe.save();
  }
}
