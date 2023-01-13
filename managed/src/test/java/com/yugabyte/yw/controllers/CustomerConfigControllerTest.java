// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AWSUtil.AWS_ACCESS_KEY_ID_FIELDNAME;
import static com.yugabyte.yw.common.AWSUtil.AWS_SECRET_ACCESS_KEY_FIELDNAME;
import static com.yugabyte.yw.common.AssertHelper.assertAuditEntry;
import static com.yugabyte.yw.common.AssertHelper.assertBadRequest;
import static com.yugabyte.yw.common.AssertHelper.assertConflict;
import static com.yugabyte.yw.common.AssertHelper.assertErrorNodeValue;
import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static com.yugabyte.yw.models.configs.CustomerConfig.ConfigType.CALLHOME;
import static com.yugabyte.yw.models.configs.CustomerConfig.ConfigType.STORAGE;
import static com.yugabyte.yw.models.helpers.CustomerConfigConsts.BACKUP_LOCATION_FIELDNAME;
import static com.yugabyte.yw.models.helpers.CustomerConfigConsts.NAME_S3;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.equalTo;
import static org.hamcrest.Matchers.hasSize;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.when;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.FakeApiHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.common.customer.config.CustomerConfigUI;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.configs.CustomerConfig.ConfigState;
import com.yugabyte.yw.models.configs.data.CustomerConfigPasswordPolicyData;
import com.yugabyte.yw.models.configs.StubbedCustomerConfigValidator;
import com.yugabyte.yw.models.helpers.CommonUtils;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;
import play.mvc.Result;

