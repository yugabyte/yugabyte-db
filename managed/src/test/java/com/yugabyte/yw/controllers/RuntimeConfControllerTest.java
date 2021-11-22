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

import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static com.yugabyte.yw.common.FakeApiHelper.doRequestWithAuthToken;
import static com.yugabyte.yw.models.ScopedRuntimeConfig.GLOBAL_SCOPE_UUID;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static play.test.Helpers.FORBIDDEN;
import static play.test.Helpers.NOT_FOUND;
import static play.test.Helpers.OK;
import static play.test.Helpers.contentAsString;
import static play.test.Helpers.fakeRequest;
import static play.test.Helpers.route;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableSet;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.forms.RuntimeConfigFormData.ScopedConfig.ScopeType;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import java.time.Duration;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;
import play.test.Helpers;

@RunWith(JUnitParamsRunner.class)
public class RuntimeConfControllerTest extends FakeDBApplication {
  private static final String LIST_KEYS = "/api/runtime_config/mutable_keys";
  private static final String LIST_SCOPES = "/api/customers/%s/runtime_config/scopes";
  private static final String GET_CONFIG = "/api/customers/%s/runtime_config/%s";
  private static final String GET_CONFIG_INCL_INHERITED = GET_CONFIG + "?includeInherited=true";
  private static final String KEY = "/api/customers/%s/runtime_config/%s/key/%s";
  private static final String GC_CHECK_INTERVAL_KEY = "yb.taskGC.gc_check_interval";

