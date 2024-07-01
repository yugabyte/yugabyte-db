/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *
https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.controllers;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.*;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.troubleshooting.TroubleshootingPlatformService;
import com.yugabyte.yw.common.troubleshooting.TroubleshootingPlatformServiceTest;
import com.yugabyte.yw.forms.TroubleshootingPlatformExt;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.TroubleshootingPlatform;
import com.yugabyte.yw.models.Users;
import java.io.IOException;
import java.util.Arrays;
import java.util.List;
import okhttp3.HttpUrl;
import okhttp3.mockwebserver.MockResponse;
import okhttp3.mockwebserver.MockWebServer;
import org.junit.Before;
import org.junit.Test;
import org.mockito.InjectMocks;
import play.libs.Json;
import play.mvc.Result;

public class TroubleshootingPlatformControllerTest extends FakeDBApplication {

  private Customer customer;
  private String authToken;
  private Users user;
  private TroubleshootingPlatformService troubleshootingPlatformService;
  private TroubleshootingPlatformController troubleshootingPlatformController;

  @InjectMocks private TroubleshootingPlatformController controller;

  @Before
  public void setup() {
    customer = ModelFactory.testCustomer();
    user = ModelFactory.testUser(customer);
    authToken = user.createAuthToken();

    troubleshootingPlatformService =
        app.injector().instanceOf(TroubleshootingPlatformService.class);
    troubleshootingPlatformController =
        app.injector().instanceOf(TroubleshootingPlatformController.class);
  }

  @Test
  public void testListTroubleshootingPlatforms() throws IOException {
    try (MockWebServer server = new MockWebServer()) {
      server.start();
      HttpUrl baseUrl = server.url("/api/customer_metadata/" + customer.toString());
      TroubleshootingPlatform platform1 =
          TroubleshootingPlatformServiceTest.createTestPlatform(
              customer.getUuid(), baseUrl.scheme() + "://" + baseUrl.host() + ":" + baseUrl.port());
      TroubleshootingPlatform platform2 =
          TroubleshootingPlatformServiceTest.createTestPlatform(
              customer.getUuid(), baseUrl.scheme() + "://127.0.0.1:" + baseUrl.port());
      server.enqueue(
          new MockResponse()
              .setBody(TroubleshootingPlatformServiceTest.convertToCustomerMetadata(platform1)));
      server.enqueue(
          new MockResponse()
              .setBody(TroubleshootingPlatformServiceTest.convertToCustomerMetadata(platform2)));
      troubleshootingPlatformService.save(platform1, false);
      troubleshootingPlatformService.save(platform2, false);

      server.enqueue(new MockResponse().setBody("[]"));
      server.enqueue(new MockResponse().setBody("[{},{}]"));
      Result result =
          doRequestWithAuthToken(
              "GET",
              "/api/customers/" + customer.getUuid() + "/troubleshooting_platform",
              authToken);
      assertThat(result.status(), equalTo(OK));
      JsonNode platformsJson = Json.parse(contentAsString(result));
      List<TroubleshootingPlatformExt> platforms =
          Arrays.asList(Json.fromJson(platformsJson, TroubleshootingPlatformExt[].class));
      assertThat(platforms, hasSize(2));
      platform1.setApiToken("to*en");
      platform2.setApiToken("to*en");
      List<TroubleshootingPlatformExt> expected =
          ImmutableList.of(
              new TroubleshootingPlatformExt()
                  .setTroubleshootingPlatform(platform1)
                  .setInUseStatus(TroubleshootingPlatformExt.InUseStatus.NOT_IN_USE),
              new TroubleshootingPlatformExt()
                  .setTroubleshootingPlatform(platform2)
                  .setInUseStatus(TroubleshootingPlatformExt.InUseStatus.IN_USE));
      assertThat(platforms, containsInAnyOrder(expected.toArray()));
    }
  }

