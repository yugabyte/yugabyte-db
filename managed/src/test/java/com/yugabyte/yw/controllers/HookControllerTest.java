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
import static com.yugabyte.yw.models.Hook.ExecutionLang.Bash;
import static com.yugabyte.yw.models.Hook.ExecutionLang.Python;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;
import static play.inject.Bindings.bind;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.HealthChecker;
import com.yugabyte.yw.common.CustomWsClientFactory;
import com.yugabyte.yw.common.CustomWsClientFactoryProvider;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformGuiceApplicationBaseTest;
import com.yugabyte.yw.common.config.DummyRuntimeConfigFactoryImpl;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Hook;
import com.yugabyte.yw.models.Hook.ExecutionLang;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.Users.Role;
import com.yugabyte.yw.models.helpers.TaskType;
import java.io.File;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import org.apache.pekko.stream.javadsl.FileIO;
import org.apache.pekko.stream.javadsl.Source;
import org.apache.pekko.util.ByteString;
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
public class HookControllerTest extends PlatformGuiceApplicationBaseTest {

  @Mock Config mockConfig;
  @Mock Commissioner mockCommissioner;

  String baseRoute;
  Customer defaultCustomer;
  Users defaultUser, superAdminUser;
  Map<String, String> defaultArgs, alternateArgs;

  @Override
  protected Application provideApplication() {
    when(mockConfig.getBoolean(HookController.ENABLE_CUSTOM_HOOKS_PATH)).thenReturn(true);
    when(mockConfig.getBoolean(HookController.ENABLE_SUDO_PATH)).thenReturn(true);
    when(mockConfig.getBoolean(HookController.ENABLE_API_HOOK_RUN_PATH)).thenReturn(true);
    return new GuiceApplicationBuilder()
        .configure(testDatabase())
        .overrides(
            bind(RuntimeConfigFactory.class)
                .toInstance(new DummyRuntimeConfigFactoryImpl(mockConfig)))
        .overrides(bind(Commissioner.class).toInstance(mockCommissioner))
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
    baseRoute = "/api/customers/" + defaultCustomer.getUuid() + "/hooks";
    defaultArgs = new HashMap<>();
    defaultArgs.put("KEY1", "123456789");
    defaultArgs.put("OPTION1", "ABCDEFGH");
    alternateArgs = new HashMap<>();
    alternateArgs.put("KEY2", "QWERTYUIOP");
  }

  private List<Http.MultipartFormData.Part<Source<ByteString, ?>>> getCreateHookMultiPartData(
      String name,
      ExecutionLang executionLang,
      String hookText,
      boolean useSudo,
      Map<String, String> runtimeArgs) {
    List<Http.MultipartFormData.Part<Source<ByteString, ?>>> bodyData = new ArrayList<>();
    bodyData.add(new Http.MultipartFormData.DataPart("name", name));
    bodyData.add(new Http.MultipartFormData.DataPart("executionLang", executionLang.name()));
    bodyData.add(new Http.MultipartFormData.DataPart("useSudo", String.valueOf(useSudo)));
    if (runtimeArgs != null) {
      for (Map.Entry<String, String> entry : runtimeArgs.entrySet()) {
        String path = "runtimeArgs[" + entry.getKey() + "]";
        bodyData.add(new Http.MultipartFormData.DataPart(path, entry.getValue()));
      }
    }
    String tmpFile = createTempFile(hookText);
    Source<ByteString, ?> hookFile = FileIO.fromFile(new File(tmpFile));
    bodyData.add(
        new Http.MultipartFormData.FilePart<>(
            "hookFile", name, "application/octet-stream", hookFile));
    return bodyData;
  }

  private Result createHook(
      String name, ExecutionLang executionLang, String hookText, boolean useSudo, Users user) {
    return createHook(name, executionLang, hookText, useSudo, defaultArgs, user);
  }

  private Result createHook(
      String name,
      ExecutionLang executionLang,
      String hookText,
      boolean useSudo,
      Map<String, String> runtimeArgs,
      Users user) {
    List<Http.MultipartFormData.Part<Source<ByteString, ?>>> bodyData =
        getCreateHookMultiPartData(name, executionLang, hookText, useSudo, runtimeArgs);
    return doRequestWithAuthTokenAndMultipartData(
        "POST", baseRoute, user.createAuthToken(), bodyData, mat);
  }

