// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.api.v2;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import api.v2.handlers.StorageConfigHandler;
import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yba.v2.client.ApiClient;
import com.yugabyte.yba.v2.client.ApiException;
import com.yugabyte.yba.v2.client.Configuration;
import com.yugabyte.yba.v2.client.api.StorageConfigApi;
import com.yugabyte.yba.v2.client.models.AzureCredentialType;
import com.yugabyte.yba.v2.client.models.AzureCredentials;
import com.yugabyte.yba.v2.client.models.AzureManagedIdentityCredentials;
import com.yugabyte.yba.v2.client.models.AzureRegionLocation;
import com.yugabyte.yba.v2.client.models.AzureSasTokenCredentials;
import com.yugabyte.yba.v2.client.models.AzureStorageSpec;
import com.yugabyte.yba.v2.client.models.GcsCredentialType;
import com.yugabyte.yba.v2.client.models.GcsCredentials;
import com.yugabyte.yba.v2.client.models.GcsServiceAccountKeyCredentials;
import com.yugabyte.yba.v2.client.models.GcsStorageSpec;
import com.yugabyte.yba.v2.client.models.NfsStorageSpec;
import com.yugabyte.yba.v2.client.models.OciCredentialType;
import com.yugabyte.yba.v2.client.models.OciCredentials;
import com.yugabyte.yba.v2.client.models.OciIamCredentials;
import com.yugabyte.yba.v2.client.models.OciStorageSpec;
import com.yugabyte.yba.v2.client.models.S3AccessKeyCredentials;
import com.yugabyte.yba.v2.client.models.S3CredentialType;
import com.yugabyte.yba.v2.client.models.S3Credentials;
import com.yugabyte.yba.v2.client.models.S3IamCredentials;
import com.yugabyte.yba.v2.client.models.S3RegionLocation;
import com.yugabyte.yba.v2.client.models.S3StorageSpec;
import com.yugabyte.yba.v2.client.models.StorageConfig;
import com.yugabyte.yba.v2.client.models.StorageConfigSpec;
import com.yugabyte.yba.v2.client.models.StorageConfigState;
import com.yugabyte.yba.v2.client.models.StorageSpec;
import com.yugabyte.yba.v2.client.models.StorageType;
import com.yugabyte.yba.v2.client.models.YBATask;
import com.yugabyte.yw.commissioner.tasks.ValidateStorageConfig;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.customer.config.UnsavedStorageConfigs;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.configs.CustomerConfig.ConfigState;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageNFSData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageS3Data;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.List;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.mockito.ArgumentCaptor;
import play.libs.Json;

public class StorageConfigApiTest extends FakeDBApplication {

