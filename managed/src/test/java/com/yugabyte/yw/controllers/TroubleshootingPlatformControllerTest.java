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
import java.util.Arrays;
import java.util.List;
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
  public void testListTroubleshootingPlatforms() {
    TroubleshootingPlatform platform1 =
        TroubleshootingPlatformServiceTest.createTestPlatform(
            customer.getUuid(), "https://tp1.yugabyte.com");
    TroubleshootingPlatform platform2 =
        TroubleshootingPlatformServiceTest.createTestPlatform(
            customer.getUuid(), "https://tp2.yugabyte.com");
    troubleshootingPlatformService.save(platform1, false);
    troubleshootingPlatformService.save(platform2, false);

    Result result =
        doRequestWithAuthToken(
            "GET", "/api/customers/" + customer.getUuid() + "/troubleshooting_platform", authToken);
    assertThat(result.status(), equalTo(OK));
    JsonNode platformsJson = Json.parse(contentAsString(result));
    List<TroubleshootingPlatformExt> platforms =
        Arrays.asList(Json.fromJson(platformsJson, TroubleshootingPlatformExt[].class));
    assertThat(platforms, hasSize(2));
    // ERROR because we fail ro make a request to TP in test
    List<TroubleshootingPlatformExt> expected =
        ImmutableList.of(
            new TroubleshootingPlatformExt()
                .setTroubleshootingPlatform(platform1)
                .setInUseStatus(TroubleshootingPlatformExt.InUseStatus.ERROR),
            new TroubleshootingPlatformExt()
                .setTroubleshootingPlatform(platform2)
                .setInUseStatus(TroubleshootingPlatformExt.InUseStatus.ERROR));
    assertThat(platforms, containsInAnyOrder(expected.toArray()));
  }

  @Test
  public void testCreateTroubleshootingPlatform() {
    TroubleshootingPlatform platform =
        TroubleshootingPlatformServiceTest.createTestPlatform(
            customer.getUuid(), "https://tp.yugabyte.com");
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

  @Test
  public void testEditTroubleshootingPlatform() {
    TroubleshootingPlatform platform =
        TroubleshootingPlatformServiceTest.createTestPlatform(
            customer.getUuid(), "https://tp.yugabyte.com");
    troubleshootingPlatformService.save(platform, false);
    platform.setYbaUrl("http://some.host");
    platform.setMetricsUrl("http://metrics.host");
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

  @Test
  public void testDeleteTroubleshootingPlatform() {
    TroubleshootingPlatform platform =
        TroubleshootingPlatformServiceTest.createTestPlatform(
            customer.getUuid(), "https://tp.yugabyte.com");
    troubleshootingPlatformService.save(platform, false);

    Result result =
        doRequestWithAuthToken(
            "DELETE",
            "/api/customers/"
                + customer.getUuid()
                + "/troubleshooting_platform/"
                + platform.getUuid()
                + "?force=true",
            authToken);
    assertThat(result.status(), equalTo(OK));
  }
}
