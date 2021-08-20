// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertAuditEntry;
import static com.yugabyte.yw.common.AssertHelper.assertBadRequest;
import static com.yugabyte.yw.common.AssertHelper.assertConflict;
import static com.yugabyte.yw.common.AssertHelper.assertErrorNodeValue;
import static com.yugabyte.yw.common.AssertHelper.assertInternalServerError;
import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.AssertHelper.assertYWSE;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.FakeApiHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.forms.PasswordPolicyFormData;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerConfig;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.Users;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;
import play.mvc.Result;

@RunWith(MockitoJUnitRunner.class)
public class CustomerConfigControllerTest extends FakeDBApplication {
  Customer defaultCustomer;
  Users defaultUser;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    defaultUser = ModelFactory.testUser(defaultCustomer);
  }

  @Test
  public void testCreateWithInvalidParams() {
    ObjectNode bodyJson = Json.newObject();
    String url = "/api/customers/" + defaultCustomer.uuid + "/configs";
    Result result =
        assertYWSE(
            () ->
                FakeApiHelper.doRequestWithAuthTokenAndBody(
                    "POST", url, defaultUser.createAuthToken(), bodyJson));

    JsonNode node = Json.parse(contentAsString(result));
    assertErrorNodeValue(node, "data", "This field is required");
    assertErrorNodeValue(node, "name", "This field is required");
    assertErrorNodeValue(node, "type", "This field is required");
    assertErrorNodeValue(node, "configName", "Config name cannot be null");
    assertEquals(BAD_REQUEST, result.status());
    assertAuditEntry(0, defaultCustomer.uuid);
  }

  @Test
  public void testCreateWithInvalidTypeParam() {
    ObjectNode bodyJson = Json.newObject();
    JsonNode data = Json.parse("{\"foo\":\"bar\"}");
    bodyJson.put("name", "test");
    bodyJson.set("data", data);
    bodyJson.put("type", "foo");
    String url = "/api/customers/" + defaultCustomer.uuid + "/configs";
    Result result =
        assertYWSE(
            () ->
                FakeApiHelper.doRequestWithAuthTokenAndBody(
                    "POST", url, defaultUser.createAuthToken(), bodyJson));

    JsonNode node = Json.parse(contentAsString(result));
    assertEquals(BAD_REQUEST, result.status());
    assertErrorNodeValue(node, "type", "Invalid type provided");
    assertAuditEntry(0, defaultCustomer.uuid);
  }

  @Test
  public void testCreateWithInvalidDataParam() {
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("name", "test");
    bodyJson.put("data", "foo");
    bodyJson.put("type", "STORAGE");
    String url = "/api/customers/" + defaultCustomer.uuid + "/configs";
    Result result =
        assertYWSE(
            () ->
                FakeApiHelper.doRequestWithAuthTokenAndBody(
                    "POST", url, defaultUser.createAuthToken(), bodyJson));

    JsonNode node = Json.parse(contentAsString(result));
    assertEquals(BAD_REQUEST, result.status());
    assertErrorNodeValue(node, "data", "Invalid data provided, expected a object.");
    assertAuditEntry(0, defaultCustomer.uuid);
  }

  @Test
  public void testCreateWithInvalidConfigNameParam() {
    ObjectNode bodyJson = Json.newObject();
    JsonNode data = Json.parse("{\"foo\":\"bar\"}");
    bodyJson.put("name", "test");
    bodyJson.put("data", data);
    bodyJson.put("type", "STORAGE");
    bodyJson.put("configName", "   ");
    String url = "/api/customers/" + defaultCustomer.uuid + "/configs";
    Result result =
        assertYWSE(
            () ->
                FakeApiHelper.doRequestWithAuthTokenAndBody(
                    "POST", url, defaultUser.createAuthToken(), bodyJson));

    JsonNode node = Json.parse(contentAsString(result));
    assertEquals(BAD_REQUEST, result.status());
    assertErrorNodeValue(node, "configName", "Invalid config name provided.");
    assertAuditEntry(0, defaultCustomer.uuid);
  }

  @Test
  public void testCreateWithValidParam() {
    ObjectNode bodyJson = Json.newObject();
    JsonNode data = Json.parse("{\"foo\":\"bar\"}");
    bodyJson.put("name", "test");
    bodyJson.set("data", data);
    bodyJson.put("type", "STORAGE");
    bodyJson.put("configName", "fake-config");
    String url = "/api/customers/" + defaultCustomer.uuid + "/configs";
    Result result =
        FakeApiHelper.doRequestWithAuthTokenAndBody(
            "POST", url, defaultUser.createAuthToken(), bodyJson);

    JsonNode node = Json.parse(contentAsString(result));
    assertOk(result);
    assertNotNull(node.get("configUUID"));
    assertEquals(1, CustomerConfig.getAll(defaultCustomer.uuid).size());
    assertAuditEntry(1, defaultCustomer.uuid);
  }

  @Test
  public void testCreateWithSameConfigName() {
    String configName = "TEST123";
    UUID configUUID = ModelFactory.createS3StorageConfig(defaultCustomer, configName).configUUID;
    ObjectNode bodyJson = Json.newObject();
    JsonNode data = Json.parse("{\"foo\":\"bar\"}");
    bodyJson.put("name", "test");
    bodyJson.set("data", data);
    bodyJson.put("type", "STORAGE");
    bodyJson.put("configName", configName);
    String url = "/api/customers/" + defaultCustomer.uuid + "/configs";
    Result result =
        assertYWSE(
            () ->
                FakeApiHelper.doRequestWithAuthTokenAndBody(
                    "POST", url, defaultUser.createAuthToken(), bodyJson));
    assertConflict(result, "Configuration TEST123 already exists");
  }

  @Test
  public void testListCustomeWithData() {
    ModelFactory.createS3StorageConfig(defaultCustomer, "TEST7");
    ModelFactory.createS3StorageConfig(defaultCustomer, "TEST8");
    String url = "/api/customers/" + defaultCustomer.uuid + "/configs";
    Result result = FakeApiHelper.doRequestWithAuthToken("GET", url, defaultUser.createAuthToken());
    JsonNode node = Json.parse(contentAsString(result));
    assertEquals(2, node.size());
    assertAuditEntry(0, defaultCustomer.uuid);
  }

  @Test
  public void testListCustomerWithoutData() {
    String url = "/api/customers/" + defaultCustomer.uuid + "/configs";
    Result result = FakeApiHelper.doRequestWithAuthToken("GET", url, defaultUser.createAuthToken());
    JsonNode node = Json.parse(contentAsString(result));
    assertEquals(0, node.size());
    assertAuditEntry(0, defaultCustomer.uuid);
  }

  @Test
  public void testDeleteValidCustomerConfig() {
    UUID configUUID = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST9").configUUID;

    String url = "/api/customers/" + defaultCustomer.uuid + "/configs/" + configUUID;
    Result result =
        FakeApiHelper.doRequestWithAuthToken("DELETE", url, defaultUser.createAuthToken());
    assertOk(result);
    assertEquals(0, CustomerConfig.getAll(defaultCustomer.uuid).size());
    assertAuditEntry(1, defaultCustomer.uuid);
  }

  @Test
  public void testDeleteInvalidCustomerConfig() {
    Customer customer = ModelFactory.testCustomer("nc", "New Customer");
    UUID configUUID = ModelFactory.createS3StorageConfig(customer, "TEST10").configUUID;
    String url = "/api/customers/" + defaultCustomer.uuid + "/configs/" + configUUID;
    Result result =
        assertYWSE(
            () ->
                FakeApiHelper.doRequestWithAuthToken("DELETE", url, defaultUser.createAuthToken()));
    assertBadRequest(result, "Invalid StorageConfig UUID: " + configUUID);
    assertEquals(1, CustomerConfig.getAll(customer.uuid).size());
    assertAuditEntry(0, defaultCustomer.uuid);
  }

  @Test
  public void testDeleteInUseStorageConfig() {
    UUID configUUID = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST11").configUUID;
    Backup backup = ModelFactory.createBackup(defaultCustomer.uuid, UUID.randomUUID(), configUUID);
    String url = "/api/customers/" + defaultCustomer.uuid + "/configs/" + configUUID;
    Result result =
        assertYWSE(
            () ->
                FakeApiHelper.doRequestWithAuthToken("DELETE", url, defaultUser.createAuthToken()));
    assertInternalServerError(result, "Customer Configuration could not be deleted.");
    backup.delete();
    Schedule schedule =
        ModelFactory.createScheduleBackup(defaultCustomer.uuid, UUID.randomUUID(), configUUID);
    result =
        assertYWSE(
            () ->
                FakeApiHelper.doRequestWithAuthToken("DELETE", url, defaultUser.createAuthToken()));
    assertInternalServerError(result, "Customer Configuration could not be deleted.");
    schedule.delete();
    result = FakeApiHelper.doRequestWithAuthToken("DELETE", url, defaultUser.createAuthToken());
    assertOk(result);
    assertEquals(0, CustomerConfig.getAll(defaultCustomer.uuid).size());
    assertAuditEntry(1, defaultCustomer.uuid);
  }

  @Test
  public void testEditInUseStorageConfig() {
    ObjectNode bodyJson = Json.newObject();
    JsonNode data =
        Json.parse(
            "{\"BACKUP_LOCATION\": \"s3://foo\", \"ACCESS_KEY\": \"A-KEY\", "
                + "\"ACCESS_SECRET\": \"A-SECRET\"}");
    bodyJson.put("name", "test1");
    bodyJson.set("data", data);
    bodyJson.put("type", "STORAGE");
    bodyJson.put("configName", "test-edited");
    UUID configUUID = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST12").configUUID;
    Backup backup = ModelFactory.createBackup(defaultCustomer.uuid, UUID.randomUUID(), configUUID);
    String url = "/api/customers/" + defaultCustomer.uuid + "/configs/" + configUUID;
    Result result =
        FakeApiHelper.doRequestWithAuthTokenAndBody(
            "PUT", url, defaultUser.createAuthToken(), bodyJson);
    assertOk(result);
    backup.delete();
    result =
        FakeApiHelper.doRequestWithAuthTokenAndBody(
            "PUT", url, defaultUser.createAuthToken(), bodyJson);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals("s3://foo", json.get("data").get("BACKUP_LOCATION").textValue());
  }

  @Test
  public void testValidPasswordPolicy() {
    Result result = testPasswordPolicy(8, 1, 1, 1, 1);
    assertOk(result);
    assertEquals(1, CustomerConfig.getAll(defaultCustomer.uuid).size());
    assertAuditEntry(1, defaultCustomer.uuid);
  }

  @Test
  public void testNegativePasswordPolicy() {
    Result result = assertYWSE(() -> testPasswordPolicy(8, -1, 1, 1, 1));
    assertBadRequest(
        result,
        "{\"password policy\":[\"Minimal number of uppercase letters should not be negative\"]}");
    assertEquals(0, CustomerConfig.getAll(defaultCustomer.uuid).size());
    assertAuditEntry(0, defaultCustomer.uuid);
  }

  @Test
  public void testEditInvalidCustomerConfig() {
    ObjectNode bodyJson = Json.newObject();
    JsonNode data = Json.parse("{\"foo\":\"bar\"}");
    bodyJson.put("name", "test1");
    bodyJson.set("data", data);
    bodyJson.put("type", "STORAGE");
    bodyJson.put("configName", "test");
    Customer customer = ModelFactory.testCustomer("nc", "New Customer");
    UUID configUUID = ModelFactory.createS3StorageConfig(customer, "TEST13").configUUID;
    String url = "/api/customers/" + defaultCustomer.uuid + "/configs/" + configUUID;
    Result result =
        assertYWSE(
            () ->
                FakeApiHelper.doRequestWithAuthTokenAndBody(
                    "PUT", url, defaultUser.createAuthToken(), bodyJson));
    assertBadRequest(result, "Invalid StorageConfig UUID: " + configUUID);
    assertEquals(1, CustomerConfig.getAll(customer.uuid).size());
    assertAuditEntry(0, defaultCustomer.uuid);
  }

  @Test
  public void testEditWithBackupLocation() {
    ObjectNode bodyJson = Json.newObject();
    JsonNode data =
        Json.parse(
            "{\"BACKUP_LOCATION\": \"test\", \"ACCESS_KEY\": \"A-KEY-NEW\", "
                + "\"ACCESS_SECRET\": \"DATA\"}");
    bodyJson.put("name", "test1");
    bodyJson.set("data", data);
    bodyJson.put("type", "STORAGE");
    bodyJson.put("configName", "test2");
    UUID configUUID = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST14").configUUID;
    String url = "/api/customers/" + defaultCustomer.uuid + "/configs/" + configUUID;
    Result result =
        assertYWSE(
            () ->
                FakeApiHelper.doRequestWithAuthTokenAndBody(
                    "PUT", url, defaultUser.createAuthToken(), bodyJson));

    assertBadRequest(result, "BACKUP_LOCATION field is read-only.");

    // Should not update the field BACKUP_LOCATION to "test".
    CustomerConfig fromDb = CustomerConfig.get(configUUID);
    assertEquals("s3://foo", fromDb.data.get("BACKUP_LOCATION").textValue());
  }

  @Test
  public void testEditStorageNameOnly_SecretKeysPersist() {
    UUID configUUID = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST15").configUUID;
    CustomerConfig fromDb = CustomerConfig.get(configUUID);

    ObjectNode bodyJson = Json.newObject();
    JsonNode data = fromDb.data;
    bodyJson.put("name", "test1");
    bodyJson.set("data", data);
    bodyJson.put("type", "STORAGE");
    bodyJson.put("configName", fromDb.configName);

    String url = "/api/customers/" + defaultCustomer.uuid + "/configs/" + configUUID;
    Result result =
        FakeApiHelper.doRequestWithAuthTokenAndBody(
            "PUT", url, defaultUser.createAuthToken(), bodyJson);
    assertOk(result);

    CustomerConfig newFromDb = CustomerConfig.get(configUUID);
    assertEquals(
        fromDb.data.get("ACCESS_KEY").textValue(), newFromDb.data.get("ACCESS_KEY").textValue());
    assertEquals(
        fromDb.data.get("ACCESS_SECRET").textValue(),
        newFromDb.data.get("ACCESS_SECRET").textValue());
  }

  @Test
  public void testEditConfigNameToExistentConfigName() {
    String existentConfigName = "TEST152";
    ModelFactory.createS3StorageConfig(defaultCustomer, existentConfigName);
    UUID configUUID = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST153").configUUID;
    CustomerConfig fromDb = CustomerConfig.get(configUUID);

    ObjectNode bodyJson = Json.newObject();
    JsonNode data = fromDb.data;
    bodyJson.put("name", "test1");
    bodyJson.set("data", data);
    bodyJson.put("type", "STORAGE");
    bodyJson.put("configName", existentConfigName);

    String url = "/api/customers/" + defaultCustomer.uuid + "/configs/" + configUUID;
    Result result =
        assertYWSE(
            () ->
                FakeApiHelper.doRequestWithAuthTokenAndBody(
                    "PUT", url, defaultUser.createAuthToken(), bodyJson));
    assertConflict(result, "Configuration TEST152 already exists");
  }

  @Test
  public void testInvalidPasswordPolicy() {
    Result result = assertYWSE(() -> testPasswordPolicy(8, 3, 3, 2, 1));
    assertBadRequest(
        result,
        "{\"password policy\":[\"Minimal length should be not less than"
            + " the sum of minimal counts for upper case, lower case, digits and special characters\"]}");
    assertEquals(0, CustomerConfig.getAll(defaultCustomer.uuid).size());
    assertAuditEntry(0, defaultCustomer.uuid);
  }

  private Result testPasswordPolicy(
      int minLength, int minUpperCase, int minLowerCase, int minDigits, int minSpecialCharacters) {
    PasswordPolicyFormData passwordPolicyFormData = new PasswordPolicyFormData();
    passwordPolicyFormData.setMinLength(minLength);
    passwordPolicyFormData.setMinUppercase(minUpperCase);
    passwordPolicyFormData.setMinLowercase(minLowerCase);
    passwordPolicyFormData.setMinDigits(minDigits);
    passwordPolicyFormData.setMinSpecialCharacters(minSpecialCharacters);

    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("name", "password policy");
    bodyJson.set("data", Json.toJson(passwordPolicyFormData));
    bodyJson.put("type", "PASSWORD_POLICY");
    bodyJson.put("configName", "fake-config");
    String url = "/api/customers/" + defaultCustomer.uuid + "/configs";
    return FakeApiHelper.doRequestWithAuthTokenAndBody(
        "POST", url, defaultUser.createAuthToken(), bodyJson);
  }
}
