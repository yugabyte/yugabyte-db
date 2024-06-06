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
import static org.hamcrest.Matchers.containsInAnyOrder;
import static org.hamcrest.Matchers.equalTo;
import static org.hamcrest.Matchers.hasSize;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.doNothing;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.RuntimeConfigEntry;
import com.yugabyte.yw.models.TelemetryProvider;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.TelemetryProviderServiceTest;
import java.util.Arrays;
import java.util.List;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;
import play.mvc.Result;

@RunWith(MockitoJUnitRunner.class)
public class TelemetryProviderControllerTest extends FakeDBApplication {

  private Customer customer;
  private String authToken;
  private Users user;
  private TelemetryProviderServiceTest telemetryProviderServiceTest;

  @Before
  public void setup() {
    customer = ModelFactory.testCustomer();
    user = ModelFactory.testUser(customer);
    authToken = user.createAuthToken();
    RuntimeConfigEntry.upsertGlobal(GlobalConfKeys.dbAuditLoggingEnabled.getKey(), "true");

    doNothing().when(mockTelemetryProviderService).validateBean(any());
    doNothing().when(mockTelemetryProviderService).validateTelemetryProvider(any());
    doNothing().when(mockTelemetryProviderService).throwExceptionIfRuntimeFlagDisabled();
  }

  @Test
  public void testListTelemetryProviders() {
    TelemetryProvider provider1 =
        TelemetryProviderServiceTest.createTestProvider(customer.getUuid(), "Test1");
    TelemetryProvider provider2 =
        TelemetryProviderServiceTest.createTestProvider(customer.getUuid(), "Test2");
    provider1.generateUUID().save();
    provider2.generateUUID().save();

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
        TelemetryProviderServiceTest.createTestProvider(customer.getUuid(), "Test");
    provider.generateUUID().save();

    Result result =
        doRequestWithAuthToken(
            "DELETE",
            "/api/customers/" + customer.getUuid() + "/telemetry_provider/" + provider.getUuid(),
            authToken);
    assertThat(result.status(), equalTo(OK));
  }
}
