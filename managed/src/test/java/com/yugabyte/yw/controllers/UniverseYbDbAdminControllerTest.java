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
import static com.yugabyte.yw.common.FakeApiHelper.doRequestWithAuthTokenAndBody;
import static com.yugabyte.yw.common.FakeApiHelper.routeWithYWErrHandler;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static org.junit.Assert.assertEquals;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.when;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.controllers.handlers.UniverseYbDbAdminHandler;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
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
    // Setting platform type as correct.
    when(mockAppConfig.getString("yb.mode", "PLATFORM")).thenReturn("OSS");
    // Setting insecure mode.
    ConfigHelper configHelper = new ConfigHelper();
    configHelper.loadConfigToDB(
        ConfigHelper.ConfigType.Security, ImmutableMap.of("level", "insecure"));

    Customer c2 = ModelFactory.testCustomer("tc2", "Test Customer 2");
    Universe u = createUniverse(c2.getCustomerId());
    ObjectNode bodyJson = Json.newObject();
    String url = "/api/customers/" + customer.uuid + "/universes/" + u.universeUUID + "/run_query";
    Http.RequestBuilder request =
        Helpers.fakeRequest("POST", url)
            .header("X-AUTH-TOKEN", authToken)
            .bodyJson(bodyJson)
            .header("Origin", "https://" + UniverseYbDbAdminHandler.LEARN_DOMAIN_NAME);
    Result result = routeWithYWErrHandler(request, app);
    assertBadRequest(
        result,
        String.format(
            "Universe UUID: %s doesn't belong to Customer UUID: %s",
            u.universeUUID, customer.uuid));
    assertAuditEntry(0, customer.uuid);
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
    Universe u = createUniverse(customer.getCustomerId());
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
            + customer.uuid
            + "/universes/"
            + u.universeUUID
            + "/create_db_credentials";
    if (ycqlProcessed || ysqlProcessed) {
      Result result = doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson);
      Mockito.verify(mockYcqlQueryExecutor, times(ycqlProcessed ? 1 : 0)).createUser(any(), any());
      Mockito.verify(mockYsqlQueryExecutor, times(ysqlProcessed ? 1 : 0)).createUser(any(), any());
      assertOk(result);
      assertAuditEntry(1, customer.uuid);
    } else {
      Result result =
          assertPlatformException(
              () -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson));
      Mockito.verifyNoMoreInteractions(mockYcqlQueryExecutor, mockYsqlQueryExecutor);
      assertErrorResponse(result, responseError);
      assertAuditEntry(0, customer.uuid);
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
    Universe u = createUniverse(customer.getCustomerId());
    if (isCloudCustomer) {
      when(mockRuntimeConfig.getBoolean("yb.cloud.enabled")).thenReturn(true);
    }
    ObjectNode bodyJson =
        Json.newObject()
            .put("ycqlAdminUsername", ycqlAdminUsername)
            .put("ysqlAdminUsername", ysqlAdminUsername)
            .put("ycqlCurrAdminPassword", "foo")
            .put("ysqlCurrAdminPassword", "foo")
            .put("ycqlAdminPassword", ycqlPassword)
            .put("ysqlAdminPassword", ysqlPassword)
            .put("dbName", "test");
    String url =
        "/api/customers/"
            + customer.uuid
            + "/universes/"
            + u.universeUUID
            + "/update_db_credentials";
    if (ycqlProcessed || ysqlProcessed) {
      Result result = doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson);
      Mockito.verify(mockYcqlQueryExecutor, times(ycqlProcessed ? 1 : 0))
          .updateAdminPassword(any(), any());
      Mockito.verify(mockYsqlQueryExecutor, times(ysqlProcessed ? 1 : 0))
          .updateAdminPassword(any(), any());
      assertOk(result);
      assertAuditEntry(1, customer.uuid);
    } else {
      Result result =
          assertPlatformException(
              () -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson));
      Mockito.verifyNoMoreInteractions(mockYcqlQueryExecutor, mockYsqlQueryExecutor);
      assertErrorResponse(result, responseError);
      assertAuditEntry(0, customer.uuid);
    }
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
    Universe u = createUniverse(customer.getCustomerId());

    if (insecure) {
      ConfigHelper configHelper = new ConfigHelper();
      configHelper.loadConfigToDB(
          ConfigHelper.ConfigType.Security, ImmutableMap.of("level", "insecure"));
    }
    when(mockAppConfig.getString("yb.mode", "PLATFORM")).thenReturn(ybmode == null ? "" : ybmode);

    ObjectNode bodyJson =
        Json.newObject().put("query", "select * from product limit 1").put("db_name", "demo");
    when(mockYsqlQueryExecutor.executeQuery(any(), any()))
        .thenReturn(Json.newObject().put("foo", "bar"));

    String url = "/api/customers/" + customer.uuid + "/universes/" + u.universeUUID + "/run_query";
    Http.RequestBuilder request =
        Helpers.fakeRequest("POST", url).header("X-AUTH-TOKEN", authToken).bodyJson(bodyJson);
    if (!StringUtils.isEmpty(origin)) {
      request = request.header("Origin", origin);
    }
    Result result = routeWithYWErrHandler(request, app);

    JsonNode json = Json.parse(contentAsString(result));
    if (isGoodResult) {
      assertOk(result);
      assertEquals("bar", json.get("foo").asText());
      assertAuditEntry(1, customer.uuid);
    } else {
      assertBadRequest(result, UniverseYbDbAdminHandler.RUN_QUERY_ISNT_ALLOWED);
      assertAuditEntry(0, customer.uuid);
    }
  }
}