@RunWith(MockitoJUnitRunner.class)
public class CustomerConfigControllerTest extends FakeDBApplication {
  private Customer defaultCustomer;
  private Users defaultUser;
  private CustomerConfigService customerConfigService;
  private List<String> allowedBuckets = new ArrayList<>();

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    defaultUser = ModelFactory.testUser(defaultCustomer);
    customerConfigService = app.injector().instanceOf(CustomerConfigService.class);
    customerConfigService.setConfigValidator(
        new StubbedCustomerConfigValidator(
            app.injector().instanceOf(BeanValidator.class), allowedBuckets));
  }

  @Test
  public void testCreateWithInvalidParams() {
    ObjectNode bodyJson = Json.newObject();
    String url = "/api/customers/" + defaultCustomer.uuid + "/configs";
    Result result =
        assertPlatformException(
            () ->
                FakeApiHelper.doRequestWithAuthTokenAndBody(
                    "POST", url, defaultUser.createAuthToken(), bodyJson));

    JsonNode node = Json.parse(contentAsString(result));
    assertErrorNodeValue(node, "data", "may not be null");
    assertErrorNodeValue(node, "name", "may not be null");
    assertErrorNodeValue(node, "type", "may not be null");
    assertErrorNodeValue(node, "configName", "may not be null");
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
        assertPlatformException(
            () ->
                FakeApiHelper.doRequestWithAuthTokenAndBody(
                    "POST", url, defaultUser.createAuthToken(), bodyJson));

    assertBadRequest(
        result,
        "Cannot deserialize value of type "
            + "`com.yugabyte.yw.models.configs.CustomerConfig$ConfigType` from String \\\"foo\\\": "
            + "not one of the values accepted for Enum class: "
            + "[STORAGE, CALLHOME, PASSWORD_POLICY, ALERTS]");
    assertAuditEntry(0, defaultCustomer.uuid);
  }

  @Test
  public void testCreateWithInvalidDataParam() {
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("name", "test");
    bodyJson.put("configName", "test1");
    bodyJson.put("data", "foo");
    bodyJson.put("type", STORAGE.toString());
    String url = "/api/customers/" + defaultCustomer.uuid + "/configs";
    Result result =
        assertPlatformException(
            () ->
                FakeApiHelper.doRequestWithAuthTokenAndBody(
                    "POST", url, defaultUser.createAuthToken(), bodyJson));

    assertBadRequest(
        result,
        "Cannot deserialize value of type `com.fasterxml.jackson.databind.node.ObjectNode` "
            + "from String value");
    assertAuditEntry(0, defaultCustomer.uuid);
  }

  @Test
  public void testCreateWithInvalidConfigNameParam() {
    ObjectNode bodyJson = Json.newObject();
    JsonNode data = Json.parse("{\"foo\":\"bar\"}");
    bodyJson.put("name", "test");
    bodyJson.put("data", data);
    bodyJson.put("type", STORAGE.toString());
    bodyJson.put("configName", "   ");
    String url = "/api/customers/" + defaultCustomer.uuid + "/configs";
    Result result =
        assertPlatformException(
            () ->
                FakeApiHelper.doRequestWithAuthTokenAndBody(
                    "POST", url, defaultUser.createAuthToken(), bodyJson));

    JsonNode node = Json.parse(contentAsString(result));
    assertEquals(BAD_REQUEST, result.status());
    assertErrorNodeValue(node, "configName", "size must be between 1 and 100");
    assertAuditEntry(0, defaultCustomer.uuid);
  }

  @Test
  public void testCreateWithValidParam() {
    ObjectNode bodyJson = Json.newObject();
    JsonNode data = Json.parse("{\"callhomeLevel\":\"MEDIUM\"}");
    bodyJson.put("name", "callhome level");
    bodyJson.set("data", data);
    bodyJson.put("type", CALLHOME.toString());
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
    ModelFactory.createS3StorageConfig(defaultCustomer, configName);
    ObjectNode bodyJson = Json.newObject();
    JsonNode data = Json.parse("{\"foo\":\"bar\"}");
    bodyJson.put("name", "test");
    bodyJson.set("data", data);
    bodyJson.put("type", STORAGE.toString());
    bodyJson.put("configName", configName);
    String url = "/api/customers/" + defaultCustomer.uuid + "/configs";
    Result result =
        assertPlatformException(
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
    UUID.randomUUID();
    Result result =
        FakeApiHelper.doRequestWithAuthToken("DELETE", url, defaultUser.createAuthToken());
    assertOk(result);
    assertAuditEntry(1, defaultCustomer.uuid);
  }

  @Test
  public void testDeleteInvalidCustomerConfig() {
    Customer customer = ModelFactory.testCustomer("nc", "New Customer");
    UUID configUUID = ModelFactory.createS3StorageConfig(customer, "TEST10").configUUID;
    String url = "/api/customers/" + defaultCustomer.uuid + "/configs/" + configUUID;
    Result result =
        assertPlatformException(
            () ->
                FakeApiHelper.doRequestWithAuthToken("DELETE", url, defaultUser.createAuthToken()));
    assertBadRequest(result, "Invalid StorageConfig UUID: " + configUUID);
    assertEquals(1, CustomerConfig.getAll(customer.uuid).size());
    assertAuditEntry(0, defaultCustomer.uuid);
  }

  @Test
  public void testDeleteInUseStorageConfigWithDeleteBackups() {
    UUID configUUID = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST11").configUUID;
    Backup backup = ModelFactory.createBackup(defaultCustomer.uuid, UUID.randomUUID(), configUUID);
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    backup.transitionState(Backup.BackupState.Completed);
    String url =
        "/api/customers/"
            + defaultCustomer.uuid
            + "/configs/"
            + configUUID
            + "?isDeleteBackups=true";
    Result result =
        FakeApiHelper.doRequestWithAuthToken("DELETE", url, defaultUser.createAuthToken());
    assertOk(result);
    CustomerTask customerTask = CustomerTask.findByTaskUUID(fakeTaskUUID);
    assertEquals(customerTask.getTargetUUID(), configUUID);
    fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    ModelFactory.createScheduleBackup(defaultCustomer.uuid, UUID.randomUUID(), configUUID);
    result = FakeApiHelper.doRequestWithAuthToken("DELETE", url, defaultUser.createAuthToken());
    assertOk(result);
    customerTask = CustomerTask.findByTaskUUID(fakeTaskUUID);
    assertEquals(customerTask.getTargetUUID(), configUUID);
    assertAuditEntry(2, defaultCustomer.uuid);
  }

  @Test
  public void testDeleteInUseStorageConfigWithoutDeleteBackups() {
    UUID configUUID = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST11").configUUID;
    Backup backup = ModelFactory.createBackup(defaultCustomer.uuid, UUID.randomUUID(), configUUID);
    backup.transitionState(Backup.BackupState.Completed);
    String url = "/api/customers/" + defaultCustomer.uuid + "/configs/" + configUUID;
    Result result =
        assertPlatformException(
            () ->
                FakeApiHelper.doRequestWithAuthToken("DELETE", url, defaultUser.createAuthToken()));
    CustomerConfig config = CustomerConfig.get(configUUID);
    assertBadRequest(
        result,
        "Configuration " + config.getConfigName() + " is used in backup and can't be deleted");
    assertEquals(1, CustomerConfig.getAll(defaultCustomer.uuid).size());
    assertAuditEntry(0, defaultCustomer.uuid);
  }

  @Test
  public void testDeleteInUsStorageConfigWithInProgressBackup() {
    UUID configUUID = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST12").configUUID;
    ModelFactory.createBackup(defaultCustomer.uuid, UUID.randomUUID(), configUUID);
    UUID.randomUUID();
    String url = "/api/customers/" + defaultCustomer.uuid + "/configs/" + configUUID;
    Result result =
        assertPlatformException(
            () ->
                FakeApiHelper.doRequestWithAuthToken("DELETE", url, defaultUser.createAuthToken()));
    assertBadRequest(
        result,
        "Backup task associated with Configuration " + configUUID.toString() + " is in progress.");
    assertEquals(1, CustomerConfig.getAll(defaultCustomer.uuid).size());
    assertAuditEntry(0, defaultCustomer.uuid);
  }

  @Test
  public void testEditInUseStorageConfig() {
    ObjectNode bodyJson = Json.newObject();
    JsonNode data =
        Json.parse(
            "{\"BACKUP_LOCATION\": \"s3://foo\", \"AWS_ACCESS_KEY_ID\": \"A-KEY\", "
                + "\"AWS_SECRET_ACCESS_KEY\": \"A-SECRET\"}");
    bodyJson.put("name", NAME_S3);
    bodyJson.set("data", data);
    bodyJson.put("type", STORAGE.toString());
    bodyJson.put("configName", "test-edited");
    allowedBuckets.add("foo");
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
    JsonNode responseData = json.get("data");
    assertEquals("s3://foo", responseData.get(BACKUP_LOCATION_FIELDNAME).textValue());

    // sensitive fields should have been masked in edit storage config flow
    assertEquals(
        CommonUtils.getMaskedValue("AWS_ACCESS_KEY_ID", "A-KEY"),
        responseData.get("AWS_ACCESS_KEY_ID").textValue());
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
    Result result = assertPlatformException(() -> testPasswordPolicy(8, -1, 1, 1, 1));
    assertBadRequest(result, "{\"data.minUppercase\":[\"must be greater than or equal to 0\"]}");
    assertEquals(0, CustomerConfig.getAll(defaultCustomer.uuid).size());
    assertAuditEntry(0, defaultCustomer.uuid);
  }

  @Test
  public void testEditInvalidCustomerConfig() {
    ObjectNode bodyJson = Json.newObject();
    JsonNode data = Json.parse("{\"foo\":\"bar\"}");
    bodyJson.put("name", "test1");
    bodyJson.set("data", data);
    bodyJson.put("type", STORAGE.toString());
    bodyJson.put("configName", "test");
    Customer customer = ModelFactory.testCustomer("nc", "New Customer");
    UUID configUUID = ModelFactory.createS3StorageConfig(customer, "TEST13").configUUID;
    String url = "/api/customers/" + defaultCustomer.uuid + "/configs/" + configUUID;
    Result result =
        assertPlatformException(
            () ->
                FakeApiHelper.doRequestWithAuthTokenAndBody(
                    "PUT", url, defaultUser.createAuthToken(), bodyJson));
    assertBadRequest(result, "Invalid StorageConfig UUID: " + configUUID);
    assertEquals(1, CustomerConfig.getAll(customer.uuid).size());
    assertAuditEntry(0, defaultCustomer.uuid);
  }

  @Test
  public void testEditWithBackupLocation() {
    CustomerConfig config = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST14");
    JsonNode newData =
        Json.parse(
            "{\"BACKUP_LOCATION\": \"test\", \"ACCESS_KEY\": \"A-KEY-NEW\", "
                + "\"ACCESS_SECRET\": \"DATA\"}");
    config.setData((ObjectNode) newData);
    JsonNode bodyJson = Json.toJson(config);
    String url = "/api/customers/" + defaultCustomer.uuid + "/configs/" + config.getConfigUUID();
    Result result =
        assertPlatformException(
            () ->
                FakeApiHelper.doRequestWithAuthTokenAndBody(
                    "PUT", url, defaultUser.createAuthToken(), bodyJson));

    assertBadRequest(result, "{\"data.BACKUP_LOCATION\":[\"Field is read-only.\"]}");

    // Should not update the field BACKUP_LOCATION to "test".
    CustomerConfig fromDb = CustomerConfig.get(config.getConfigUUID());
    assertEquals("s3://foo", fromDb.data.get(BACKUP_LOCATION_FIELDNAME).textValue());
  }

  @Test
  public void testSecretKeyMasked() {
    ModelFactory.createS3StorageConfig(defaultCustomer, "TEST15");

    String url = "/api/customers/" + defaultCustomer.uuid + "/configs";

    Result result = FakeApiHelper.doRequestWithAuthToken("GET", url, defaultUser.createAuthToken());

    JsonNode node = Json.parse(contentAsString(result));
    List<CustomerConfigUI> customerConfigUIList =
        Arrays.asList(Json.fromJson(node, CustomerConfigUI[].class));

    assertThat(customerConfigUIList, hasSize(1));
    CustomerConfigUI configUI = customerConfigUIList.get(0);
    ObjectNode maskedData = CommonUtils.maskConfig(configUI.getCustomerConfig().getData());
    assertThat(configUI.getCustomerConfig().getData(), equalTo(maskedData));
  }

  @Test
  public void testEditStorageNameOnly_SecretKeysPersist() {
    UUID configUUID = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST15").configUUID;
    CustomerConfig fromDb = CustomerConfig.get(configUUID);
    allowedBuckets.add("foo");

    ObjectNode bodyJson = Json.newObject();
    JsonNode data = fromDb.getMaskedData();
    bodyJson.put("name", NAME_S3);
    bodyJson.set("data", data);
    bodyJson.put("type", STORAGE.toString());
    bodyJson.put("configName", fromDb.configName);

    String url = "/api/customers/" + defaultCustomer.uuid + "/configs/" + configUUID;
    Result result =
        FakeApiHelper.doRequestWithAuthTokenAndBody(
            "PUT", url, defaultUser.createAuthToken(), bodyJson);
    assertOk(result);

    CustomerConfig newFromDb = CustomerConfig.get(configUUID);
    assertEquals(
        fromDb.data.get(AWS_ACCESS_KEY_ID_FIELDNAME).textValue(),
        newFromDb.data.get(AWS_ACCESS_KEY_ID_FIELDNAME).textValue());
    assertEquals(
        fromDb.data.get(AWS_SECRET_ACCESS_KEY_FIELDNAME).textValue(),
        newFromDb.data.get(AWS_SECRET_ACCESS_KEY_FIELDNAME).textValue());
  }

  @Test
  public void testEditConfigNameToExistentConfigName() {
    String existentConfigName = "TEST152";
    ModelFactory.createS3StorageConfig(defaultCustomer, existentConfigName);
    UUID configUUID = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST153").configUUID;
    CustomerConfig fromDb = CustomerConfig.get(configUUID);

    ObjectNode bodyJson = Json.newObject();
    JsonNode data = fromDb.data;
    bodyJson.put("name", NAME_S3);
    bodyJson.set("data", data);
    bodyJson.put("type", STORAGE.toString());
    bodyJson.put("configName", existentConfigName);

    String url = "/api/customers/" + defaultCustomer.uuid + "/configs/" + configUUID;
    Result result =
        assertPlatformException(
            () ->
                FakeApiHelper.doRequestWithAuthTokenAndBody(
                    "PUT", url, defaultUser.createAuthToken(), bodyJson));
    assertConflict(result, "Configuration TEST152 already exists");
  }

  @Test
  public void testInvalidPasswordPolicy() {
    Result result = assertPlatformException(() -> testPasswordPolicy(8, 3, 3, 2, 1));
    assertBadRequest(
        result,
        "{\"data\":[\"Minimal length should be not less than"
            + " the sum of minimal counts for upper case, lower case, digits and special characters\"]}");
    assertEquals(0, CustomerConfig.getAll(defaultCustomer.uuid).size());
    assertAuditEntry(0, defaultCustomer.uuid);
  }

  private Result testPasswordPolicy(
      int minLength, int minUpperCase, int minLowerCase, int minDigits, int minSpecialCharacters) {
    CustomerConfigPasswordPolicyData passwordPolicyData = new CustomerConfigPasswordPolicyData();
    passwordPolicyData.setMinLength(minLength);
    passwordPolicyData.setMinUppercase(minUpperCase);
    passwordPolicyData.setMinLowercase(minLowerCase);
    passwordPolicyData.setMinDigits(minDigits);
    passwordPolicyData.setMinSpecialCharacters(minSpecialCharacters);

    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("name", "password policy");
    bodyJson.set("data", Json.toJson(passwordPolicyData));
    bodyJson.put("type", "PASSWORD_POLICY");
    bodyJson.put("configName", "fake-config");
    String url = "/api/customers/" + defaultCustomer.uuid + "/configs";
    return FakeApiHelper.doRequestWithAuthTokenAndBody(
        "POST", url, defaultUser.createAuthToken(), bodyJson);
  }

  @Test
  public void testDeleteInUsStorageConfigYbWithInProgressBackup() {
    UUID configUUID = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST17").configUUID;
    ModelFactory.createBackup(defaultCustomer.uuid, UUID.randomUUID(), configUUID);
    String url = "/api/customers/" + defaultCustomer.uuid + "/configs/" + configUUID + "/delete";
    Result result =
        assertPlatformException(
            () ->
                FakeApiHelper.doRequestWithAuthToken("DELETE", url, defaultUser.createAuthToken()));
    assertBadRequest(
        result,
        "Backup task associated with Configuration " + configUUID.toString() + " is in progress.");
    assertEquals(1, CustomerConfig.getAll(defaultCustomer.uuid).size());
    assertAuditEntry(0, defaultCustomer.uuid);
  }

  @Test
  public void testDeleteInUseStorageConfigYbWithoutDeleteBackups() {
    UUID configUUID = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST18").configUUID;
    Backup backup = ModelFactory.createBackup(defaultCustomer.uuid, UUID.randomUUID(), configUUID);
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    backup.transitionState(Backup.BackupState.Completed);
    String url = "/api/customers/" + defaultCustomer.uuid + "/configs/" + configUUID + "/delete";
    Result result =
        FakeApiHelper.doRequestWithAuthToken("DELETE", url, defaultUser.createAuthToken());
    assertOk(result);
    CustomerTask customerTask = CustomerTask.findByTaskUUID(fakeTaskUUID);
    assertEquals(customerTask.getTargetUUID(), configUUID);
  }

  @Test
  public void testDeleteStorageConfigYbWithoutDeleteBackups() {
    UUID configUUID = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST18").configUUID;
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    String url = "/api/customers/" + defaultCustomer.uuid + "/configs/" + configUUID + "/delete";
    Result result =
        FakeApiHelper.doRequestWithAuthToken("DELETE", url, defaultUser.createAuthToken());
    assertOk(result);
    CustomerTask customerTask = CustomerTask.findByTaskUUID(fakeTaskUUID);
    assertEquals(customerTask.getTargetUUID(), configUUID);
  }

  @Test
  public void testDeleteInUseStorageConfigYbWithDeleteBackups() {
    UUID configUUID = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST19").configUUID;
    Backup backup = ModelFactory.createBackup(defaultCustomer.uuid, UUID.randomUUID(), configUUID);
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    backup.transitionState(Backup.BackupState.Completed);
    String url =
        "/api/customers/"
            + defaultCustomer.uuid
            + "/configs/"
            + configUUID
            + "/delete?isDeleteBackups=true";
    Result result =
        FakeApiHelper.doRequestWithAuthToken("DELETE", url, defaultUser.createAuthToken());
    assertOk(result);
    CustomerTask customerTask = CustomerTask.findByTaskUUID(fakeTaskUUID);
    assertEquals(customerTask.getTargetUUID(), configUUID);
  }

  @Test
  public void testDeleteInvalidCustomerConfigYb() {
    Customer customer = ModelFactory.testCustomer("nc", "New Customer");
    UUID configUUID = ModelFactory.createS3StorageConfig(customer, "TEST20").configUUID;
    String url = "/api/customers/" + defaultCustomer.uuid + "/configs/" + configUUID + "/delete";
    Result result =
        assertPlatformException(
            () ->
                FakeApiHelper.doRequestWithAuthToken("DELETE", url, defaultUser.createAuthToken()));
    assertBadRequest(result, "Invalid StorageConfig UUID: " + configUUID);
    assertEquals(1, CustomerConfig.getAll(customer.uuid).size());
    assertAuditEntry(0, defaultCustomer.uuid);
  }

  @Test
  public void testDeleteValidCustomerConfigYb() {
    UUID configUUID = CustomerConfig.createCallHomeConfig(defaultCustomer.uuid).configUUID;
    String url = "/api/customers/" + defaultCustomer.uuid + "/configs/" + configUUID + "/delete";
    Result result =
        FakeApiHelper.doRequestWithAuthToken("DELETE", url, defaultUser.createAuthToken());
    assertOk(result);
    assertAuditEntry(1, defaultCustomer.uuid);
  }

  @Test
  public void testEditInUseStorageConfigYb() {
    ObjectNode bodyJson = Json.newObject();
    JsonNode data =
        Json.parse(
            "{\"BACKUP_LOCATION\": \"s3://foo\", \"AWS_ACCESS_KEY_ID\": \"A-KEY\", "
                + "\"AWS_SECRET_ACCESS_KEY\": \"A-SECRET\"}");
    bodyJson.put("name", NAME_S3);
    bodyJson.set("data", data);
    bodyJson.put("type", STORAGE.toString());
    bodyJson.put("configName", "test-edited");
    allowedBuckets.add("foo");
    UUID configUUID = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST12").configUUID;
    Backup backup = ModelFactory.createBackup(defaultCustomer.uuid, UUID.randomUUID(), configUUID);
    String url = "/api/customers/" + defaultCustomer.uuid + "/configs/" + configUUID + "/edit";
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
    assertEquals("s3://foo", json.get("data").get(BACKUP_LOCATION_FIELDNAME).textValue());
  }

  @Test
  public void testEditInvalidCustomerConfigYb() {
    ObjectNode bodyJson = Json.newObject();
    JsonNode data = Json.parse("{\"foo\":\"bar\"}");
    bodyJson.put("name", "test1");
    bodyJson.set("data", data);
    bodyJson.put("type", STORAGE.toString());
    bodyJson.put("configName", "test");
    Customer customer = ModelFactory.testCustomer("nc", "New Customer");
    UUID configUUID = ModelFactory.createS3StorageConfig(customer, "TEST13").configUUID;
    String url = "/api/customers/" + defaultCustomer.uuid + "/configs/" + configUUID + "/edit";
    Result result =
        assertPlatformException(
            () ->
                FakeApiHelper.doRequestWithAuthTokenAndBody(
                    "PUT", url, defaultUser.createAuthToken(), bodyJson));
    assertBadRequest(result, "Invalid StorageConfig UUID: " + configUUID);
    assertEquals(1, CustomerConfig.getAll(customer.uuid).size());
    assertAuditEntry(0, defaultCustomer.uuid);
  }

  @Test
  public void testEditWithBackupLocationYb() {
    CustomerConfig config = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST14");
    JsonNode newData =
        Json.parse(
            "{\"BACKUP_LOCATION\": \"test\", \"ACCESS_KEY\": \"A-KEY-NEW\", "
                + "\"ACCESS_SECRET\": \"DATA\"}");
    config.setData((ObjectNode) newData);
    JsonNode bodyJson = Json.toJson(config);
    String url =
        "/api/customers/" + defaultCustomer.uuid + "/configs/" + config.getConfigUUID() + "/edit";
    Result result =
        assertPlatformException(
            () ->
                FakeApiHelper.doRequestWithAuthTokenAndBody(
                    "PUT", url, defaultUser.createAuthToken(), bodyJson));

    assertBadRequest(result, "{\"data.BACKUP_LOCATION\":[\"Field is read-only.\"]}");

    // Should not update the field BACKUP_LOCATION to "test".
    CustomerConfig fromDb = CustomerConfig.get(config.getConfigUUID());
    assertEquals("s3://foo", fromDb.data.get(BACKUP_LOCATION_FIELDNAME).textValue());
  }

  @Test
  public void testEditStorageNameOnly_SecretKeysPersistYb() {
    UUID configUUID = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST15").configUUID;
    CustomerConfig fromDb = CustomerConfig.get(configUUID);
    allowedBuckets.add("foo");

    ObjectNode bodyJson = Json.newObject();
    JsonNode data = fromDb.getMaskedData();
    bodyJson.put("name", NAME_S3);
    bodyJson.set("data", data);
    bodyJson.put("type", STORAGE.toString());
    bodyJson.put("configName", fromDb.configName);

    String url = "/api/customers/" + defaultCustomer.uuid + "/configs/" + configUUID + "/edit";
    Result result =
        FakeApiHelper.doRequestWithAuthTokenAndBody(
            "PUT", url, defaultUser.createAuthToken(), bodyJson);
    assertOk(result);

    CustomerConfig newFromDb = CustomerConfig.get(configUUID);
    assertEquals(
        fromDb.data.get("AWS_ACCESS_KEY_ID").textValue(),
        newFromDb.data.get("AWS_ACCESS_KEY_ID").textValue());
    assertEquals(
        fromDb.data.get("AWS_SECRET_ACCESS_KEY").textValue(),
        newFromDb.data.get("AWS_SECRET_ACCESS_KEY").textValue());

    String sensitiveKey = "AWS_ACCESS_KEY_ID";
    String maskedValue =
        CommonUtils.getMaskedValue(sensitiveKey, newFromDb.getData().get(sensitiveKey).textValue());
    JsonNode resultNode = Json.parse(contentAsString(result));
    assertEquals(maskedValue, resultNode.get("data").get(sensitiveKey).textValue());
  }

  @Test
  public void testEditConfigNameToExistentConfigNameYb() {
    String existentConfigName = "TEST152";
    ModelFactory.createS3StorageConfig(defaultCustomer, existentConfigName);
    UUID configUUID = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST153").configUUID;
    CustomerConfig fromDb = CustomerConfig.get(configUUID);

    ObjectNode bodyJson = Json.newObject();
    JsonNode data = fromDb.data;
    bodyJson.put("name", NAME_S3);
    bodyJson.set("data", data);
    bodyJson.put("type", STORAGE.toString());
    bodyJson.put("configName", existentConfigName);

    String url = "/api/customers/" + defaultCustomer.uuid + "/configs/" + configUUID + "/edit";
    Result result =
        assertPlatformException(
            () ->
                FakeApiHelper.doRequestWithAuthTokenAndBody(
                    "PUT", url, defaultUser.createAuthToken(), bodyJson));
    assertConflict(result, "Configuration TEST152 already exists");
  }

  @Test
  public void testEditConfigQueuedForDeletion() {
    UUID configUUID = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST15").configUUID;
    CustomerConfig config = customerConfigService.getOrBadRequest(defaultCustomer.uuid, configUUID);
    config.setState(ConfigState.QueuedForDeletion);
    config.refresh();

    CustomerConfig fromDb = CustomerConfig.get(configUUID);
    ObjectNode bodyJson = Json.newObject();
    JsonNode data = fromDb.getMaskedData();
    bodyJson.put("name", "test1");
    bodyJson.set("data", data);
    bodyJson.put("type", STORAGE.toString());
    bodyJson.put("configName", fromDb.configName);

    String url = "/api/customers/" + defaultCustomer.uuid + "/configs/" + configUUID + "/edit";
    Result result =
        assertPlatformException(
            () ->
                FakeApiHelper.doRequestWithAuthTokenAndBody(
                    "PUT", url, defaultUser.createAuthToken(), bodyJson));
    assertBadRequest(result, "Cannot edit config as it is queued for deletion.");
  }
}
