// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertAuditEntry;
import static com.yugabyte.yw.common.AssertHelper.assertBadRequest;
import static com.yugabyte.yw.common.AssertHelper.assertForbiddenWithException;
import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static com.yugabyte.yw.common.AssertHelper.assertUnauthorized;
import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static com.yugabyte.yw.common.TestHelper.createTempFile;
import static com.yugabyte.yw.common.TestHelper.testDatabase;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;
import static play.inject.Bindings.bind;
import static play.test.Helpers.contentAsString;

import akka.stream.javadsl.FileIO;
import akka.stream.javadsl.Source;
import akka.util.ByteString;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.HealthChecker;
import com.yugabyte.yw.common.CustomWsClientFactory;
import com.yugabyte.yw.common.CustomWsClientFactoryProvider;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformGuiceApplicationBaseTest;
import com.yugabyte.yw.common.config.DummyRuntimeConfigFactoryImpl;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Hook.ExecutionLang;
import com.yugabyte.yw.models.HookScope;
import com.yugabyte.yw.models.HookScope.TriggerType;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.Users.Role;
import java.io.File;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;
import play.Application;
import play.inject.guice.GuiceApplicationBuilder;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;

@RunWith(MockitoJUnitRunner.class)
public class HookScopeControllerTest extends PlatformGuiceApplicationBaseTest {

  @Mock Config mockConfig;

  String baseRoute;
  Customer defaultCustomer;
  Users defaultUser, adminUser, superAdminUser;
  Provider defaultProvider;
  Universe defaultUniverse;

  @Override
  protected Application provideApplication() {
    when(mockConfig.getBoolean(HookScopeController.ENABLE_CUSTOM_HOOKS_PATH)).thenReturn(true);
    return new GuiceApplicationBuilder()
        .configure(testDatabase())
        .overrides(
            bind(RuntimeConfigFactory.class)
                .toInstance(new DummyRuntimeConfigFactoryImpl(mockConfig)))
        .overrides(bind(HealthChecker.class).toInstance(mock(HealthChecker.class)))
        .overrides(
            bind(CustomWsClientFactory.class).toProvider(CustomWsClientFactoryProvider.class))
        .build();
  }

  @Before
  public void before() {
    defaultCustomer = ModelFactory.testCustomer();
    defaultUser = ModelFactory.testUser(defaultCustomer);
    adminUser = ModelFactory.testUser(defaultCustomer, "admin@customer.com", Role.Admin);
    superAdminUser =
        ModelFactory.testUser(defaultCustomer, "superadmin@customer.com", Role.SuperAdmin);
    defaultProvider = ModelFactory.awsProvider(defaultCustomer);
    defaultUniverse = ModelFactory.createUniverse();
    baseRoute = "/api/customers/" + defaultCustomer.getUuid() + "/hook_scopes";
  }

  private List<Http.MultipartFormData.Part<Source<ByteString, ?>>> getCreateHookMultiPartData(
      String name, ExecutionLang executionLang, String hookText, boolean useSudo) {
    List<Http.MultipartFormData.Part<Source<ByteString, ?>>> bodyData = new ArrayList<>();
    bodyData.add(new Http.MultipartFormData.DataPart("name", name));
    bodyData.add(new Http.MultipartFormData.DataPart("executionLang", executionLang.name()));
    bodyData.add(new Http.MultipartFormData.DataPart("useSudo", String.valueOf(useSudo)));
    String tmpFile = createTempFile(hookText);
    Source<ByteString, ?> hookFile = FileIO.fromFile(new File(tmpFile));
    bodyData.add(
        new Http.MultipartFormData.FilePart<>(
            "hookFile", name, "application/octet-stream", hookFile));
    return bodyData;
  }

  private Result createHook(
      String name, ExecutionLang executionLang, String hookText, boolean useSudo) {
    List<Http.MultipartFormData.Part<Source<ByteString, ?>>> bodyData =
        getCreateHookMultiPartData(name, executionLang, hookText, useSudo);
    String uri = "/api/customers/" + defaultCustomer.getUuid() + "/hooks";
    return doRequestWithAuthTokenAndMultipartData(
        "POST", uri, superAdminUser.createAuthToken(), bodyData, mat);
  }

