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
import static play.test.Helpers.*;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.TelemetryProvider;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.TelemetryProviderService;
import com.yugabyte.yw.models.helpers.TelemetryProviderServiceTest;
import java.util.Arrays;
import java.util.List;
import org.junit.Before;
import org.junit.Test;
import org.mockito.InjectMocks;
import play.libs.Json;
import play.mvc.Result;

public class TelemetryProviderControllerTest extends FakeDBApplication {

  private Customer customer;
  private String authToken;
  private Users user;
  private TelemetryProviderService telemetryProviderService;
  private TelemetryProviderServiceTest telemetryProviderServiceTest;
  private TelemetryProvider telemetryProvider;
  private TelemetryProviderController telemetryProviderController;

  @InjectMocks private TelemetryProviderController controller;

  @Before
  public void setup() {
    customer = ModelFactory.testCustomer();
    user = ModelFactory.testUser(customer);
    authToken = user.createAuthToken();

    telemetryProviderService = app.injector().instanceOf(TelemetryProviderService.class);
    telemetryProviderController = app.injector().instanceOf(TelemetryProviderController.class);
  }

  @Test
  public void testListTelemetryProviders() {
    TelemetryProvider provider1 =
        TelemetryProviderServiceTest.createTestProvider(customer.getUuid(), "Test1");
    TelemetryProvider provider2 =
        TelemetryProviderServiceTest.createTestProvider(customer.getUuid(), "Test2");
    telemetryProviderService.save(provider1);
    telemetryProviderService.save(provider2);

    Result result =
        doRequestWithAuthToken(
            "GET", "/api/customers/" + customer.getUuid() + "/telemetry_provider", authToken);
    assertThat(result.status(), equalTo(OK));
    JsonNode providersJson = Json.parse(contentAsString(result));
    List<TelemetryProvider> providers =
        Arrays.asList(Json.fromJson(providersJson, TelemetryProvider[].class));
    assertThat(providers, hasSize(2));
    assertThat(
        providers,
        containsInAnyOrder(CommonUtils.maskObject(provider1), CommonUtils.maskObject(provider2)));
  }

  @Test
  public void testCreateTelemetryProvider() {
    TelemetryProvider provider =
        telemetryProviderServiceTest.createTestProvider(customer.getUuid(), "Test");
    Result result =
        doRequestWithAuthTokenAndBody(
            "POST",
            "/api/customers/" + customer.getUuid() + "/telemetry_provider",
            authToken,
            Json.toJson(provider));
    assertThat(result.status(), equalTo(OK));
    JsonNode providerJson = Json.parse(contentAsString(result));
    TelemetryProvider queriedProvider = Json.fromJson(providerJson, TelemetryProvider.class);
    assertThat(queriedProvider.getName(), equalTo(provider.getName()));
    assertThat(queriedProvider.getConfig(), equalTo(provider.getConfig()));
  }

  @Test
  public void testDeleteTelemetryProvider() {
    TelemetryProvider provider =
        telemetryProviderServiceTest.createTestProvider(customer.getUuid(), "Test");
    telemetryProviderService.save(provider);

    Result result =
        doRequestWithAuthToken(
            "DELETE",
            "/api/customers/" + customer.getUuid() + "/telemetry_provider/" + provider.getUuid(),
            authToken);
    assertThat(result.status(), equalTo(OK));
  }
}
