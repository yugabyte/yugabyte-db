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
import static com.yugabyte.yw.models.helpers.ExternalScriptHelper.EXT_SCRIPT_CONTENT_CONF_PATH;
import static com.yugabyte.yw.models.helpers.ExternalScriptHelper.EXT_SCRIPT_PARAMS_CONF_PATH;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;
import static play.test.Helpers.FORBIDDEN;
import static play.test.Helpers.NOT_FOUND;
import static play.test.Helpers.OK;
import static play.test.Helpers.contentAsString;
import static play.test.Helpers.fakeRequest;
import static play.test.Helpers.route;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableSet;
import com.typesafe.config.Config;
import com.typesafe.config.ConfigFactory;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.RuntimeConfigChangeListener;
import com.yugabyte.yw.common.config.RuntimeConfigChangeNotifier;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.forms.RuntimeConfigFormData.ScopedConfig.ScopeType;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.helpers.ExternalScriptHelper;
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
import org.apache.commons.lang3.StringUtils;
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
  private static final String EXT_SCRIPT_KEY = "yb.external_script";
  private static final String EXT_SCRIPT_SCHEDULE_KEY = "yb.external_script.schedule";

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
        ImmutableSet.of(
            "yb.universe_boot_script",
            "yb.taskGC.gc_check_interval",
            "yb.taskGC.task_retention_duration",
            "yb.external_script");
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
        assertPlatformException(() -> getKey(defaultUniverse.universeUUID, GC_CHECK_INTERVAL_KEY))
            .status());
    String newInterval = "2 days";
    assertEquals(OK, setGCInterval(newInterval, defaultUniverse.universeUUID).status());
    RuntimeConfigFactory runtimeConfigFactory =
        app.injector().instanceOf(RuntimeConfigFactory.class);
    Duration duration =
        runtimeConfigFactory.forUniverse(defaultUniverse).getDuration(GC_CHECK_INTERVAL_KEY);
    assertEquals(24 * 60 * 2, duration.toMinutes());
    assertEquals(
        newInterval, contentAsString(getKey(defaultUniverse.universeUUID, GC_CHECK_INTERVAL_KEY)));
    assertEquals(OK, deleteKey(defaultUniverse.universeUUID, GC_CHECK_INTERVAL_KEY).status());
    assertEquals(
        NOT_FOUND,
        assertPlatformException(() -> getKey(defaultUniverse.universeUUID, GC_CHECK_INTERVAL_KEY))
            .status());
  }

  private Result setGCInterval(String interval, UUID scopeUUID) {
    Http.RequestBuilder request =
        fakeRequest(
                "PUT", String.format(KEY, defaultCustomer.uuid, scopeUUID, GC_CHECK_INTERVAL_KEY))
            .header("X-AUTH-TOKEN", authToken)
            .bodyText(interval);
    return route(app, request);
  }

  @Test
  public void keyObj() {
    assertEquals(
        NOT_FOUND,
        assertPlatformException(() -> getKey(defaultUniverse.universeUUID, GC_CHECK_INTERVAL_KEY))
            .status());
    String newInterval = "2 days";
    String newRetention = "32 days";
    assertEquals(
        OK, setExtScriptObject(newInterval, newRetention, defaultUniverse.universeUUID).status());

    // Now get key internal to the external script object directly (on server side)
    RuntimeConfigFactory runtimeConfigFactory =
        app.injector().instanceOf(RuntimeConfigFactory.class);
    Duration duration =
        runtimeConfigFactory.forUniverse(defaultUniverse).getDuration(EXT_SCRIPT_SCHEDULE_KEY);
    assertEquals(24 * 60 * 2, duration.toMinutes());
    String content =
        runtimeConfigFactory.forUniverse(defaultUniverse).getString(EXT_SCRIPT_CONTENT_CONF_PATH);
    assertEquals("the script", content);
    String params =
        runtimeConfigFactory.forUniverse(defaultUniverse).getString(EXT_SCRIPT_PARAMS_CONF_PATH);
    assertEquals(newRetention, params);

    // Fetching internal key through API should not work
    assertEquals(
        NOT_FOUND,
        assertPlatformException(() -> getKey(defaultUniverse.universeUUID, EXT_SCRIPT_SCHEDULE_KEY))
            .status());

    // Fetch whole object through the API parse it and extract the internal key
    assertEquals(
        newInterval,
        ConfigFactory.parseString(
                contentAsString(getKey(defaultUniverse.universeUUID, EXT_SCRIPT_KEY)))
            .getString("schedule"));

    assertEquals(
        "If you set an object deleting its internal key should result in key not found",
        NOT_FOUND,
        assertPlatformException(
                () -> deleteKey(defaultUniverse.universeUUID, EXT_SCRIPT_SCHEDULE_KEY))
            .status());

    assertEquals(
        "Delete of the object key should work",
        OK,
        deleteKey(defaultUniverse.universeUUID, EXT_SCRIPT_KEY).status());

    assertEquals(
        "The object was deleted. So expecting NOT_FOUND status",
        NOT_FOUND,
        assertPlatformException(() -> getKey(defaultUniverse.universeUUID, EXT_SCRIPT_KEY))
            .status());
  }

  private Result setExtScriptObject(String schedule, String content, UUID scopeUUID) {
    Http.RequestBuilder request =
        fakeRequest("PUT", String.format(KEY, defaultCustomer.uuid, scopeUUID, EXT_SCRIPT_KEY))
            .header("X-AUTH-TOKEN", authToken)
            .bodyText(
                String.format(
                    "{"
                        + "  schedule = %s\n"
                        + "  params = %s\n"
                        + "  content = \"the script\"\n"
                        + "}",
                    schedule, content));
    return route(app, request);
  }

  private Result getKey(UUID scopeUUID, String key) {
    return doRequestWithAuthToken(
        "GET", String.format(KEY, defaultCustomer.uuid, scopeUUID, key), authToken);
  }

  private Result deleteKey(UUID universeUUID, String key) {
    return doRequestWithAuthToken(
        "DELETE", String.format(KEY, defaultCustomer.uuid, universeUUID, key), authToken);
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
  public void getConfig_universe_inherited(
      ScopeType scopeType, String presetIntervalValue, String expectedIntervalValue) {
    UUID scopeUUID = getScopeUUIDForType(scopeType);
    if (!presetIntervalValue.isEmpty()) {
      setGCInterval(presetIntervalValue, scopeUUID);
    }
    final String actualValue =
        internal_getConfig_universe_inherited(
            scopeType, presetIntervalValue, scopeUUID, GC_CHECK_INTERVAL_KEY);
    compareToExpectedValue(expectedIntervalValue, actualValue, "1 hour");
  }

  // Same test as above except the config is set as external Script object with retention  key
  // embeded
  @Test
  @Parameters(method = "scopeAndPresetParamsObj")
  public void getConfig_universe_inherited_obj(
      ScopeType scopeType, String presetIntervalValue, String expectedIntervalValue) {
    UUID scopeUUID = getScopeUUIDForType(scopeType);
    String newRetention = "32 days";
    if (!presetIntervalValue.isEmpty()) {
      setExtScriptObject(presetIntervalValue, newRetention, scopeUUID);
    }
    final String actualObjValue =
        internal_getConfig_universe_inherited(
            scopeType, presetIntervalValue, scopeUUID, EXT_SCRIPT_KEY);
    final Config configObj = ConfigFactory.parseString(actualObjValue);
    String expectedValue = null;
    if (configObj.hasPath("schedule")) {
      expectedValue = configObj.getValue("schedule").render();
    }
    compareToExpectedValue(expectedIntervalValue, expectedValue, null);
  }

  private String internal_getConfig_universe_inherited(
      ScopeType scopeType, String presetIntervalValue, UUID scopeUUID, String checkKey) {
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
      if (checkKey.equals(entry.get("key").asText()) && !presetIntervalValue.isEmpty()) {
        assertFalse(contentAsString(result), entry.get("inherited").asBoolean());
      } else {
        assertTrue(contentAsString(result), entry.get("inherited").asBoolean());
      }
      configEntriesMap.put(entry.get("key").asText(), entry.get("value").asText());
    }
    // two taskGC entries (There will be more in future hence >= 2)
    assertTrue(contentAsString(result), configEntriesMap.size() >= 2);
    return configEntriesMap.get(checkKey);
  }

  private void compareToExpectedValue(
      String presetIntervalValue, String value, String defaultValue) {
    if (StringUtils.isEmpty(presetIntervalValue)) {
      assertEquals(defaultValue, value);
    } else {
      assertEquals(presetIntervalValue, value);
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
      new Object[] {ScopeType.GLOBAL, "", ""},
      new Object[] {ScopeType.CUSTOMER, "", ""},
      new Object[] {ScopeType.PROVIDER, "", ""},
      new Object[] {ScopeType.UNIVERSE, "", ""},
      // We will return any strings as unquoted even if they were set as quoted
      new Object[] {ScopeType.GLOBAL, "\"33 days\"", "33 days"},
      new Object[] {ScopeType.CUSTOMER, "\"44 seconds\"", "44 seconds"},
      new Object[] {ScopeType.PROVIDER, "\"22 hours\"", "22 hours"},
      // Set without quotes should be allowed for string objects backward compatibility
      // Even when set with quotes we will return string without redundant quotes.
      // But we will do proper escaping for special characters
      new Object[] {ScopeType.UNIVERSE, "11\"", "11\\\""},
    };
  }

  public Object[] scopeAndPresetParamsObj() {
    return new Object[] {
      new Object[] {ScopeType.GLOBAL, "", null},
      new Object[] {ScopeType.CUSTOMER, "", null},
      new Object[] {ScopeType.PROVIDER, "", null},
      new Object[] {ScopeType.UNIVERSE, "", null},
      new Object[] {ScopeType.GLOBAL, "\"33 days\"", "\"33 days\""},
      new Object[] {ScopeType.CUSTOMER, "\"44 seconds\"", "\"44 seconds\""},
      new Object[] {ScopeType.PROVIDER, "\"22 hours\"", "\"22 hours\""},
      // Set without escape quotes should not be allowed within a json object
      new Object[] {ScopeType.UNIVERSE, "\"11\\\"\"", "\"11\\\"\""},
    };
  }

  @Test
  @Parameters({"yb.upgrade.vmImage", "yb.health.trigger_api.enabled"})
  public void configResolution(String key) {
    RuntimeConfigFactory runtimeConfigFactory =
        app.injector().instanceOf(RuntimeConfigFactory.class);
    assertFalse(runtimeConfigFactory.forUniverse(defaultUniverse).getBoolean(key));
    setCloudEnabled(defaultUniverse.universeUUID);
    assertTrue(runtimeConfigFactory.forUniverse(defaultUniverse).getBoolean(key));
  }

  private void setCloudEnabled(UUID scopeUUID) {
    Http.RequestBuilder request =
        fakeRequest("PUT", String.format(KEY, defaultCustomer.uuid, scopeUUID, "yb.cloud.enabled"))
            .header("X-AUTH-TOKEN", authToken)
            .bodyText("true");
    route(app, request);
  }

  @Test
  public void testFailingListener() {
    assertEquals(
        NOT_FOUND,
        assertPlatformException(() -> getKey(defaultUniverse.universeUUID, GC_CHECK_INTERVAL_KEY))
            .status());
    String newInterval = "2 days";
    RuntimeConfigChangeNotifier runtimeConfigChangeNotifier =
        getApp().injector().instanceOf(RuntimeConfigChangeNotifier.class);
    runtimeConfigChangeNotifier.addListener(
        new RuntimeConfigChangeListener() {
          @Override
          public String getKeyPath() {
            return GC_CHECK_INTERVAL_KEY;
          }

          public void processUniverse(Universe universe) {
            throw new PlatformServiceException(INTERNAL_SERVER_ERROR, "Some error");
          }
        });
    assertEquals(
        INTERNAL_SERVER_ERROR,
        assertPlatformException(() -> setGCInterval(newInterval, defaultUniverse.universeUUID))
            .status());
    assertEquals(
        NOT_FOUND,
        assertPlatformException(() -> getKey(defaultUniverse.universeUUID, GC_CHECK_INTERVAL_KEY))
            .status());
  }
}