  private Result createHookScope(
      TriggerType triggerType, UUID providerUUID, UUID universeUUID, Users user) {
    return createHookScope(triggerType, providerUUID, universeUUID, null, user);
  }

  private Result createHookScope(
      TriggerType triggerType, UUID providerUUID, UUID universeUUID, UUID clusterUUID, Users user) {
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("triggerType", triggerType.name());
    if (providerUUID != null) bodyJson.put("providerUUID", providerUUID.toString());
    if (universeUUID != null) bodyJson.put("universeUUID", universeUUID.toString());
    if (clusterUUID != null) bodyJson.put("clusterUUID", clusterUUID.toString());
    return doRequestWithAuthTokenAndBody("POST", baseRoute, user.createAuthToken(), bodyJson);
  }

  @Test
  public void testCreateGlobalHookScope() {
    Result result = createHookScope(TriggerType.PreNodeProvision, null, null, superAdminUser);
    JsonNode json = Json.parse(contentAsString(result));
    assertOk(result);
    assertValue(json, "triggerType", "PreNodeProvision");
    assertAuditEntry(1, defaultCustomer.getUuid());

    // Ensure persistence
    String hookScopeUUID = json.get("uuid").asText();
    HookScope hookScope =
        HookScope.getOrBadRequest(defaultCustomer.getUuid(), UUID.fromString(hookScopeUUID));
    assertTrue(hookScope.getTriggerType() == TriggerType.PreNodeProvision);
    assertTrue(hookScope.getProviderUUID() == null);
    assertTrue(hookScope.getUniverseUUID() == null);
  }

  @Test
  public void testCreateProviderHookScopeValid() {
    Result result =
        createHookScope(
            TriggerType.PreNodeProvision, defaultProvider.getUuid(), null, superAdminUser);
    JsonNode json = Json.parse(contentAsString(result));
    assertOk(result);
    assertValue(json, "triggerType", "PreNodeProvision");
    assertValue(json, "providerUUID", defaultProvider.getUuid().toString());
    assertAuditEntry(1, defaultCustomer.getUuid());

    // Ensure persistence
    String hookScopeUUID = json.get("uuid").asText();
    HookScope hookScope =
        HookScope.getOrBadRequest(defaultCustomer.getUuid(), UUID.fromString(hookScopeUUID));
    assertTrue(hookScope.getTriggerType() == TriggerType.PreNodeProvision);
    assertTrue(hookScope.getProviderUUID().equals(defaultProvider.getUuid()));
    assertTrue(hookScope.getUniverseUUID() == null);
  }

