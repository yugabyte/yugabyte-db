package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertAuditEntry;
import static com.yugabyte.yw.common.AssertHelper.assertErrorNodeValue;
import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static org.junit.Assert.assertEquals;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.when;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.config.impl.RuntimeConfig;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.forms.LdapUnivSyncFormData;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;
import play.mvc.Result;

@RunWith(MockitoJUnitRunner.class)
public class LdapUniverseSyncControllerTest extends FakeDBApplication {

  private Customer defaultCustomer;
  private Universe defaultUniverse;
  private Users defaultUser;
  private ObjectMapper mapper;
  private SettableRuntimeConfigFactory runtimeConfigFactory;
  private LdapUnivSyncFormData ldapUnivSyncFormData;
  private String path = "yb.security.ldap.ldap_universe_sync";

  @Before
  public void setup() {
    defaultCustomer = ModelFactory.testCustomer();
    defaultUser = ModelFactory.testUser(defaultCustomer);
    defaultUniverse = ModelFactory.createUniverse(defaultCustomer.getId());
    defaultUniverse = ModelFactory.addNodesToUniverse(defaultUniverse.getUniverseUUID(), 1);
    mapper = new ObjectMapper();
    ldapUnivSyncFormData = new LdapUnivSyncFormData();
    runtimeConfigFactory = app.injector().instanceOf(SettableRuntimeConfigFactory.class);
  }

  /* ==== Helper Request Functions ==== */
  private Result syncLdapWithUniv(UUID universeUUID, JsonNode bodyJson) {
    String authToken = defaultUser.createAuthToken();
    String method = "POST";
    String url =
        "/api/customers/"
            + defaultCustomer.getUuid()
            + "/universes/"
            + universeUUID
            + "/ldap_roles_sync";

    return doRequestWithAuthTokenAndBody(method, url, authToken, bodyJson);
  }

  private void setUniverseRuntimeConfig(String value) {
    RuntimeConfig<Universe> universeConfig = runtimeConfigFactory.forUniverse(defaultUniverse);
    universeConfig.setValue(path, value);
  }