  @Test
  public void testCreateHook() {
    Result result = createHook("test.py", Python, "DEFAULT\nTEXT\n", true, superAdminUser);
    JsonNode json = Json.parse(contentAsString(result));
    assertOk(result);
    assertValue(json, "name", "test.py");
    assertValue(json, "hookText", "DEFAULT\nTEXT\n");
    assertValue(json, "useSudo", "true");
    assertValue(json, "executionLang", "Python");
    assertValue(json.get("runtimeArgs"), "KEY1", "123456789");
    assertValue(json.get("runtimeArgs"), "OPTION1", "ABCDEFGH");

    // Ensure persistence
    String hookUUID = json.get("uuid").asText();
    Hook hook = Hook.getOrBadRequest(defaultCustomer.getUuid(), UUID.fromString(hookUUID));
    assertTrue(hook.getName().equals("test.py"));
    assertTrue(hook.getHookText().equals("DEFAULT\nTEXT\n"));
    assertTrue(hook.isUseSudo() == true);
    assertTrue(hook.getExecutionLang() == ExecutionLang.Python);
    Map<String, String> persistedArgs = hook.getRuntimeArgs();
    assertTrue(persistedArgs.size() == 2);
    assertTrue(persistedArgs.get("KEY1").equals("123456789"));
    assertTrue(persistedArgs.get("OPTION1").equals("ABCDEFGH"));

    // Ensure audit entry has the runtime args redacted and the hook text logged.
    assertAuditEntry(1, defaultCustomer.getUuid());
    List<Audit> entries = Audit.getAll(defaultCustomer.getUuid());
    JsonNode payload = entries.get(0).getPayload();
    assertValue(payload, "hookText", "DEFAULT\nTEXT\n");
    assertValue(payload.get("runtimeArgs"), "KEY1", "123456789");
    assertValue(payload.get("runtimeArgs"), "OPTION1", "ABCDEFGH");
  }

