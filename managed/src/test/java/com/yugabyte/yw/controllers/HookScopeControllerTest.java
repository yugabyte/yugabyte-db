// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertAuditEntry;
import static com.yugabyte.yw.common.AssertHelper.assertBadRequest;
import static com.yugabyte.yw.common.AssertHelper.assertUnauthorized;
import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static com.yugabyte.yw.common.TestHelper.createTempFile;
import static com.yugabyte.yw.common.TestHelper.testDatabase;
import static org.junit.Assert.assertTrue;
import static play.test.Helpers.contentAsString;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;
import static play.inject.Bindings.bind;

import akka.stream.javadsl.FileIO;
import akka.stream.javadsl.Source;
import akka.util.ByteString;
import com.typesafe.config.Config;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.HealthChecker;
import com.yugabyte.yw.common.CustomWsClientFactory;
import com.yugabyte.yw.common.CustomWsClientFactoryProvider;
import com.yugabyte.yw.common.FakeApiHelper;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.config.DummyRuntimeConfigFactoryImpl;
import com.yugabyte.yw.models.Hook.ExecutionLang;
import com.yugabyte.yw.models.HookScope.TriggerType;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.Users.Role;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.HookScope;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import org.junit.Before;
import java.io.File;
import java.util.UUID;
import java.util.List;
import java.util.ArrayList;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import org.mockito.Mock;
import play.Application;
import play.inject.guice.GuiceApplicationBuilder;
import play.test.WithApplication;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;

@RunWith(MockitoJUnitRunner.class)
public class HookScopeControllerTest extends WithApplication {

  @Mock Config mockConfig;