  /* ==== API Tests ==== */
  @Test
  public void testWithRuntimeflagFalse() {
    setUniverseRuntimeConfig("false");
    JsonNode bodyJson = Json.newObject();

    Result result =
        assertPlatformException(
            () -> syncLdapWithUniv(defaultUniverse.getUniverseUUID(), bodyJson));
    assertEquals(BAD_REQUEST, result.status());
    JsonNode resultJson = Json.parse(contentAsString(result));
    assertValue(
        resultJson,
        "error",
        "Please enable the runtime flag: yb.security.ldap.ldap_universe_sync to perform the sync.");
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testLdapUnivSyncWithInvalidUniverseUUID() {
    setUniverseRuntimeConfig("true");
    UUID universeUUID = UUID.randomUUID();
    JsonNode bodyJson = Json.newObject();

    Result result = assertPlatformException(() -> syncLdapWithUniv(universeUUID, bodyJson));
    assertEquals(BAD_REQUEST, result.status());
    JsonNode resultJson = Json.parse(contentAsString(result));
    assertValue(resultJson, "error", "Cannot find universe " + universeUUID);
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testLdapUnivSyncWithInvalidDbUserYsql() {
    setUniverseRuntimeConfig("true");
    ldapUnivSyncFormData.setTargetApi(LdapUnivSyncFormData.TargetApi.ysql);
    ldapUnivSyncFormData.setDbUser("testUser");
    ldapUnivSyncFormData.setLdapUserfield("test");
    ldapUnivSyncFormData.setLdapGroupfield("test");

    JsonNode bodyJson = mapper.valueToTree(ldapUnivSyncFormData);
    Result result =
        assertPlatformException(
            () -> syncLdapWithUniv(defaultUniverse.getUniverseUUID(), bodyJson));
    assertEquals(BAD_REQUEST, result.status());
    JsonNode resultJson = Json.parse(contentAsString(result));
    assertValue(
        resultJson,
        "error",
        String.format("Sync can be performed only by the dbUser(YSQL): yugabyte"));
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testLdapUnivSyncWithInvalidDbUserYcql() {
    setUniverseRuntimeConfig("true");
    ldapUnivSyncFormData.setTargetApi(LdapUnivSyncFormData.TargetApi.ycql);
    ldapUnivSyncFormData.setDbUser("testUser");
    ldapUnivSyncFormData.setLdapUserfield("test");
    ldapUnivSyncFormData.setLdapGroupfield("test");

    JsonNode bodyJson = mapper.valueToTree(ldapUnivSyncFormData);
    Result result =
        assertPlatformException(
            () -> syncLdapWithUniv(defaultUniverse.getUniverseUUID(), bodyJson));
    assertEquals(BAD_REQUEST, result.status());
    JsonNode resultJson = Json.parse(contentAsString(result));
    assertValue(
        resultJson,
        "error",
        String.format("Sync can be performed only by the dbUser(YCQL): cassandra"));
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testLdapUnivSyncWithInvalidYcqlParams() {
    setUniverseRuntimeConfig("true");
    ldapUnivSyncFormData.setTargetApi(LdapUnivSyncFormData.TargetApi.ycql);
    ldapUnivSyncFormData.setDbUser("cassandra");
    ldapUnivSyncFormData.setDbuserPassword("");
    ldapUnivSyncFormData.setLdapUserfield("test");
    ldapUnivSyncFormData.setLdapGroupfield("test");

    JsonNode bodyJson = mapper.valueToTree(ldapUnivSyncFormData);
    Result result =
        assertPlatformException(
            () -> syncLdapWithUniv(defaultUniverse.getUniverseUUID(), bodyJson));
    assertEquals(BAD_REQUEST, result.status());
    JsonNode resultJson = Json.parse(contentAsString(result));
    assertValue(
        resultJson,
        "error",
        String.format(
            "Password is required for the user(YCQL): %s", ldapUnivSyncFormData.getDbUser()));
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testLdapUnivSyncWithoutUserfield() {
    setUniverseRuntimeConfig("true");
    ldapUnivSyncFormData.setTargetApi(LdapUnivSyncFormData.TargetApi.ysql);
    ldapUnivSyncFormData.setDbUser("");
    ldapUnivSyncFormData.setLdapGroupfield("test");

    JsonNode bodyJson = mapper.valueToTree(ldapUnivSyncFormData);
    Result result =
        assertPlatformException(
            () -> syncLdapWithUniv(defaultUniverse.getUniverseUUID(), bodyJson));
    assertEquals(BAD_REQUEST, result.status());
    JsonNode resultJson = Json.parse(contentAsString(result));
    assertErrorNodeValue(resultJson, "ldapUserfield", "error.required");
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testLdapUnivSyncWithoutGroupfield() {
    setUniverseRuntimeConfig("true");
    ldapUnivSyncFormData.setTargetApi(LdapUnivSyncFormData.TargetApi.ysql);
    ldapUnivSyncFormData.setDbUser("");
    ldapUnivSyncFormData.setLdapUserfield("test");

    JsonNode bodyJson = mapper.valueToTree(ldapUnivSyncFormData);
    Result result =
        assertPlatformException(
            () -> syncLdapWithUniv(defaultUniverse.getUniverseUUID(), bodyJson));
    assertEquals(BAD_REQUEST, result.status());
    JsonNode resultJson = Json.parse(contentAsString(result));
    assertErrorNodeValue(resultJson, "ldapGroupfield", "error.required");
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testLdapUnivSync() {
    setUniverseRuntimeConfig("true");
    UUID fakeTaskUUID = UUID.randomUUID();
    ldapUnivSyncFormData.setTargetApi(LdapUnivSyncFormData.TargetApi.ysql);
    ldapUnivSyncFormData.setDbUser("");
    ldapUnivSyncFormData.setLdapUserfield("test");
    ldapUnivSyncFormData.setLdapGroupfield("test");

    when(mockLdapUniverseSyncHandler.syncUniv(any(), any(), any(), any(), any()))
        .thenReturn(fakeTaskUUID);

    JsonNode bodyJson = mapper.valueToTree(ldapUnivSyncFormData);
    Result result = syncLdapWithUniv(defaultUniverse.getUniverseUUID(), bodyJson);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertValue(json, "taskUUID", fakeTaskUUID.toString());

    ldapUnivSyncFormData.setTargetApi(LdapUnivSyncFormData.TargetApi.ycql);
    ldapUnivSyncFormData.setDbuserPassword("test");

    bodyJson = mapper.valueToTree(ldapUnivSyncFormData);
    result = syncLdapWithUniv(defaultUniverse.getUniverseUUID(), bodyJson);
    assertOk(result);
    json = Json.parse(contentAsString(result));
    assertValue(json, "taskUUID", fakeTaskUUID.toString());
  }
}