  @Test
  public void testCreateTroubleshootingPlatform() throws IOException {
    try (MockWebServer server = new MockWebServer()) {
      server.start();
      HttpUrl baseUrl = server.url("/api/customer_metadata/" + customer.toString());
      TroubleshootingPlatform platform =
          TroubleshootingPlatformServiceTest.createTestPlatform(
              customer.getUuid(), baseUrl.scheme() + "://" + baseUrl.host() + ":" + baseUrl.port());
      server.enqueue(
          new MockResponse()
              .setBody(TroubleshootingPlatformServiceTest.convertToCustomerMetadata(platform)));
      Result result =
          doRequestWithAuthTokenAndBody(
              "POST",
              "/api/customers/" + customer.getUuid() + "/troubleshooting_platform",
              authToken,
              Json.toJson(platform));
      assertThat(result.status(), equalTo(OK));
      JsonNode platformJson = Json.parse(contentAsString(result));
      TroubleshootingPlatform queriedPlatform =
          Json.fromJson(platformJson, TroubleshootingPlatform.class);
      assertThat(queriedPlatform.getTpUrl(), equalTo(platform.getTpUrl()));
      assertThat(queriedPlatform.getYbaUrl(), equalTo(platform.getYbaUrl()));
      assertThat(queriedPlatform.getMetricsUrl(), equalTo(platform.getMetricsUrl()));
    }
  }

  @Test
  public void testEditTroubleshootingPlatform() throws IOException {
    try (MockWebServer server = new MockWebServer()) {
      server.start();
      HttpUrl baseUrl = server.url("/api/customer_metadata/" + customer.toString());
      TroubleshootingPlatform platform =
          TroubleshootingPlatformServiceTest.createTestPlatform(
              customer.getUuid(), baseUrl.scheme() + "://" + baseUrl.host() + ":" + baseUrl.port());
      server.enqueue(
          new MockResponse()
              .setBody(TroubleshootingPlatformServiceTest.convertToCustomerMetadata(platform)));
      troubleshootingPlatformService.save(platform, false);
      platform.setYbaUrl("http://some.host");
      platform.setMetricsUrl("http://metrics.host");

      server.enqueue(
          new MockResponse()
              .setBody(TroubleshootingPlatformServiceTest.convertToCustomerMetadata(platform)));
      Result result =
          doRequestWithAuthTokenAndBody(
              "PUT",
              "/api/customers/"
                  + customer.getUuid()
                  + "/troubleshooting_platform/"
                  + platform.getUuid(),
              authToken,
              Json.toJson(platform));
      assertThat(result.status(), equalTo(OK));
      JsonNode platformJson = Json.parse(contentAsString(result));
      TroubleshootingPlatform queriedPlatform =
          Json.fromJson(platformJson, TroubleshootingPlatform.class);
      assertThat(queriedPlatform.getTpUrl(), equalTo(platform.getTpUrl()));
      assertThat(queriedPlatform.getYbaUrl(), equalTo(platform.getYbaUrl()));
      assertThat(queriedPlatform.getMetricsUrl(), equalTo(platform.getMetricsUrl()));
    }
  }

  @Test
  public void testDeleteTroubleshootingPlatform() throws IOException {
    try (MockWebServer server = new MockWebServer()) {
      server.start();
      HttpUrl baseUrl = server.url("/api/customer_metadata/" + customer.toString());
      TroubleshootingPlatform platform =
          TroubleshootingPlatformServiceTest.createTestPlatform(
              customer.getUuid(), baseUrl.scheme() + "://" + baseUrl.host() + ":" + baseUrl.port());
      server.enqueue(
          new MockResponse()
              .setBody(TroubleshootingPlatformServiceTest.convertToCustomerMetadata(platform)));
      troubleshootingPlatformService.save(platform, false);

      server.enqueue(new MockResponse());
      Result result =
          doRequestWithAuthToken(
              "DELETE",
              "/api/customers/"
                  + customer.getUuid()
                  + "/troubleshooting_platform/"
                  + platform.getUuid(),
              authToken);
      assertThat(result.status(), equalTo(OK));
    }
  }
}