  private Customer defaultCustomer;
  private Universe defaultUniverse;
  private Provider defaultProvider;
  private String authToken;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    defaultUniverse = ModelFactory.createUniverse(defaultCustomer.getCustomerId());
    defaultProvider = ModelFactory.kubernetesProvider(defaultCustomer);
    Users user = ModelFactory.testUser(defaultCustomer, Users.Role.SuperAdmin);
    authToken = user.createAuthToken();
  }

  @Test
  public void authError() {
    Result result =
        Helpers.route(app, fakeRequest("GET", String.format(LIST_SCOPES, defaultCustomer.uuid)));
    assertEquals(FORBIDDEN, result.status());
    assertEquals("Unable To Authenticate User", contentAsString(result));
  }

  @Test
  public void listKeys() {
    Result result = doRequestWithAuthToken("GET", LIST_KEYS, authToken);
    assertEquals(OK, result.status());
    ImmutableSet<String> expectedKeys =
        ImmutableSet.of("yb.taskGC.gc_check_interval", "yb.taskGC.task_retention_duration");
    Set<String> actualKeys =
        ImmutableSet.copyOf(Json.parse(contentAsString(result)).elements())
            .stream()
            .map(JsonNode::asText)
            .collect(Collectors.toSet());
    assertTrue(String.valueOf(actualKeys), actualKeys.containsAll(expectedKeys));
  }

  @Test
  public void listScopes() {
    Result result =
        doRequestWithAuthToken("GET", String.format(LIST_SCOPES, defaultCustomer.uuid), authToken);
    assertEquals(OK, result.status());
    Iterator<JsonNode> scopedConfigList =
        Json.parse(contentAsString(result)).get("scopedConfigList").elements();
    Map<ScopeType, List<UUID>> actualScopesMap = new HashMap<>();
    while (scopedConfigList.hasNext()) {
      JsonNode actualScope = scopedConfigList.next();
      assertTrue(actualScope.get("mutableScope").asBoolean());
      ScopeType type = ScopeType.valueOf(actualScope.get("type").asText());

      if (actualScopesMap.containsKey(type)) {
        actualScopesMap.get(type).add(UUID.fromString(actualScope.get("uuid").asText()));
      } else {
        List<UUID> typeList = new ArrayList<>();
        typeList.add(UUID.fromString(actualScope.get("uuid").asText()));
        actualScopesMap.put(type, typeList);
      }
    }
    assertEquals(4, actualScopesMap.size());
    assertTrue(actualScopesMap.get(ScopeType.GLOBAL).contains(GLOBAL_SCOPE_UUID));
    assertTrue(actualScopesMap.get(ScopeType.CUSTOMER).contains(defaultCustomer.uuid));
    assertTrue(actualScopesMap.get(ScopeType.PROVIDER).contains(defaultProvider.uuid));
    assertTrue(actualScopesMap.get(ScopeType.UNIVERSE).contains(defaultUniverse.universeUUID));
  }

  @Test
  public void key() {
    assertEquals(
        NOT_FOUND,
        assertPlatformException(() -> getGCInterval(defaultUniverse.universeUUID)).status());
    String newInterval = "2 days";
    Result result = setGCInterval(newInterval, defaultUniverse.universeUUID);
    RuntimeConfigFactory runtimeConfigFactory =
        app.injector().instanceOf(RuntimeConfigFactory.class);
    Duration duration =
        runtimeConfigFactory.forUniverse(defaultUniverse).getDuration(GC_CHECK_INTERVAL_KEY);
    assertEquals(24 * 60 * 2, duration.toMinutes());
    assertEquals(OK, result.status());
    assertEquals(newInterval, contentAsString(getGCInterval(defaultUniverse.universeUUID)));
    assertEquals(OK, deleteGCInterval(defaultUniverse.universeUUID).status());
    assertEquals(
        NOT_FOUND,
        assertPlatformException(() -> getGCInterval(defaultUniverse.universeUUID)).status());
  }

  private Result setGCInterval(String interval, UUID scopeUUID) {
    Http.RequestBuilder request =
        fakeRequest(
                "PUT", String.format(KEY, defaultCustomer.uuid, scopeUUID, GC_CHECK_INTERVAL_KEY))
            .header("X-AUTH-TOKEN", authToken)
            .bodyText(interval);
    return route(app, request);
  }

  private Result getGCInterval(UUID scopeUUID) {
    return doRequestWithAuthToken(
        "GET",
        String.format(KEY, defaultCustomer.uuid, scopeUUID, GC_CHECK_INTERVAL_KEY),
        authToken);
  }

  private Result deleteGCInterval(UUID universeUUID) {
    return doRequestWithAuthToken(
        "DELETE",
        String.format(KEY, defaultCustomer.uuid, universeUUID, GC_CHECK_INTERVAL_KEY),
        authToken);
  }

  @Test
  public void getConfig_global() {
    Result result =
        doRequestWithAuthToken(
            "GET", String.format(GET_CONFIG, defaultCustomer.uuid, GLOBAL_SCOPE_UUID), authToken);
    assertEquals(OK, result.status());
    JsonNode actualScope = Json.parse(contentAsString(result));
    assertTrue(actualScope.get("mutableScope").asBoolean());
    assertEquals(ScopeType.GLOBAL, ScopeType.valueOf(actualScope.get("type").asText()));
    assertFalse(actualScope.get("configEntries").elements().hasNext());
  }

  @Test
  @Parameters(method = "scopeAndPresetParams")
  public void getConfig_universe_inherited(ScopeType scopeType, String presetIntervalValue) {
    UUID scopeUUID = getScopeUUIDForType(scopeType);
    if (!presetIntervalValue.isEmpty()) {
      setGCInterval(presetIntervalValue, scopeUUID);
    }
    Result result =
        doRequestWithAuthToken(
            "GET",
            String.format(GET_CONFIG_INCL_INHERITED, defaultCustomer.uuid, scopeUUID),
            authToken);
    assertEquals(OK, result.status());
    JsonNode actualScope = Json.parse(contentAsString(result));
    assertTrue(actualScope.get("mutableScope").asBoolean());
    assertEquals(scopeType, ScopeType.valueOf(actualScope.get("type").asText()));
    Iterator<JsonNode> configEntries = actualScope.get("configEntries").elements();
    Map<String, String> configEntriesMap = new HashMap<>();
    while (configEntries.hasNext()) {
      JsonNode entry = configEntries.next();
      if (GC_CHECK_INTERVAL_KEY.equals(entry.get("key").asText())
          && !presetIntervalValue.isEmpty()) {
        assertFalse(contentAsString(result), entry.get("inherited").asBoolean());
      } else {
        assertTrue(contentAsString(result), entry.get("inherited").asBoolean());
      }
      configEntriesMap.put(entry.get("key").asText(), entry.get("value").asText());
    }
    // two taskGC entries (There will be more in future hence >= 2)
    assertTrue(contentAsString(result), configEntriesMap.size() >= 2);
    if (presetIntervalValue.isEmpty()) {
      assertEquals("1 hour", configEntriesMap.get(GC_CHECK_INTERVAL_KEY));
    } else {
      assertEquals(presetIntervalValue, configEntriesMap.get(GC_CHECK_INTERVAL_KEY));
    }
  }

  private UUID getScopeUUIDForType(ScopeType scopeType) {
    switch (scopeType) {
      case GLOBAL:
        return GLOBAL_SCOPE_UUID;
      case CUSTOMER:
        return defaultCustomer.uuid;
      case UNIVERSE:
        return defaultUniverse.universeUUID;
      case PROVIDER:
        return defaultProvider.uuid;
    }
    return null;
  }

  public Object[] scopeAndPresetParams() {
    return new Object[] {
      new Object[] {ScopeType.GLOBAL, ""},
      new Object[] {ScopeType.CUSTOMER, ""},
      new Object[] {ScopeType.PROVIDER, ""},
      new Object[] {ScopeType.UNIVERSE, ""},
      new Object[] {ScopeType.GLOBAL, "33 days"},
      new Object[] {ScopeType.CUSTOMER, "44 seconds"},
      new Object[] {ScopeType.PROVIDER, "22 hours"},
      new Object[] {ScopeType.UNIVERSE, "11 minutes"},
    };
  }
}