  private Customer customer;
  private StorageConfigApi storageConfigApi;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    Users user = ModelFactory.testUser(customer);
    ApiClient v2Client = Configuration.getDefaultApiClient();
    String basePath = String.format("http://localhost:%d/api/v2", port);
    v2Client =
        v2Client.setBasePath(basePath).addDefaultHeader("X-AUTH-TOKEN", user.createAuthToken());
    Configuration.setDefaultApiClient(v2Client);
    storageConfigApi = new StorageConfigApi();
    when(mockCommissioner.submit(eq(TaskType.ValidateStorageConfig), any(ITaskParams.class)))
        // CustomerTask rows carry a foreign key onto task_info, so the task has to really exist.
        .thenAnswer(invocation -> buildTaskInfo(null, TaskType.ValidateStorageConfig));
  }

  /** The params of the ValidateStorageConfig task submitted last. */
  private ValidateStorageConfig.Params submittedParams(int submits) {
    ArgumentCaptor<ITaskParams> params = ArgumentCaptor.forClass(ITaskParams.class);
    verify(mockCommissioner, times(submits))
        .submit(eq(TaskType.ValidateStorageConfig), params.capture());
    return (ValidateStorageConfig.Params) params.getValue();
  }

  private UUID create(StorageConfigSpec spec) throws ApiException {
    return storageConfigApi.createStorageConfig(customer.getUuid(), spec, null).getResourceUuid();
  }

  private static StorageConfigSpec s3Spec(String name) {
    return new StorageConfigSpec()
        .name(name)
        .storage(
            new StorageSpec(
                new S3StorageSpec()
                    .storageType(StorageType.StorageTypeS3)
                    .backupLocation("s3://bucket/yba")
                    .credentials(
                        new S3Credentials(
                            new S3AccessKeyCredentials()
                                .credentialType(S3CredentialType.S3CredentialAccessKey)
                                .accessKeyId("AKIAEXAMPLE")
                                .secretAccessKey("s3cr3t-key")))
                    .regionLocations(
                        List.of(
                            new S3RegionLocation()
                                .region("us-west-2")
                                .location("s3://bucket-west/yba")))));
  }

  private static StorageConfigSpec nfsSpec(String name, String location) {
    return new StorageConfigSpec()
        .name(name)
        .storage(
            new StorageSpec(
                new NfsStorageSpec()
                    .storageType(StorageType.StorageTypeNFS)
                    .backupLocation(location)));
  }

  private static JsonNode error(ApiException e) {
    return Json.parse(e.getResponseBody()).get("error");
  }

  @Test
  public void createAndGetS3() throws ApiException {
    YBATask task =
        storageConfigApi.createStorageConfig(customer.getUuid(), s3Spec("s3-backups"), null);

    UUID uuid = task.getResourceUuid();
    assertNotNull(task.getTaskUuid());
    assertNotNull(uuid);
    ValidateStorageConfig.Params params = submittedParams(1);
    assertEquals(customer.getUuid(), params.customerUUID);
    assertEquals(uuid, params.configUUID);
    assertFalse(params.dryRun);
    CustomerTask customerTask = CustomerTask.findByTaskUUID(task.getTaskUuid());
    assertEquals(CustomerTask.TargetType.CustomerConfiguration, customerTask.getTargetType());
    assertEquals(CustomerTask.TaskType.Create, customerTask.getType());
    assertEquals(uuid, customerTask.getTargetUUID());
    assertEquals("s3-backups", customerTask.getTargetName());

    // Saved before the task checks the storage, and unusable until it has.
    StorageConfig created = storageConfigApi.getStorageConfig(customer.getUuid(), uuid);
    assertEquals(uuid, created.getInfo().getUuid());
    assertEquals(StorageConfigState.StorageConfigValidating, created.getInfo().getState());
    assertFalse(created.getInfo().getIsKubernetesOperatorControlled());
    assertEquals("s3-backups", created.getSpec().getName());
    S3StorageSpec s3 = created.getSpec().getStorage().getS3StorageSpec();
    assertEquals("s3://bucket/yba", s3.getBackupLocation());
    S3AccessKeyCredentials keys = s3.getCredentials().getS3AccessKeyCredentials();
    assertNull("the key ID isn't returned", keys.getAccessKeyId());
    assertNull("the secret isn't returned", keys.getSecretAccessKey());
    assertEquals("us-west-2", s3.getRegionLocations().get(0).getRegion());

    // Stored as v1 storage configs are, so that backups and the v1 API can use it.
    CustomerConfig v1 = CustomerConfig.get(customer.getUuid(), uuid);
    assertEquals(ConfigState.Validating, v1.getState());
    assertEquals(CustomerConfig.ConfigType.STORAGE, v1.getType());
    assertEquals("S3", v1.getName());
    CustomerConfigStorageS3Data data = (CustomerConfigStorageS3Data) v1.getDataObject();
    assertEquals("AKIAEXAMPLE", data.awsAccessKeyId);
    assertEquals("s3cr3t-key", data.awsSecretAccessKey);
    assertEquals("s3://bucket-west/yba", data.regionLocations.get(0).location);

    v1.updateState(ConfigState.Active);
    assertEquals(
        StorageConfigState.StorageConfigActive,
        storageConfigApi.getStorageConfig(customer.getUuid(), uuid).getInfo().getState());

    List<Audit> audits =
        Audit.getAll(customer.getUuid()).stream()
            .filter(a -> a.getTarget() == Audit.TargetType.CustomerConfig)
            .toList();
    assertEquals(1, audits.size());
    assertEquals(Audit.ActionType.Create, audits.get(0).getAction());
    assertEquals(uuid.toString(), audits.get(0).getTargetID());
    assertEquals(task.getTaskUuid(), audits.get(0).getTaskUUID());
    assertFalse(
        "the audit entry has no secret", audits.get(0).getPayload().toString().contains("s3cr3t"));
  }

  @Test
  public void createOtherStorageTypes() throws ApiException {
    UUID gcsUuid =
        create(
            new StorageConfigSpec()
                .name("gcs")
                .storage(
                    new StorageSpec(
                        new GcsStorageSpec()
                            .storageType(StorageType.StorageTypeGCS)
                            .backupLocation("gs://bucket")
                            .credentials(
                                new GcsCredentials(
                                    new GcsServiceAccountKeyCredentials()
                                        .credentialType(
                                            GcsCredentialType.GcsCredentialServiceAccountKey)
                                        .credentialsJson("{\"type\": \"service_account\"}"))))));
    GcsServiceAccountKeyCredentials gcsKey =
        storageConfigApi
            .getStorageConfig(customer.getUuid(), gcsUuid)
            .getSpec()
            .getStorage()
            .getGcsStorageSpec()
            .getCredentials()
            .getGcsServiceAccountKeyCredentials();
    assertNull(gcsKey.getCredentialsJson());

    UUID azureUuid =
        create(
            new StorageConfigSpec()
                .name("azure")
                .storage(
                    new StorageSpec(
                        new AzureStorageSpec()
                            .storageType(StorageType.StorageTypeAzure)
                            .backupLocation("https://account.blob.core.windows.net/backups")
                            .credentials(
                                new AzureCredentials(
                                    new AzureManagedIdentityCredentials()
                                        .credentialType(
                                            AzureCredentialType.AzureCredentialManagedIdentity)
                                        .clientId("client-id"))))));
    assertEquals(
        "client-id",
        storageConfigApi
            .getStorageConfig(customer.getUuid(), azureUuid)
            .getSpec()
            .getStorage()
            .getAzureStorageSpec()
            .getCredentials()
            .getAzureManagedIdentityCredentials()
            .getClientId());

    UUID ociUuid =
        create(
            new StorageConfigSpec()
                .name("oci")
                .storage(
                    new StorageSpec(
                        new OciStorageSpec()
                            .storageType(StorageType.StorageTypeOCI)
                            .backupLocation(
                                "https://objectstorage.us-sanjose-1.oraclecloud.com/n/ns/b/bkt/o")
                            .region("us-sanjose-1")
                            .namespace("ns")
                            .credentials(
                                new OciCredentials(
                                    new OciIamCredentials()
                                        .credentialType(OciCredentialType.OciCredentialIam))))));
    assertEquals(
        "ns",
        storageConfigApi
            .getStorageConfig(customer.getUuid(), ociUuid)
            .getSpec()
            .getStorage()
            .getOciStorageSpec()
            .getNamespace());
    assertEquals("OCI", CustomerConfig.get(customer.getUuid(), ociUuid).getName());
  }

  @Test
  public void createLeavesTheStorageValidatorToTheTask() throws ApiException {
    UUID uuid = create(nfsSpec("nfs", "/mnt/nfs"));
    NfsStorageSpec spec =
        storageConfigApi
            .getStorageConfig(customer.getUuid(), uuid)
            .getSpec()
            .getStorage()
            .getNfsStorageSpec();
    assertEquals("/mnt/nfs", spec.getBackupLocation());
    assertEquals("yugabyte_backup", spec.getBucket());

    // A relative path is found by the storage validator, which runs in the task: the request
    // succeeds.
    UUID bad = create(nfsSpec("nfs-bad", "relative/path"));
    assertEquals(ConfigState.Validating, CustomerConfig.get(customer.getUuid(), bad).getState());
  }

  @Test
  public void nameConflict() throws ApiException {
    UUID existing = create(nfsSpec("dup", "/mnt"));

    ApiException e =
        assertThrows(
            ApiException.class,
            () ->
                storageConfigApi.createStorageConfig(
                    customer.getUuid(), nfsSpec("dup", "/mnt/other"), null));
    assertEquals(409, e.getCode());
    JsonNode error = error(e);
    assertEquals(StorageConfigHandler.NAME_CONFLICT, error.get("code").asText());
    assertEquals(existing.toString(), error.get("storage_config_uuid").asText());
    assertEquals("VALIDATING", error.get("storage_config_state").asText());

    CustomerConfig v1 = CustomerConfig.get(customer.getUuid(), existing);
    v1.updateState(ConfigState.Active);
    e =
        assertThrows(
            ApiException.class,
            () ->
                storageConfigApi.createStorageConfig(
                    customer.getUuid(), nfsSpec("dup", "/mnt/other"), null));
    assertEquals("ACTIVE", error(e).get("storage_config_state").asText());

    // A config queued for deletion keeps its name until the garbage collector removes it.
    v1.setState(ConfigState.QueuedForDeletion);
    v1.update();
    e =
        assertThrows(
            ApiException.class,
            () ->
                storageConfigApi.createStorageConfig(
                    customer.getUuid(), nfsSpec("dup", "/mnt/other"), null));
    assertEquals("QUEUED_FOR_DELETION", error(e).get("storage_config_state").asText());

    // The name is unique among all configs of the customer, not only storage ones.
    CustomerConfig alerts = CustomerConfig.createAlertConfig(customer.getUuid(), Json.newObject());
    alerts.generateUUID();
    alerts.save();
    e =
        assertThrows(
            ApiException.class,
            () ->
                storageConfigApi.createStorageConfig(
                    customer.getUuid(), nfsSpec(alerts.getConfigName(), "/mnt"), null));
    assertEquals(409, e.getCode());
    assertFalse(error(e).has("storage_config_uuid"));
    // Only the first create submitted a task.
    submittedParams(1);
  }

  @Test
  public void idempotentCreate() throws ApiException {
    String key = UUID.randomUUID().toString();

    YBATask first = storageConfigApi.createStorageConfig(customer.getUuid(), s3Spec("s3"), key);
    YBATask retry = storageConfigApi.createStorageConfig(customer.getUuid(), s3Spec("s3"), key);

    assertEquals(first.getTaskUuid(), retry.getTaskUuid());
    assertEquals(first.getResourceUuid(), retry.getResourceUuid());
    submittedParams(1);
    assertEquals(1, savedConfigs());
  }

  /** All of the customer's configs, including those being validated, which getAll leaves out. */
  private int savedConfigs() {
    return CustomerConfig.find.query().where().eq("customer_uuid", customer.getUuid()).findCount();
  }

  @Test
  public void missingSecretsAndBadRegions() {
    StorageConfigSpec noSecret = s3Spec("no-secret");
    noSecret
        .getStorage()
        .getS3StorageSpec()
        .getCredentials()
        .getS3AccessKeyCredentials()
        .secretAccessKey(null);
    assertBadRequest(noSecret, "secret_access_key");

    StorageConfigSpec noKeyId = s3Spec("no-key-id");
    noKeyId
        .getStorage()
        .getS3StorageSpec()
        .getCredentials()
        .getS3AccessKeyCredentials()
        .accessKeyId(null);
    assertBadRequest(noKeyId, "access_key_id");

    StorageConfigSpec duplicateRegion = s3Spec("dup-region");
    S3StorageSpec s3 = duplicateRegion.getStorage().getS3StorageSpec();
    s3.regionLocations(
        List.of(
            new S3RegionLocation().region("r1").location("s3://a/b"),
            new S3RegionLocation().region("r1").location("s3://c/d")));
    assertBadRequest(duplicateRegion, "more than once");

    StorageConfigSpec azureNoRegionToken =
        new StorageConfigSpec()
            .name("azure")
            .storage(
                new StorageSpec(
                    new AzureStorageSpec()
                        .storageType(StorageType.StorageTypeAzure)
                        .backupLocation("https://account.blob.core.windows.net/backups")
                        .credentials(
                            new AzureCredentials(
                                new AzureSasTokenCredentials()
                                    .credentialType(AzureCredentialType.AzureCredentialSasToken)
                                    .sasToken("?sv=token")))
                        .regionLocations(
                            List.of(
                                new AzureRegionLocation()
                                    .region("westus2")
                                    .location("https://west.blob.core.windows.net/backups")))));
    assertBadRequest(azureNoRegionToken, "sas_token of region westus2");

    StorageConfigSpec ociIamNoNamespace =
        new StorageConfigSpec()
            .name("oci")
            .storage(
                new StorageSpec(
                    new OciStorageSpec()
                        .storageType(StorageType.StorageTypeOCI)
                        .backupLocation("s3://bucket")
                        .region("us-sanjose-1")
                        .credentials(
                            new OciCredentials(
                                new OciIamCredentials()
                                    .credentialType(OciCredentialType.OciCredentialIam)))));
    assertBadRequest(ociIamNoNamespace, "namespace");

    StorageConfigSpec noLocation = nfsSpec("no-location", null);
    assertBadRequest(noLocation, "backupLocation");

    assertBadRequest(nfsSpec("   ", "/mnt"), "name must not be blank");

    assertEquals(0, savedConfigs());
    verify(mockCommissioner, never()).submit(any(TaskType.class), any(ITaskParams.class));
  }

  /** Both create and validate answer 400, without submitting a task. */
  private void assertBadRequest(StorageConfigSpec spec, String messagePart) {
    for (boolean dryRun : new boolean[] {false, true}) {
      ApiException e =
          assertThrows(
              ApiException.class,
              () -> {
                if (dryRun) {
                  storageConfigApi.validateStorageConfig(customer.getUuid(), spec, null);
                } else {
                  storageConfigApi.createStorageConfig(customer.getUuid(), spec, null);
                }
              });
      assertEquals(e.getResponseBody(), 400, e.getCode());
      assertTrue(e.getResponseBody(), e.getResponseBody().contains(messagePart));
    }
  }

  @Test
  public void validateSubmitsATaskAndSavesNothing() throws ApiException {
    YBATask task =
        storageConfigApi.validateStorageConfig(
            customer.getUuid(), nfsSpec("nfs", "relative"), null);
    assertNotNull(task.getTaskUuid());
    assertNull(task.getResourceUuid());
    assertEquals(0, savedConfigs());

    ValidateStorageConfig.Params params = submittedParams(1);
    assertTrue(params.dryRun);
    assertEquals(customer.getUuid(), params.customerUUID);
    // The unsaved config goes to the task in memory, not in its params.
    CustomerConfig unsaved =
        app.injector().instanceOf(UnsavedStorageConfigs.class).get(params.configUUID);
    assertEquals("nfs", unsaved.getConfigName());
    assertEquals(
        "relative", ((CustomerConfigStorageNFSData) unsaved.getDataObject()).backupLocation);
    assertFalse(Json.toJson(params).toString().contains("relative"));
    CustomerTask customerTask = CustomerTask.findByTaskUUID(task.getTaskUuid());
    assertEquals(CustomerTask.TaskType.Validate, customerTask.getType());
    assertEquals(CustomerTask.TargetType.CustomerConfiguration, customerTask.getTargetType());
    assertEquals(params.configUUID, customerTask.getTargetUUID());
    // Not audited.
    assertTrue(Audit.getAll(customer.getUuid()).isEmpty());

    create(nfsSpec("nfs", "/mnt/nfs"));
    ApiException conflict =
        assertThrows(
            ApiException.class,
            () ->
                storageConfigApi.validateStorageConfig(
                    customer.getUuid(), nfsSpec("nfs", "/mnt/nfs"), null));
    assertEquals(409, conflict.getCode());
  }

  @Test
  public void getMissing() {
    ApiException e =
        assertThrows(
            ApiException.class,
            () -> storageConfigApi.getStorageConfig(customer.getUuid(), UUID.randomUUID()));
    assertEquals(404, e.getCode());

    // A config of another type isn't a storage config.
    CustomerConfig alerts = CustomerConfig.createAlertConfig(customer.getUuid(), Json.newObject());
    alerts.generateUUID();
    alerts.save();
    e =
        assertThrows(
            ApiException.class,
            () -> storageConfigApi.getStorageConfig(customer.getUuid(), alerts.getConfigUUID()));
    assertEquals(404, e.getCode());
  }

  @Test
  public void getAnotherCustomersConfig() throws ApiException {
    UUID nfs = create(nfsSpec("nfs", "/mnt"));
    Customer other = ModelFactory.testCustomer("other-customer");

    // The caller isn't a user of the other customer.
    ApiException e =
        assertThrows(
            ApiException.class, () -> storageConfigApi.getStorageConfig(other.getUuid(), nfs));
    assertEquals(401, e.getCode());
  }

  @Test
  public void getsAV1Config() throws ApiException {
    // Created through v1, with IAM.
    CustomerConfig v1 =
        CustomerConfig.createStorageConfig(
            customer.getUuid(),
            "S3",
            "v1-s3",
            Json.parse(
                "{\"BACKUP_LOCATION\": \"s3://bucket\", \"IAM_INSTANCE_PROFILE\": \"true\"}"));
    v1.generateUUID();
    v1.save();

    StorageConfig fetched =
        storageConfigApi.getStorageConfig(customer.getUuid(), v1.getConfigUUID());
    assertEquals(StorageConfigState.StorageConfigActive, fetched.getInfo().getState());
    S3StorageSpec s3 = fetched.getSpec().getStorage().getS3StorageSpec();
    S3IamCredentials iam = s3.getCredentials().getS3IamCredentials();
    assertEquals(S3CredentialType.S3CredentialIam, iam.getCredentialType());
    assertTrue(s3.getGlobalBucketAccess());
  }
}