  @Test
  public void testCreateHookWithSudoDisabled() {
    when(mockConfig.getBoolean(HookController.ENABLE_SUDO_PATH)).thenReturn(false);
    Result result =
        assertPlatformException(
            () -> createHook("test.py", Python, "DEFAULT\nTEXT\n", true, superAdminUser));
    assertUnauthorized(
        result,
        "Creating custom hooks with superuser privileges is not enabled on this Anywhere instance");
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testCreateHookWithSameName() {
    createHook("test.py", Python, "DEFAULT\nTEXT\n", true, superAdminUser);
    Result result =
        assertPlatformException(
            () -> createHook("test.py", Python, "NEW\nTEXT\n", false, superAdminUser));
    assertBadRequest(result, "Hook with this name already exists: test.py");
    assertAuditEntry(1, defaultCustomer.getUuid());
  }

  @Test
  public void testCreateHookWithNonSuperAdmin() {
    Result result =
        assertPlatformException(
            () -> createHook("test.py", Python, "NEW\nTEXT\n", false, defaultUser));
    assertForbiddenWithException(result, "Only Super Admins can perform this operation.");
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testListHooks() {
    createHook("test.py", Python, "DEFAULT\nTEXT\n", true, superAdminUser);
    createHook("test2.sh", Bash, "DEFAULT\nTEXT\n", false, superAdminUser);
    Result result = doRequestWithAuthToken("GET", baseRoute, superAdminUser.createAuthToken());
    JsonNode json = Json.parse(contentAsString(result));
    assertOk(result);
    assertTrue(json.size() == 2);
  }

  @Test
  public void testListHooksWithNonSuperAdmin() {
    createHook("test.py", Python, "DEFAULT\nTEXT\n", true, superAdminUser);
    createHook("test2.sh", Bash, "DEFAULT\nTEXT\n", false, superAdminUser);
    Result result =
        assertPlatformException(
            () -> doRequestWithAuthToken("GET", baseRoute, defaultUser.createAuthToken()));
    assertForbiddenWithException(result, "Only Super Admins can perform this operation.");
  }

  @Test
  public void testDeleteHook() {
    Result createResult = createHook("test.py", Python, "DEFAULT\nTEXT\n", true, superAdminUser);
    JsonNode json = Json.parse(contentAsString(createResult));
    String uuid = json.get("uuid").asText();
    String uri = baseRoute + "/" + uuid;
    Result deleteResult = doRequestWithAuthToken("DELETE", uri, superAdminUser.createAuthToken());
    assertOk(deleteResult);
    assertAuditEntry(2, defaultCustomer.getUuid());
  }

  @Test
  public void testDeleteHookWithNonSuperAdmin() {
    Result createResult = createHook("test.py", Python, "DEFAULT\nTEXT\n", true, superAdminUser);
    JsonNode json = Json.parse(contentAsString(createResult));
    String uuid = json.get("uuid").asText();
    String uri = baseRoute + "/" + uuid;
    Result deleteResult =
        assertPlatformException(
            () -> doRequestWithAuthToken("DELETE", uri, defaultUser.createAuthToken()));
    assertForbiddenWithException(deleteResult, "Only Super Admins can perform this operation.");
    assertAuditEntry(1, defaultCustomer.getUuid());
  }

  @Test
  public void testUpdateHook() {
    Result createResult = createHook("test.py", Python, "DEFAULT\nTEXT\n", true, superAdminUser);
    JsonNode json = Json.parse(contentAsString(createResult));
    String uuid = json.get("uuid").asText();
    String uri = baseRoute + "/" + uuid;

    List<Http.MultipartFormData.Part<Source<ByteString, ?>>> bodyData =
        getCreateHookMultiPartData("test2.sh", Bash, "UPDATED\nTEXT\n", false, alternateArgs);
    Result updateResult =
        doRequestWithAuthTokenAndMultipartData(
            "PUT", uri, superAdminUser.createAuthToken(), bodyData, mat);
    JsonNode updateResultJson = Json.parse(contentAsString(updateResult));

    assertValue(updateResultJson, "name", "test2.sh");
    assertValue(updateResultJson, "hookText", "UPDATED\nTEXT\n");
    assertValue(updateResultJson, "executionLang", "Bash");
    assertValue(updateResultJson, "useSudo", "false");
    assertValue(updateResultJson.get("runtimeArgs"), "KEY2", "QWERTYUIOP");
    assertOk(updateResult);

    // Ensure persistence
    Hook hook = Hook.getOrBadRequest(defaultCustomer.getUuid(), UUID.fromString(uuid));
    assertTrue(hook.getName().equals("test2.sh"));
    assertTrue(hook.getHookText().equals("UPDATED\nTEXT\n"));
    assertTrue(hook.isUseSudo() == false);
    assertTrue(hook.getExecutionLang() == Bash);
    Map<String, String> persistedArgs = hook.getRuntimeArgs();
    assertTrue(persistedArgs.size() == 1);
    assertTrue(persistedArgs.get("KEY2").equals("QWERTYUIOP"));

    // Ensure audit entry has the runtime args redacted and hook text logged
    assertAuditEntry(2, defaultCustomer.getUuid());
    List<Audit> entries = Audit.getAll(defaultCustomer.getUuid());
    JsonNode payload = entries.get(1).getPayload();
    assertValue(payload, "hookText", "UPDATED\nTEXT\n");
    assertValue(payload.get("runtimeArgs"), "KEY2", "QWERTYUIOP");
  }

  @Test
  public void testUpdateHookWithNonUniqueName() {
    createHook("test2.sh", Bash, "DEFAULT\nTEST\n", false, superAdminUser);
    Result createResult = createHook("test.py", Python, "DEFAULT\nTEXT\n", true, superAdminUser);
    JsonNode json = Json.parse(contentAsString(createResult));
    String uuid = json.get("uuid").asText();
    String uri = baseRoute + "/" + uuid;

    List<Http.MultipartFormData.Part<Source<ByteString, ?>>> bodyData =
        getCreateHookMultiPartData("test2.sh", Bash, "UPDATED\nTEXT\n", false, alternateArgs);
    Result updateResult =
        assertPlatformException(
            () ->
                doRequestWithAuthTokenAndMultipartData(
                    "PUT", uri, superAdminUser.createAuthToken(), bodyData, mat));
    assertBadRequest(updateResult, "Hook with this name already exists: test2.sh");
    assertAuditEntry(2, defaultCustomer.getUuid());
  }

  @Test
  public void testUpdateHookWithSameName() {
    Result createResult = createHook("test.py", Python, "DEFAULT\nTEXT\n", true, superAdminUser);
    JsonNode json = Json.parse(contentAsString(createResult));
    String uuid = json.get("uuid").asText();
    String uri = baseRoute + "/" + uuid;

    List<Http.MultipartFormData.Part<Source<ByteString, ?>>> bodyData =
        getCreateHookMultiPartData("test.py", Python, "UPDATED\nTEXT\n", false, alternateArgs);
    Result updateResult =
        doRequestWithAuthTokenAndMultipartData(
            "PUT", uri, superAdminUser.createAuthToken(), bodyData, mat);
    JsonNode updateResultJson = Json.parse(contentAsString(updateResult));

    assertValue(updateResultJson, "name", "test.py");
    assertValue(updateResultJson, "hookText", "UPDATED\nTEXT\n");
    assertValue(updateResultJson, "executionLang", "Python");
    assertValue(updateResultJson, "useSudo", "false");
    assertOk(updateResult);
    assertAuditEntry(2, defaultCustomer.getUuid());
  }

  @Test
  public void testUpdateHookWithNonSuperAdmin() {
    Result createResult = createHook("test.py", Python, "DEFAULT\nTEXT\n", true, superAdminUser);
    JsonNode json = Json.parse(contentAsString(createResult));
    String uuid = json.get("uuid").asText();
    String uri = baseRoute + "/" + uuid;

    List<Http.MultipartFormData.Part<Source<ByteString, ?>>> bodyData =
        getCreateHookMultiPartData("test2.sh", Bash, "UPDATED\nTEXT\n", false, alternateArgs);
    Result updateResult =
        assertPlatformException(
            () ->
                doRequestWithAuthTokenAndMultipartData(
                    "PUT", uri, defaultUser.createAuthToken(), bodyData, mat));
    assertForbiddenWithException(updateResult, "Only Super Admins can perform this operation.");
    assertAuditEntry(1, defaultCustomer.getUuid());
  }

  @Test
  public void testRunApiTriggeredHooks() {
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.RunHooks);
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    Universe universe = ModelFactory.createUniverse();
    String uri =
        "/api/customers/"
            + defaultCustomer.getUuid()
            + "/universes/"
            + universe.getUniverseUUID()
            + "/run_hooks";
    Result result = doRequestWithAuthToken("POST", uri, superAdminUser.createAuthToken());
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertValue(json, "taskUUID", fakeTaskUUID.toString());
    CustomerTask customerTask =
        CustomerTask.find.query().where().eq("task_uuid", fakeTaskUUID).findOne();
    assertNotNull(customerTask);
    assertTrue(customerTask.getCustomerUUID().equals(defaultCustomer.getUuid()));
    assertAuditEntry(1, defaultCustomer.getUuid());
  }

  @Test
  public void runApiTriggeredHooksForCluster() {
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.RunHooks);
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    Universe universe = ModelFactory.createUniverse();
    UUID clusterUUID = universe.getUniverseDetails().clusters.get(0).uuid;
    String uri =
        "/api/customers/"
            + defaultCustomer.getUuid()
            + "/universes/"
            + universe.getUniverseUUID()
            + "/run_hooks"
            + "?clusterUUID="
            + clusterUUID;

    Result result = doRequestWithAuthToken("POST", uri, superAdminUser.createAuthToken());
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertValue(json, "taskUUID", fakeTaskUUID.toString());
    CustomerTask customerTask =
        CustomerTask.find.query().where().eq("task_uuid", fakeTaskUUID).findOne();
    assertNotNull(customerTask);
    assertTrue(customerTask.getCustomerUUID().equals(defaultCustomer.getUuid()));
    assertTrue(customerTask.getTargetType().equals(CustomerTask.TargetType.Cluster));
    assertAuditEntry(1, defaultCustomer.getUuid());
  }

  @Test
  public void testRunApiTriggeredHooksWhenDisabled() {
    when(mockConfig.getBoolean(HookController.ENABLE_API_HOOK_RUN_PATH)).thenReturn(false);
    Universe universe = ModelFactory.createUniverse();
    String uri =
        "/api/customers/"
            + defaultCustomer.getUuid()
            + "/universes/"
            + universe.getUniverseUUID()
            + "/run_hooks";
    Result result =
        assertPlatformException(
            () -> doRequestWithAuthToken("POST", uri, superAdminUser.createAuthToken()));
    assertUnauthorized(
        result,
        "The execution of API Triggered custom hooks is not enabled on this Anywhere instance");
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testOperationWithoutCustomHooksEnabled() {
    when(mockConfig.getBoolean(HookController.ENABLE_CUSTOM_HOOKS_PATH)).thenReturn(false);
    Result result =
        assertPlatformException(
            () -> createHook("test.py", Python, "DEFAULT\nTEXT\n", true, superAdminUser));
    assertUnauthorized(result, "Custom hooks is not enabled on this Anywhere instance");
    assertAuditEntry(0, defaultCustomer.getUuid());
  }
}