  String baseRoute;
  Customer defaultCustomer;
  Users defaultUser, superAdminUser;
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
    superAdminUser =
        ModelFactory.testUser(defaultCustomer, "superadmin@customer.com", Role.SuperAdmin);
    defaultProvider = ModelFactory.awsProvider(defaultCustomer);
    defaultUniverse = ModelFactory.createUniverse();
    baseRoute = "/api/customers/" + defaultCustomer.uuid + "/hook_scopes";
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
    String uri = "/api/customers/" + defaultCustomer.uuid + "/hooks";
    return FakeApiHelper.doRequestWithAuthTokenAndMultipartData(
        "POST", uri, superAdminUser.createAuthToken(), bodyData, mat);
  }

  private Result createHookScope(
      TriggerType triggerType, UUID providerUUID, UUID universeUUID, Users user) {
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("triggerType", triggerType.name());
    if (providerUUID != null) bodyJson.put("providerUUID", providerUUID.toString());
    if (universeUUID != null) bodyJson.put("universeUUID", universeUUID.toString());
    return FakeApiHelper.doRequestWithAuthTokenAndBody(
        "POST", baseRoute, user.createAuthToken(), bodyJson);
  }

  @Test
  public void testCreateGlobalHookScope() {
    Result result = createHookScope(TriggerType.PreNodeProvision, null, null, superAdminUser);
    JsonNode json = Json.parse(contentAsString(result));
    assertOk(result);
    assertValue(json, "triggerType", "PreNodeProvision");
    assertAuditEntry(1, defaultCustomer.uuid);

    // Ensure persistence
    String hookScopeUUID = json.get("uuid").asText();
    HookScope hookScope =
        HookScope.getOrBadRequest(defaultCustomer.uuid, UUID.fromString(hookScopeUUID));
    assertTrue(hookScope.triggerType == TriggerType.PreNodeProvision);
    assertTrue(hookScope.providerUUID == null);
    assertTrue(hookScope.universeUUID == null);
  }

  @Test
  public void testCreateProviderHookScopeValid() {
    Result result =
        createHookScope(TriggerType.PreNodeProvision, defaultProvider.uuid, null, superAdminUser);
    JsonNode json = Json.parse(contentAsString(result));
    assertOk(result);
    assertValue(json, "triggerType", "PreNodeProvision");
    assertValue(json, "providerUUID", defaultProvider.uuid.toString());
    assertAuditEntry(1, defaultCustomer.uuid);

    // Ensure persistence
    String hookScopeUUID = json.get("uuid").asText();
    HookScope hookScope =
        HookScope.getOrBadRequest(defaultCustomer.uuid, UUID.fromString(hookScopeUUID));
    assertTrue(hookScope.triggerType == TriggerType.PreNodeProvision);
    assertTrue(hookScope.providerUUID.equals(defaultProvider.uuid));
    assertTrue(hookScope.universeUUID == null);
  }

  @Test
  public void testCreateProviderHookScopeInvalid() {
    UUID dummyUUID = UUID.randomUUID();
    Result result =
        assertPlatformException(
            () -> createHookScope(TriggerType.PreNodeProvision, dummyUUID, null, superAdminUser));
    assertBadRequest(result, "Invalid Provider UUID: " + dummyUUID.toString());
    assertAuditEntry(0, defaultCustomer.uuid);
  }

  @Test
  public void testCreateUniverseHookScopeValid() {
    Result result =
        createHookScope(
            TriggerType.PostNodeProvision, null, defaultUniverse.universeUUID, superAdminUser);
    JsonNode json = Json.parse(contentAsString(result));
    assertOk(result);
    assertValue(json, "triggerType", "PostNodeProvision");
    assertValue(json, "universeUUID", defaultUniverse.universeUUID.toString());
    assertAuditEntry(1, defaultCustomer.uuid);

    // Ensure persistence
    String hookScopeUUID = json.get("uuid").asText();
    HookScope hookScope =
        HookScope.getOrBadRequest(defaultCustomer.uuid, UUID.fromString(hookScopeUUID));
    assertTrue(hookScope.triggerType == TriggerType.PostNodeProvision);
    assertTrue(hookScope.providerUUID == null);
    assertTrue(hookScope.universeUUID.equals(defaultUniverse.universeUUID));
  }

  @Test
  public void testCreateUniverseHookScopeInvalidUniverse() {
    UUID dummyUUID = UUID.randomUUID();
    Result result =
        assertPlatformException(
            () -> createHookScope(TriggerType.PostNodeProvision, null, dummyUUID, superAdminUser));
    assertBadRequest(result, "Cannot find universe " + dummyUUID.toString());
    assertAuditEntry(0, defaultCustomer.uuid);
  }

  @Test
  public void testCreateHookScopeNonSuperAdmin() {
    Result result =
        assertPlatformException(
            () -> createHookScope(TriggerType.PreNodeProvision, null, null, defaultUser));
    assertUnauthorized(result, "Only Super Admins can perform this operation.");
    assertAuditEntry(0, defaultCustomer.uuid);
  }

  @Test
  public void testCreateNonUniqueHookScope() {
    Result result = createHookScope(TriggerType.PreNodeProvision, null, null, superAdminUser);
    result =
        assertPlatformException(
            () -> createHookScope(TriggerType.PreNodeProvision, null, null, superAdminUser));
    assertBadRequest(result, "Hook scope with this scope ID and trigger already exists");
    assertAuditEntry(1, defaultCustomer.uuid);
  }

  @Test
  public void testListHookScopes() {
    createHookScope(TriggerType.PreNodeProvision, null, null, superAdminUser);
    createHookScope(TriggerType.PreNodeProvision, defaultProvider.uuid, null, superAdminUser);
    Result result =
        FakeApiHelper.doRequestWithAuthToken("GET", baseRoute, superAdminUser.createAuthToken());
    JsonNode json = Json.parse(contentAsString(result));
    assertOk(result);
    assertTrue(json.size() == 2);
  }

  @Test
  public void testListHookScopesNonSuperAdmin() {
    createHookScope(TriggerType.PreNodeProvision, null, null, superAdminUser);
    createHookScope(TriggerType.PreNodeProvision, defaultProvider.uuid, null, superAdminUser);
    Result result =
        assertPlatformException(
            () ->
                FakeApiHelper.doRequestWithAuthToken(
                    "GET", baseRoute, defaultUser.createAuthToken()));
    assertUnauthorized(result, "Only Super Admins can perform this operation.");
  }

  @Test
  public void testDeleteHookScope() {
    Result result = createHookScope(TriggerType.PreNodeProvision, null, null, superAdminUser);
    JsonNode json = Json.parse(contentAsString(result));
    String uuid = json.get("uuid").asText();
    String uri = baseRoute + "/" + uuid;

    Result deleteResult =
        FakeApiHelper.doRequestWithAuthToken("DELETE", uri, superAdminUser.createAuthToken());
    assertOk(deleteResult);
    assertAuditEntry(2, defaultCustomer.uuid);
  }

  @Test
  public void testDeleteHookScopeNonSuperAdmin() {
    Result result = createHookScope(TriggerType.PreNodeProvision, null, null, superAdminUser);
    JsonNode json = Json.parse(contentAsString(result));
    String uuid = json.get("uuid").asText();
    String uri = baseRoute + "/" + uuid;

    Result deleteResult =
        assertPlatformException(
            () ->
                FakeApiHelper.doRequestWithAuthToken("DELETE", uri, defaultUser.createAuthToken()));
    assertUnauthorized(deleteResult, "Only Super Admins can perform this operation.");
    assertAuditEntry(1, defaultCustomer.uuid);
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
            () -> FakeApiHelper.doRequestWithAuthToken("POST", uri, defaultUser.createAuthToken()));
    assertUnauthorized(result, "Only Super Admins can perform this operation.");

    // Attach the hook to the hook scope
    result = FakeApiHelper.doRequestWithAuthToken("POST", uri, superAdminUser.createAuthToken());
    json = Json.parse(contentAsString((result)));
    json = json.get("hooks");
    String returnedHookUUID = json.get(0).get("uuid").asText();
    assertTrue(json.size() == 1);
    assertTrue(returnedHookUUID.equals(hookUUID));
    assertOk(result);

    // Detaching the hook with non super admin should fail
    result =
        assertPlatformException(
            () ->
                FakeApiHelper.doRequestWithAuthToken("DELETE", uri, defaultUser.createAuthToken()));
    assertUnauthorized(result, "Only Super Admins can perform this operation.");

    // Detach the hook from the hook scope
    result = FakeApiHelper.doRequestWithAuthToken("DELETE", uri, superAdminUser.createAuthToken());
    json = Json.parse(contentAsString((result)));
    json = json.get("hooks");
    assertTrue(json.size() == 0); // hook was removed
    assertOk(result);

    // Detaching the hook again should fail
    result =
        assertPlatformException(
            () ->
                FakeApiHelper.doRequestWithAuthToken(
                    "DELETE", uri, superAdminUser.createAuthToken()));
    assertBadRequest(
        result, "Hook " + hookUUID + " is not attached to hook scope " + hookScopeUUID);
    assertAuditEntry(4, defaultCustomer.uuid);
  }

  @Test
  public void testOperationWithoutCustomHooksEnabled() {
    when(mockConfig.getBoolean(HookController.ENABLE_CUSTOM_HOOKS_PATH)).thenReturn(false);
    Result result =
        assertPlatformException(
            () -> createHookScope(TriggerType.PreNodeProvision, null, null, superAdminUser));
    assertUnauthorized(result, "Custom hooks is not enabled on this Anywhere instance");
    assertAuditEntry(0, defaultCustomer.uuid);
  }
}
