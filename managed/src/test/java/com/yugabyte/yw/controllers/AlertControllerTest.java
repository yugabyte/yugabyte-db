// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.CloudQueryHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.CallHomeManager.CollectionLevel;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Users;

import com.yugabyte.yw.models.CustomerConfig;
import com.yugabyte.yw.models.Universe;
import org.junit.Before;
import org.junit.Test;
import org.mockito.ArgumentCaptor;
import play.Application;
import play.inject.guice.GuiceApplicationBuilder;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;
import play.test.Helpers;

import java.text.ParseException;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.Map;
import java.util.UUID;

import static com.yugabyte.yw.common.AssertHelper.assertBadRequest;
import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static com.yugabyte.yw.common.AssertHelper.assertAuditEntry;
import static com.yugabyte.yw.common.FakeApiHelper.doRequestWithAuthTokenAndBody;
import static com.yugabyte.yw.common.FakeApiHelper.doRequestWithAuthToken;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static org.hamcrest.CoreMatchers.*;
import static org.hamcrest.core.StringContains.containsString;
import static org.mockito.Matchers.anyList;
import static org.mockito.Matchers.anyMap;
import static org.mockito.Mockito.*;
import static play.inject.Bindings.bind;
import static play.test.Helpers.*;
import static org.junit.Assert.*;
import static play.mvc.Http.Status.OK;
import static play.mvc.Http.Status.FORBIDDEN;
import static play.test.Helpers.fakeRequest;

public class AlertControllerTest extends FakeDBApplication {
  private Customer customer;
  private Users user;
  private String authToken;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    user = ModelFactory.testUser(customer);
    authToken = user.createAuthToken();
  }

  @Test
  public void testCreateAlert() {
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    Result result = doRequestWithAuthToken("GET",
        "/api/customers/" + customer.uuid + "/alerts", authToken);
    assertEquals(OK, result.status());
    assertEquals("[]", contentAsString(result));

    ObjectNode params = Json.newObject();
    params.put("errCode", "VALID_ALERT");
    params.put("type", "WARNING");
    params.put("message", "Testing add valid alert.");
    result = doRequestWithAuthTokenAndBody("POST",
        "/api/customers/" + customer.uuid + "/alerts", authToken, params);
    assertEquals(OK, result.status());

    result = doRequestWithAuthToken("GET",
        "/api/customers/" + customer.uuid + "/alerts", authToken);
    assertEquals(OK, result.status());
    assertAuditEntry(1, customer.uuid);
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(1, json.size());
    JsonNode alert = json.get(0);
    assertEquals(params.get("errCode"), alert.get("errCode"));
    assertEquals(params.get("type"), alert.get("type"));
    assertEquals(params.get("message"), alert.get("message"));
  }

  @Test
  public void testUpsertValid() throws ParseException, InterruptedException {
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    Result result = doRequestWithAuthToken("GET",
        "/api/customers/" + customer.uuid + "/alerts", authToken);
    assertEquals(OK, result.status());
    assertEquals("[]", contentAsString(result));

    ObjectNode params = Json.newObject();
    params.put("errCode", "VALID_ALERT")
          .put("type", "WARNING")
          .put("message", "First alert.");
    result = doRequestWithAuthTokenAndBody("PUT",
        "/api/customers/" + customer.uuid + "/alerts", authToken, params);
    assertEquals(OK, result.status());

    result = doRequestWithAuthToken("GET",
        "/api/customers/" + customer.uuid + "/alerts", authToken);
    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(1, json.size());
    JsonNode alert = json.get(0);
    assertEquals(params.get("errCode"), alert.get("errCode"));
    assertEquals(params.get("type"), alert.get("type"));
    assertEquals(params.get("message"), alert.get("message"));

    SimpleDateFormat formatter = new SimpleDateFormat("EEE MMM dd hh:mm:ss z yyyy");
    Date firstDate = formatter.parse(alert.get("createTime").asText());

    // Sleep so that API registers the request as a different Date.
    Thread.sleep(1000);
    params.put("message", "Second alert.");
    result = doRequestWithAuthTokenAndBody("PUT",
        "/api/customers/" + customer.uuid + "/alerts", authToken, params);
    assertEquals(OK, result.status());

    result = doRequestWithAuthToken("GET",
    "/api/customers/" + customer.uuid + "/alerts", authToken);
    assertEquals(OK, result.status());
    json = Json.parse(contentAsString(result));
    assertEquals(1, json.size());
    alert = json.get(0);
    assertEquals(params.get("errCode"), alert.get("errCode"));
    assertEquals(params.get("type"), alert.get("type"));
    assertEquals(params.get("message"), alert.get("message"));

    Date secondDate = formatter.parse(alert.get("createTime").asText());
    String errMsg = String.format("Expected second alert's createTime to be later than first."
      + "First: %s. Second: %s.", firstDate, secondDate);
    assertTrue(errMsg, secondDate.after(firstDate));
    assertAuditEntry(2, customer.uuid);
  }
}
