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
import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static com.yugabyte.yw.models.ScopedRuntimeConfig.GLOBAL_SCOPE_UUID;
import static com.yugabyte.yw.models.helpers.ExternalScriptHelper.EXT_SCRIPT_CONTENT_CONF_PATH;
import static com.yugabyte.yw.models.helpers.ExternalScriptHelper.EXT_SCRIPT_PARAMS_CONF_PATH;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;
import static play.test.Helpers.BAD_REQUEST;
import static play.test.Helpers.NOT_FOUND;
import static play.test.Helpers.OK;
import static play.test.Helpers.UNAUTHORIZED;
import static play.test.Helpers.contentAsString;
import static play.test.Helpers.fakeRequest;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.common.collect.ImmutableSet;
import com.nimbusds.oauth2.sdk.ParseException;
import com.nimbusds.openid.connect.sdk.op.OIDCProviderMetadata;
import com.typesafe.config.Config;
import com.typesafe.config.ConfigException;
import com.typesafe.config.ConfigFactory;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.common.config.ConfKeyInfo.ConfKeyTags;
import com.yugabyte.yw.common.config.CustomerConfKeys;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.ProviderConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.RuntimeConfigChangeListener;
import com.yugabyte.yw.common.config.RuntimeConfigChangeNotifier;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.config.UniverseConfKeys;
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
  private static final String LIST_KEY_INFO = "/api/runtime_config/mutable_key_info";
  private static final String LIST_SCOPES = "/api/customers/%s/runtime_config/scopes";
  private static final String GET_CONFIG = "/api/customers/%s/runtime_config/%s";
  private static final String GET_CONFIG_INCL_INHERITED = GET_CONFIG + "?includeInherited=true";
  private static final String KEY = "/api/customers/%s/runtime_config/%s/key/%s";
  private static final String GC_CHECK_INTERVAL_KEY = "yb.taskGC.gc_check_interval";
  private static final String EXT_SCRIPT_KEY = "yb.external_script";
  private static final String EXT_SCRIPT_SCHEDULE_KEY = "yb.external_script.schedule";
  private static final String GLOBAL_KEY = "yb.runtime_conf_ui.tag_filter";
  private static final String PROVIDER_KEY = "yb.internal.allow_unsupported_instances";

  private Customer defaultCustomer;
  private Universe defaultUniverse;
  private Provider defaultProvider;
  private String authToken;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    defaultUniverse = ModelFactory.createUniverse(defaultCustomer.getId());
    defaultProvider = ModelFactory.kubernetesProvider(defaultCustomer);
    Users user = ModelFactory.testUser(defaultCustomer, Users.Role.SuperAdmin);
    authToken = user.createAuthToken();
  }

  @Test
  public void authError() {
    Result result =
        Helpers.route(
            app, fakeRequest("GET", String.format(LIST_SCOPES, defaultCustomer.getUuid())));
    assertEquals(UNAUTHORIZED, result.status());
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
        ImmutableSet.copyOf(Json.parse(contentAsString(result)).elements()).stream()
            .map(JsonNode::asText)
            .collect(Collectors.toSet());
    assertTrue(String.valueOf(actualKeys), actualKeys.containsAll(expectedKeys));
  }

  @Test
  public void listKeyInfo() {
    Result result = doRequestWithAuthToken("GET", LIST_KEY_INFO, authToken);
    assertEquals(OK, result.status());
    String actualInfo = contentAsString(result);
    List<String> expectedInfo = new ArrayList<>();
    ObjectMapper objMapper = new ObjectMapper();
    try {
      expectedInfo.add(objMapper.writeValueAsString(CustomerConfKeys.taskGcRetentionDuration));
      expectedInfo.add(objMapper.writeValueAsString(ProviderConfKeys.allowUnsupportedInstances));
      expectedInfo.add(objMapper.writeValueAsString(UniverseConfKeys.allowDowngrades));
      expectedInfo.add(objMapper.writeValueAsString(GlobalConfKeys.auditMaxHistory));

      for (String info : expectedInfo) {
        assertTrue(info, actualInfo.contains(info));
      }
    } catch (JsonProcessingException e) {
      fail(e.getMessage());
    }
  }

  @Test
  public void listScopes() {
    Result result =
        doRequestWithAuthToken(
            "GET", String.format(LIST_SCOPES, defaultCustomer.getUuid()), authToken);
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
    assertTrue(actualScopesMap.get(ScopeType.CUSTOMER).contains(defaultCustomer.getUuid()));
    assertTrue(actualScopesMap.get(ScopeType.PROVIDER).contains(defaultProvider.getUuid()));
    assertTrue(actualScopesMap.get(ScopeType.UNIVERSE).contains(defaultUniverse.getUniverseUUID()));
  }

  @Test
  public void key() {
    String defaultInterval = "1 hour";
    Result res = getKey(GLOBAL_SCOPE_UUID, GC_CHECK_INTERVAL_KEY);
    assertEquals(OK, res.status());
    assertEquals(defaultInterval, contentAsString(res));

    String newInterval = "2 days";
    assertEquals(OK, setGCInterval(newInterval, GLOBAL_SCOPE_UUID).status());
    RuntimeConfigFactory runtimeConfigFactory =
        app.injector().instanceOf(RuntimeConfigFactory.class);
    Duration duration = runtimeConfigFactory.globalRuntimeConf().getDuration(GC_CHECK_INTERVAL_KEY);
    assertEquals(24 * 60 * 2, duration.toMinutes());
    assertEquals(newInterval, contentAsString(getKey(GLOBAL_SCOPE_UUID, GC_CHECK_INTERVAL_KEY)));
    assertEquals(OK, deleteKey(GLOBAL_SCOPE_UUID, GC_CHECK_INTERVAL_KEY).status());
    assertEquals(
        defaultInterval, contentAsString(getKey(GLOBAL_SCOPE_UUID, GC_CHECK_INTERVAL_KEY)));

    // Make sure we get inherited values if applicable.
    res = getKey(defaultProvider.getUuid(), PROVIDER_KEY);
    assertEquals("false", contentAsString(res));
    setKey(PROVIDER_KEY, "true", GLOBAL_SCOPE_UUID);
    res = getKey(defaultProvider.getUuid(), PROVIDER_KEY);
    assertEquals("true", contentAsString(res));
  }

  // Multiline json should be posted as triple quoted string
  // UI should wrap before request such multiline content in
  // triple quotes (and similarly unwrap the response)
  @Test
  public void multilineJsonContent() throws ParseException {
    String metadataKey = "yb.security.oidcProviderMetadata";
    String defaultValue = "";
    Result res = getKey(GLOBAL_SCOPE_UUID, metadataKey);
    assertEquals(OK, res.status());
    assertEquals(defaultValue, contentAsString(res));
    String multilineJsonAsString =
        TestUtils.readResource("com/yugabyte/yw/controllers/test_openid_config.txt");
    String tripleQuoted = "\"\"\"" + multilineJsonAsString + "\"\"\"";
    assertEquals(OK, setKey(metadataKey, tripleQuoted, GLOBAL_SCOPE_UUID).status());
    RuntimeConfigFactory runtimeConfigFactory =
        app.injector().instanceOf(RuntimeConfigFactory.class);
    String m = runtimeConfigFactory.globalRuntimeConf().getString(metadataKey);
    OIDCProviderMetadata metadata = OIDCProviderMetadata.parse(m);
    // internally we get it without quotes
    assertEquals(multilineJsonAsString, m);
    res = getKey(GLOBAL_SCOPE_UUID, metadataKey);
    assertEquals(OK, res.status());

    // triple quotes are preserved on HTTP GET response
    assertEquals(tripleQuoted, contentAsString(res));
  }

  private Result setGCInterval(String interval, UUID scopeUUID) {
    return setKey(GC_CHECK_INTERVAL_KEY, interval, scopeUUID);
  }

  @Test
  public void keyObj() {
    String defaultInterval = "1 hour";
    Result res = getKey(GLOBAL_SCOPE_UUID, GC_CHECK_INTERVAL_KEY);
    assertEquals(OK, res.status());
    assertEquals(defaultInterval, contentAsString(res));

    String newInterval = "2 days";
    String newRetention = "32 days";
    assertEquals(
        OK,
        setExtScriptObject(newInterval, newRetention, defaultUniverse.getUniverseUUID()).status());

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
        assertPlatformException(
                () -> getKey(defaultUniverse.getUniverseUUID(), EXT_SCRIPT_SCHEDULE_KEY))
            .status());

    // Fetch whole object through the API parse it and extract the internal key
    assertEquals(
        newInterval,
        ConfigFactory.parseString(
                contentAsString(getKey(defaultUniverse.getUniverseUUID(), EXT_SCRIPT_KEY)))
            .getString("schedule"));

    assertEquals(
        "If you set an object deleting its internal key should result in key not found",
        NOT_FOUND,
        assertPlatformException(
                () -> deleteKey(defaultUniverse.getUniverseUUID(), EXT_SCRIPT_SCHEDULE_KEY))
            .status());

    assertEquals(
        "Delete of the object key should work",
        OK,
        deleteKey(defaultUniverse.getUniverseUUID(), EXT_SCRIPT_KEY).status());

    // Expect ConfigException since default value in reference.conf is null
    assertThrows(
        ConfigException.WrongType.class,
        () -> getKey(defaultUniverse.getUniverseUUID(), EXT_SCRIPT_KEY));
  }

  private Result setExtScriptObject(String schedule, String content, UUID scopeUUID) {
    return setKey(
        EXT_SCRIPT_KEY,
        String.format(
            "{" + "  schedule = %s\n" + "  params = %s\n" + "  content = \"the script\"\n" + "}",
            schedule, content),
        scopeUUID);
  }

  private Result getKey(UUID scopeUUID, String key) {
    return doRequestWithAuthToken(
        "GET", String.format(KEY, defaultCustomer.getUuid(), scopeUUID, key), authToken);
  }

  private Result deleteKey(UUID universeUUID, String key) {
    return doRequestWithAuthToken(
        "DELETE", String.format(KEY, defaultCustomer.getUuid(), universeUUID, key), authToken);
  }

  @Test
  public void getConfig_global() {
    Result result =
        doRequestWithAuthToken(
            "GET",
            String.format(GET_CONFIG, defaultCustomer.getUuid(), GLOBAL_SCOPE_UUID),
            authToken);
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
            String.format(GET_CONFIG_INCL_INHERITED, defaultCustomer.getUuid(), scopeUUID),
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
        return defaultCustomer.getUuid();
      case UNIVERSE:
        return defaultUniverse.getUniverseUUID();
      case PROVIDER:
        return defaultProvider.getUuid();
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
      //      new Object[] {ScopeType.CUSTOMER, "\"44 seconds\"", "44 seconds"},
      //      new Object[] {ScopeType.PROVIDER, "\"22 hours\"", "22 hours"},
      //      // Set without quotes should be allowed for string objects backward compatibility
      //      // Even when set with quotes we will return string without redundant quotes.
      //      // But we will do proper escaping for special characters
      //      new Object[] {ScopeType.UNIVERSE, "11\"", "11\\\""},
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
  @Parameters({
    "yb.upgrade.vmImage",
    "yb.health.trigger_api.enabled",
    "yb.security.custom_hooks.enable_api_triggered_hooks"
  })
  public void configResolution(String key) {
    RuntimeConfigFactory runtimeConfigFactory =
        app.injector().instanceOf(RuntimeConfigFactory.class);
    assertFalse(runtimeConfigFactory.forUniverse(defaultUniverse).getBoolean(key));
    setCloudEnabled();
    assertTrue(runtimeConfigFactory.forUniverse(defaultUniverse).getBoolean(key));
  }

  private void setCloudEnabled() {
    setKey("yb.cloud.enabled", "true", defaultCustomer.getUuid());
  }

  @Test
  public void testFailingListener() {
    assertEquals(
        NOT_FOUND,
        assertPlatformException(
                () -> getKey(defaultUniverse.getUniverseUUID(), GC_CHECK_INTERVAL_KEY))
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

          public void processGlobal() {
            throw new PlatformServiceException(INTERNAL_SERVER_ERROR, "Some error");
          }
        });
    assertEquals(
        INTERNAL_SERVER_ERROR,
        assertPlatformException(() -> setGCInterval(newInterval, GLOBAL_SCOPE_UUID)).status());
    assertEquals(
        NOT_FOUND,
        assertPlatformException(
                () -> getKey(defaultUniverse.getUniverseUUID(), GC_CHECK_INTERVAL_KEY))
            .status());
  }

  private Result setKey(String path, String newVal, UUID scopeUUID) {
    Http.RequestBuilder request =
        fakeRequest("PUT", String.format(KEY, defaultCustomer.getUuid(), scopeUUID, path))
            .header("X-AUTH-TOKEN", authToken)
            .bodyText(newVal);
    return route(request);
  }

  @Test
  public void scopeStrictnessTest() {
    // Global key can only be set in global scope
    Result r =
        assertPlatformException(
            () -> setKey(GLOBAL_KEY, "[\"PUBLIC\"]", defaultCustomer.getUuid()));
    assertEquals(BAD_REQUEST, r.status());
    JsonNode rJson = Json.parse(contentAsString(r));
    assertValue(rJson, "error", "Cannot set the key in this scope");

    r = setKey(GLOBAL_KEY, "[\"PUBLIC\"]", GLOBAL_SCOPE_UUID);
    final RuntimeConfGetter runtimeConfGetter = app.injector().instanceOf(RuntimeConfGetter.class);
    List<ConfKeyTags> tags = runtimeConfGetter.getGlobalConf(GlobalConfKeys.tagList);
    assertEquals(ConfKeyTags.PUBLIC, tags.get(0));
    assertEquals(OK, r.status());
  }

  @Test
  public void dataValidationTest() {
    Result r = assertPlatformException(() -> setKey(GLOBAL_KEY, "Random", GLOBAL_SCOPE_UUID));
    assertEquals(BAD_REQUEST, r.status());
    JsonNode rJson = Json.parse(contentAsString(r));
    assertValue(
        rJson,
        "error",
        "Not a valid list of tags." + "All possible tags are " + "PUBLIC, BETA, INTERNAL");
  }

  @Test
  public void catchKeysWithNoMetadata() {

    Result result = doRequestWithAuthToken("GET", LIST_KEYS, authToken);
    assertEquals(OK, result.status());
    Set<String> listKeys =
        ImmutableSet.copyOf(Json.parse(contentAsString(result)).elements()).stream()
            .map(JsonNode::asText)
            .collect(Collectors.toSet());

    result = doRequestWithAuthToken("GET", LIST_KEY_INFO, authToken);
    assertEquals(OK, result.status());
    Set<String> metaKeys =
        ImmutableSet.copyOf(Json.parse(contentAsString(result))).stream()
            .map(JsonNode -> JsonNode.get("key"))
            .map(JsonNode::asText)
            .collect(Collectors.toSet());

    for (String key : listKeys) {
      if (!metaKeys.contains(key) && !validExcludedKey(key)) {
        String failMsg =
            String.format(
                "Please define information for this key \"%s\" in one of "
                    + "GlobalConfKeys, ProviderConfKeys , CustomerConfKeys or UniverseConfKeys."
                    + "If you have questions post it to #runtime-config channel."
                    + "Also see "
                    + "https://docs.google.com/document/d/"
                    + "1NAURMNdtOexYnfYN9mOSDChtrP2T4qkRhxsFdah7uwM/edit?usp=sharing",
                key);
        fail(failMsg);
      }
    }
  }

  private boolean validExcludedKey(String path) {
    Set<String> excludedKeys =
        ImmutableSet.of(
            "yb.alert.slack.ws",
            "yb.alert.webhook.ws",
            "yb.alert.pagerduty.ws",
            "yb.external_script",
            "yb.ha.ws",
            "yb.query_stats.live_queries.ws",
            "yb.metrics.ws",
            "yb.troubleshooting.ws",
            "yb.perf_advisor",
            // TODO (PLAT-7110)
            "yb.releases.path",
            "yb.universe.user_tags.accepted_values");
    assertEquals(
        "Do not modify this list to get the test to pass without discussing "
            + "on #runtime-config channel.",
        11,
        excludedKeys.size());
    for (String key : excludedKeys) {
      if (path.startsWith(key)) return true;
    }
    return false;
  }
}