  @Test
  public void testCreateProviderHookScopeInvalid() {
    UUID dummyUUID = UUID.randomUUID();
    Result result =
        assertPlatformException(
            () -> createHookScope(TriggerType.PreNodeProvision, dummyUUID, null, superAdminUser));
    assertBadRequest(result, "Invalid Provider UUID: " + dummyUUID.toString());
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testCreateUniverseHookScopeValid() {
    Result result =
        createHookScope(
            TriggerType.PostNodeProvision, null, defaultUniverse.getUniverseUUID(), superAdminUser);
    JsonNode json = Json.parse(contentAsString(result));
    assertOk(result);
    assertValue(json, "triggerType", "PostNodeProvision");
    assertValue(json, "universeUUID", defaultUniverse.getUniverseUUID().toString());
    assertAuditEntry(1, defaultCustomer.getUuid());

    // Ensure persistence
    String hookScopeUUID = json.get("uuid").asText();
    HookScope hookScope =
        HookScope.getOrBadRequest(defaultCustomer.getUuid(), UUID.fromString(hookScopeUUID));
    assertTrue(hookScope.getTriggerType() == TriggerType.PostNodeProvision);
    assertTrue(hookScope.getProviderUUID() == null);
    assertTrue(hookScope.getUniverseUUID().equals(defaultUniverse.getUniverseUUID()));
  }

  @Test
  public void testCreateUniverseHookScopeInvalidUniverse() {
    UUID dummyUUID = UUID.randomUUID();
    Result result =
        assertPlatformException(
            () -> createHookScope(TriggerType.PostNodeProvision, null, dummyUUID, superAdminUser));
    assertBadRequest(result, "Cannot find universe " + dummyUUID.toString());
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testCreateUniverseClusterHookScopeValid() {
    UUID clusterUUID = UUID.randomUUID();
    Result result =
        createHookScope(
            TriggerType.PostNodeProvision,
            null,
            defaultUniverse.getUniverseUUID(),
            clusterUUID,
            superAdminUser);
    JsonNode json = Json.parse(contentAsString(result));
    assertOk(result);
    assertValue(json, "triggerType", "PostNodeProvision");
    assertValue(json, "universeUUID", defaultUniverse.getUniverseUUID().toString());
    assertValue(json, "clusterUUID", clusterUUID.toString());
    assertAuditEntry(1, defaultCustomer.getUuid());

    // Ensure persistence
    String hookScopeUUID = json.get("uuid").asText();
    HookScope hookScope =
        HookScope.getOrBadRequest(defaultCustomer.getUuid(), UUID.fromString(hookScopeUUID));
    assertTrue(hookScope.getTriggerType() == TriggerType.PostNodeProvision);
    assertTrue(hookScope.getProviderUUID() == null);
    assertTrue(hookScope.getUniverseUUID().equals(defaultUniverse.getUniverseUUID()));
    assertTrue(hookScope.getClusterUUID().equals(clusterUUID));
  }

  @Test
  public void testCreateHookScopeNonSuperAdmin() {
    Result result =
        assertPlatformException(
            () -> createHookScope(TriggerType.PreNodeProvision, null, null, defaultUser));
    assertForbiddenWithException(result, "Only Super Admins can perform this operation.");
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testCreateHookScopeAdminInCloud() {
    when(mockConfig.getBoolean("yb.cloud.enabled")).thenReturn(true);
    List<Users> users = new ArrayList<>(Arrays.asList(adminUser, superAdminUser));
    int i = 0;

    for (Users user : users) {
      TriggerType trigger = i++ == 0 ? TriggerType.PostNodeProvision : TriggerType.PreNodeProvision;

      Result result = createHookScope(trigger, null, null, user);
      JsonNode json = Json.parse(contentAsString(result));
      assertOk(result);
      assertValue(json, "triggerType", trigger.toString());
      assertAuditEntry(i, defaultCustomer.getUuid());

      // Ensure persistence
      String hookScopeUUID = json.get("uuid").asText();
      HookScope hookScope =
          HookScope.getOrBadRequest(defaultCustomer.getUuid(), UUID.fromString(hookScopeUUID));
      assertTrue(hookScope.getTriggerType() == trigger);
      assertTrue(hookScope.getProviderUUID() == null);
      assertTrue(hookScope.getUniverseUUID() == null);
    }
  }

  @Test
  public void testCreateNonUniqueHookScope() {
    Result result = createHookScope(TriggerType.PreNodeProvision, null, null, superAdminUser);
    result =
        assertPlatformException(
            () -> createHookScope(TriggerType.PreNodeProvision, null, null, superAdminUser));
    assertBadRequest(result, "Hook scope with this scope ID and trigger already exists");
    assertAuditEntry(1, defaultCustomer.getUuid());
  }

  @Test
  public void testListHookScopes() {
    createHookScope(TriggerType.PreNodeProvision, null, null, superAdminUser);
    createHookScope(TriggerType.PreNodeProvision, defaultProvider.getUuid(), null, superAdminUser);
    Result result = doRequestWithAuthToken("GET", baseRoute, superAdminUser.createAuthToken());
    JsonNode json = Json.parse(contentAsString(result));
    assertOk(result);
    assertTrue(json.size() == 2);
  }

  @Test
  public void testListHookScopesNonSuperAdmin() {
    createHookScope(TriggerType.PreNodeProvision, null, null, superAdminUser);
    createHookScope(TriggerType.PreNodeProvision, defaultProvider.getUuid(), null, superAdminUser);
    Result result =
        assertPlatformException(
            () -> doRequestWithAuthToken("GET", baseRoute, defaultUser.createAuthToken()));
    assertForbiddenWithException(result, "Only Super Admins can perform this operation.");
  }

  @Test
  public void testDeleteHookScope() {
    Result result = createHookScope(TriggerType.PreNodeProvision, null, null, superAdminUser);
    JsonNode json = Json.parse(contentAsString(result));
    String uuid = json.get("uuid").asText();
    String uri = baseRoute + "/" + uuid;

    Result deleteResult = doRequestWithAuthToken("DELETE", uri, superAdminUser.createAuthToken());
    assertOk(deleteResult);
    assertAuditEntry(2, defaultCustomer.getUuid());
  }

  @Test
  public void testDeleteHookScopeNonSuperAdmin() {
    Result result = createHookScope(TriggerType.PreNodeProvision, null, null, superAdminUser);
    JsonNode json = Json.parse(contentAsString(result));
    String uuid = json.get("uuid").asText();
    String uri = baseRoute + "/" + uuid;

    Result deleteResult =
        assertPlatformException(
            () -> doRequestWithAuthToken("DELETE", uri, defaultUser.createAuthToken()));
    assertForbiddenWithException(deleteResult, "Only Super Admins can perform this operation.");
    assertAuditEntry(1, defaultCustomer.getUuid());
  }

  @Test
  public void testAddAndRemoveHook() {
    // Create primary hook scope
    Result result = createHookScope(TriggerType.PreNodeProvision, null, null, superAdminUser);
    JsonNode json = Json.parse(contentAsString(result));
    String hookScopeUUID = json.get("uuid").asText();

    // Create hook
    result = createHook("test.py", ExecutionLang.Python, "DEFAULT\nTEXT\n", false);
    json = Json.parse(contentAsString(result));
    String hookUUID = json.get("uuid").asText();
    String uri = baseRoute + "/" + hookScopeUUID + "/hooks/" + hookUUID;

    // Attaching the hook with non super admin should fail
    result =
        assertPlatformException(
            () -> doRequestWithAuthToken("POST", uri, defaultUser.createAuthToken()));
    assertForbiddenWithException(result, "Only Super Admins can perform this operation.");

    // Attach the hook to the hook scope
    result = doRequestWithAuthToken("POST", uri, superAdminUser.createAuthToken());
    json = Json.parse(contentAsString((result)));
    json = json.get("hooks");
    String returnedHookUUID = json.get(0).get("uuid").asText();
    assertTrue(json.size() == 1);
    assertTrue(returnedHookUUID.equals(hookUUID));
    assertOk(result);

    // Detaching the hook with non super admin should fail
    result =
        assertPlatformException(
            () -> doRequestWithAuthToken("DELETE", uri, defaultUser.createAuthToken()));
    assertForbiddenWithException(result, "Only Super Admins can perform this operation.");

    // Detach the hook from the hook scope
    result = doRequestWithAuthToken("DELETE", uri, superAdminUser.createAuthToken());
    json = Json.parse(contentAsString((result)));
    json = json.get("hooks");
    assertTrue(json.size() == 0); // hook was removed
    assertOk(result);

    // Detaching the hook again should fail
    result =
        assertPlatformException(
            () -> doRequestWithAuthToken("DELETE", uri, superAdminUser.createAuthToken()));
    assertBadRequest(
        result, "Hook " + hookUUID + " is not attached to hook scope " + hookScopeUUID);
    assertAuditEntry(4, defaultCustomer.getUuid());
  }

  @Test
  public void testOperationWithoutCustomHooksEnabled() {
    when(mockConfig.getBoolean(HookController.ENABLE_CUSTOM_HOOKS_PATH)).thenReturn(false);
    Result result =
        assertPlatformException(
            () -> createHookScope(TriggerType.PreNodeProvision, null, null, superAdminUser));
    assertUnauthorized(result, "Custom hooks is not enabled on this Anywhere instance");
    assertAuditEntry(0, defaultCustomer.getUuid());
  }
}
