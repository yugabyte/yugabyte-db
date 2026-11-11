// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.api.v2;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.doAnswer;
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import api.v2.handlers.StorageConfigHandler;
import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.util.concurrent.Uninterruptibles;
import com.typesafe.config.ConfigFactory;
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
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformExecutorFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ShutdownHookHandler;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.RuntimeConfigEntry;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.configs.CustomerConfig.ConfigState;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageS3Data;
import com.yugabyte.yw.models.helpers.CustomerConfigValidator;
import java.net.ConnectException;
import java.net.SocketTimeoutException;
import java.net.UnknownHostException;
import java.time.Duration;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import org.apache.pekko.actor.CoordinatedShutdown;
import org.junit.Before;
import org.junit.Test;
import play.libs.Json;
import software.amazon.awssdk.awscore.exception.AwsErrorDetails;
import software.amazon.awssdk.core.exception.SdkClientException;
import software.amazon.awssdk.services.s3.model.S3Exception;

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
  }

  /** The cloud storage validators would reach out to the cloud; NFS has no remote check. */
  private static void skipStorageValidation() {
    RuntimeConfigEntry.upsertGlobal(GlobalConfKeys.skipStorageConfigValidation.getKey(), "true");
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
    skipStorageValidation();

    StorageConfig created =
        storageConfigApi.createStorageConfig(customer.getUuid(), s3Spec("s3-backups"), null);

    UUID uuid = created.getInfo().getUuid();
    assertEquals(StorageConfigState.StorageConfigActive, created.getInfo().getState());
    assertFalse(created.getInfo().getKubernetesOperatorControlled());
    assertEquals("s3-backups", created.getSpec().getName());
    S3StorageSpec s3 = created.getSpec().getStorage().getS3StorageSpec();
    assertEquals("s3://bucket/yba", s3.getBackupLocation());
    S3AccessKeyCredentials keys = s3.getCredentials().getS3AccessKeyCredentials();
    assertEquals("AKIAEXAMPLE", keys.getAccessKeyId());
    assertNull("the secret isn't returned", keys.getSecretAccessKey());
    assertEquals("us-west-2", s3.getRegionLocations().get(0).getRegion());

    StorageConfig fetched = storageConfigApi.getStorageConfig(customer.getUuid(), uuid);
    assertEquals(created, fetched);

    // Stored as v1 storage configs are, so that backups and the v1 API can use it.
    CustomerConfig v1 = CustomerConfig.get(customer.getUuid(), uuid);
    assertEquals(CustomerConfig.ConfigType.STORAGE, v1.getType());
    assertEquals("S3", v1.getName());
    CustomerConfigStorageS3Data data = (CustomerConfigStorageS3Data) v1.getDataObject();
    assertEquals("AKIAEXAMPLE", data.awsAccessKeyId);
    assertEquals("s3cr3t-key", data.awsSecretAccessKey);
    assertEquals("s3://bucket-west/yba", data.regionLocations.get(0).location);

    List<Audit> audits =
        Audit.getAll(customer.getUuid()).stream()
            .filter(a -> a.getTarget() == Audit.TargetType.CustomerConfig)
            .toList();
    assertEquals(1, audits.size());
    assertEquals(Audit.ActionType.Create, audits.get(0).getAction());
    assertEquals(uuid.toString(), audits.get(0).getTargetID());
    assertFalse(
        "the audit entry has no secret", audits.get(0).getPayload().toString().contains("s3cr3t"));
  }

  @Test
  public void createOtherStorageTypes() throws ApiException {
    skipStorageValidation();

    StorageConfig gcs =
        storageConfigApi.createStorageConfig(
            customer.getUuid(),
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
                                        .credentialsJson("{\"type\": \"service_account\"}"))))),
            null);
    GcsServiceAccountKeyCredentials gcsKey =
        gcs.getSpec()
            .getStorage()
            .getGcsStorageSpec()
            .getCredentials()
            .getGcsServiceAccountKeyCredentials();
    assertNull(gcsKey.getCredentialsJson());

    StorageConfig azure =
        storageConfigApi.createStorageConfig(
            customer.getUuid(),
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
                                        .clientId("client-id"))))),
            null);
    assertEquals(
        "client-id",
        azure
            .getSpec()
            .getStorage()
            .getAzureStorageSpec()
            .getCredentials()
            .getAzureManagedIdentityCredentials()
            .getClientId());

    StorageConfig oci =
        storageConfigApi.createStorageConfig(
            customer.getUuid(),
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
                                        .credentialType(OciCredentialType.OciCredentialIam))))),
            null);
    assertEquals("ns", oci.getSpec().getStorage().getOciStorageSpec().getNamespace());
    assertEquals("OCI", CustomerConfig.get(customer.getUuid(), oci.getInfo().getUuid()).getName());
  }

  @Test
  public void createNfsRunsTheStorageValidator() throws ApiException {
    StorageConfig nfs =
        storageConfigApi.createStorageConfig(customer.getUuid(), nfsSpec("nfs", "/mnt/nfs"), null);
    NfsStorageSpec spec = nfs.getSpec().getStorage().getNfsStorageSpec();
    assertEquals("/mnt/nfs", spec.getBackupLocation());
    assertEquals("yugabyte_backup", spec.getBucket());

    ApiException e =
        assertThrows(
            ApiException.class,
            () ->
                storageConfigApi.createStorageConfig(
                    customer.getUuid(), nfsSpec("nfs-bad", "relative/path"), null));
    // Found by the storage validator without reaching the storage: an invalid request.
    assertEquals(400, e.getCode());
    JsonNode error = error(e);
    assertEquals(StorageConfigHandler.CONFIG_INVALID, error.get("code").asText());
    assertTrue(
        error.toString(),
        error.get("errors").get("storage.backup_location").get(0).asText().contains("Invalid"));
    assertNull(CustomerConfig.get(customer.getUuid(), "nfs-bad"));
  }

  @Test
  public void nameConflict() throws ApiException {
    StorageConfig existing =
        storageConfigApi.createStorageConfig(customer.getUuid(), nfsSpec("dup", "/mnt"), null);

    ApiException e =
        assertThrows(
            ApiException.class,
            () ->
                storageConfigApi.createStorageConfig(
                    customer.getUuid(), nfsSpec("dup", "/mnt/other"), null));
    assertEquals(409, e.getCode());
    JsonNode error = error(e);
    assertEquals(StorageConfigHandler.NAME_CONFLICT, error.get("code").asText());
    assertEquals(
        existing.getInfo().getUuid().toString(), error.get("storage_config_uuid").asText());
    assertEquals("ACTIVE", error.get("storage_config_state").asText());

    // A config queued for deletion keeps its name until the garbage collector removes it.
    CustomerConfig v1 = CustomerConfig.get(customer.getUuid(), existing.getInfo().getUuid());
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
  }

  @Test
  public void idempotentCreate() throws ApiException {
    skipStorageValidation();
    String key = UUID.randomUUID().toString();

    StorageConfig first =
        storageConfigApi.createStorageConfig(customer.getUuid(), s3Spec("s3"), key);
    StorageConfig retry =
        storageConfigApi.createStorageConfig(customer.getUuid(), s3Spec("s3"), key);

    assertEquals(first.getInfo().getUuid(), retry.getInfo().getUuid());
    assertEquals(
        1,
        CustomerConfig.getAll(customer.getUuid()).stream()
            .filter(c -> c.getConfigName().equals("s3"))
            .count());
  }

  @Test
  public void missingSecretsAndBadRegions() {
    skipStorageValidation();

    StorageConfigSpec noSecret = s3Spec("no-secret");
    noSecret
        .getStorage()
        .getS3StorageSpec()
        .getCredentials()
        .getS3AccessKeyCredentials()
        .secretAccessKey(null);
    assertBadRequest(noSecret, "secret_access_key");

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

    assertTrue(CustomerConfig.getAll(customer.getUuid()).isEmpty());
  }

  private void assertBadRequest(StorageConfigSpec spec, String messagePart) {
    ApiException e =
        assertThrows(
            ApiException.class,
            () -> storageConfigApi.createStorageConfig(customer.getUuid(), spec, null));
    assertEquals(e.getResponseBody(), 400, e.getCode());
    assertTrue(e.getResponseBody(), e.getResponseBody().contains(messagePart));
  }

  @Test
  public void validateCreatesNothing() throws ApiException {
    storageConfigApi.validateStorageConfig(customer.getUuid(), nfsSpec("nfs", "/mnt/nfs"));
    assertTrue(CustomerConfig.getAll(customer.getUuid()).isEmpty());

    ApiException invalid =
        assertThrows(
            ApiException.class,
            () ->
                storageConfigApi.validateStorageConfig(
                    customer.getUuid(), nfsSpec("nfs", "relative")));
    assertEquals(400, invalid.getCode());

    storageConfigApi.createStorageConfig(customer.getUuid(), nfsSpec("nfs", "/mnt/nfs"), null);
    ApiException conflict =
        assertThrows(
            ApiException.class,
            () ->
                storageConfigApi.validateStorageConfig(
                    customer.getUuid(), nfsSpec("nfs", "/mnt/nfs")));
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
    StorageConfig nfs =
        storageConfigApi.createStorageConfig(customer.getUuid(), nfsSpec("nfs", "/mnt"), null);
    Customer other = ModelFactory.testCustomer("other-customer");

    // The caller isn't a user of the other customer.
    ApiException e =
        assertThrows(
            ApiException.class,
            () -> storageConfigApi.getStorageConfig(other.getUuid(), nfs.getInfo().getUuid()));
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

    S3StorageSpec s3 =
        storageConfigApi
            .getStorageConfig(customer.getUuid(), v1.getConfigUUID())
            .getSpec()
            .getStorage()
            .getS3StorageSpec();
    S3IamCredentials iam = s3.getCredentials().getS3IamCredentials();
    assertEquals(S3CredentialType.S3CredentialIam, iam.getCredentialType());
    assertTrue(s3.getGlobalBucketAccess());
  }

  private StorageConfigHandler handler(
      CustomerConfigValidator validator, Duration timeout, PlatformExecutorFactory executors) {
    RuntimeConfGetter confGetter = mock(RuntimeConfGetter.class);
    when(confGetter.getGlobalConf(GlobalConfKeys.storageConfigValidationTimeout))
        .thenReturn(timeout);
    return new StorageConfigHandler(
        validator, confGetter, executors, app.injector().instanceOf(CoordinatedShutdown.class));
  }

  private StorageConfigHandler handler(CustomerConfigValidator validator, Duration timeout) {
    return handler(validator, timeout, app.injector().instanceOf(PlatformExecutorFactory.class));
  }

  /** The v2 error of a storage check that fails with the given exception. */
  private JsonNode checkFailingWith(RuntimeException failure, int expectedStatus) {
    CustomerConfigValidator validator = mock(CustomerConfigValidator.class);
    doThrow(failure).when(validator).validateConfigDataWithValidator(any());
    PlatformServiceException e =
        assertThrows(
            PlatformServiceException.class,
            () ->
                handler(validator, Duration.ofSeconds(10))
                    .validateStorageConfig(customer.getUuid(), v2Nfs("check")));
    assertEquals(e.getContentJson().toString(), expectedStatus, e.getHttpStatus());
    return e.getContentJson().get("error");
  }

  /** A v1 validator error for BACKUP_LOCATION, as the storage validators throw it. */
  private static PlatformServiceException v1FieldError(int status, Throwable cause) {
    PlatformServiceException e =
        new PlatformServiceException(
            status, Json.toJson(Map.of("data.BACKUP_LOCATION", List.of("check failed"))));
    if (cause != null) {
      e.initCause(cause);
    }
    return e;
  }

  @Test
  public void providerErrorsKeepYbaStatusCodes() {
    // The provider's 403 is the client's storage problem, not YBA's authorization: 422.
    S3Exception accessDenied =
        (S3Exception)
            S3Exception.builder()
                .statusCode(403)
                .awsErrorDetails(AwsErrorDetails.builder().errorCode("AccessDenied").build())
                .message("Access Denied")
                .build();
    JsonNode error = checkFailingWith(v1FieldError(400, accessDenied), 422);
    assertEquals(StorageConfigHandler.VALIDATION_FAILED, error.get("code").asText());
    assertEquals(403, error.get("provider_status").asInt());
    assertEquals("AccessDenied", error.get("provider_error_code").asText());
    assertEquals(
        "check failed", error.get("errors").get("storage.backup_location").get(0).asText());

    // Throttling or a provider failure: 503, worth retrying.
    S3Exception slowDown =
        (S3Exception)
            S3Exception.builder()
                .statusCode(503)
                .awsErrorDetails(AwsErrorDetails.builder().errorCode("SlowDown").build())
                .message("Slow Down")
                .build();
    error = checkFailingWith(v1FieldError(400, slowDown), 503);
    assertEquals(StorageConfigHandler.PROVIDER_UNAVAILABLE, error.get("code").asText());
    assertEquals(503, error.get("provider_status").asInt());

    // An SDK exception the validator didn't wrap.
    error = checkFailingWith(slowDown, 503);
    assertEquals(StorageConfigHandler.PROVIDER_UNAVAILABLE, error.get("code").asText());
    assertTrue(error.get("errors").get("storage").get(0).asText().startsWith("Slow Down"));

    // A timeout on the way to the provider: no provider status.
    SdkClientException timeout =
        SdkClientException.builder()
            .message("Unable to execute HTTP request")
            .cause(new SocketTimeoutException("Read timed out"))
            .build();
    error = checkFailingWith(v1FieldError(400, timeout), 503);
    assertEquals(StorageConfigHandler.PROVIDER_UNAVAILABLE, error.get("code").asText());
    assertFalse(error.has("provider_status"));

    // An unknown host is more likely a wrong endpoint: 422.
    SdkClientException unknownHost =
        SdkClientException.builder()
            .message("Unable to execute HTTP request")
            .cause(new UnknownHostException("bucket.example.invalid"))
            .build();
    error = checkFailingWith(v1FieldError(400, unknownHost), 422);
    assertEquals(StorageConfigHandler.VALIDATION_FAILED, error.get("code").asText());

    // The Azure SDK doesn't wrap network errors in an SDK type: the validator wraps its reactor
    // exception for an unknown host, and rethrows any other one as is.
    RuntimeException azureUnknownHost =
        new RuntimeException("reactor", new UnknownHostException("account.example.invalid"));
    error = checkFailingWith(v1FieldError(400, azureUnknownHost), 422);
    assertEquals(StorageConfigHandler.VALIDATION_FAILED, error.get("code").asText());
    checkFailingWith(new RuntimeException(new ConnectException("Connection refused")), 422);
  }

  @Test
  public void validatorErrorsWithoutProvider() {
    // Found without reaching the storage, such as a malformed location: 400.
    JsonNode error = checkFailingWith(v1FieldError(400, null), 400);
    assertEquals(StorageConfigHandler.CONFIG_INVALID, error.get("code").asText());

    // The storage answered, but wrongly, such as a test object read back different: 422.
    error = checkFailingWith(v1FieldError(417, null), 422);
    assertEquals(StorageConfigHandler.VALIDATION_FAILED, error.get("code").asText());

    // Anything else is a YBA bug: 500.
    checkFailingWith(new IllegalStateException("bug"), 500);
  }

  @Test
  public void storageValidationTimesOut() throws Exception {
    CustomerConfigValidator validator = mock(CustomerConfigValidator.class);
    CountDownLatch release = new CountDownLatch(1);
    doAnswer(
            invocation -> {
              release.await(10, TimeUnit.SECONDS);
              return null;
            })
        .when(validator)
        .validateConfigDataWithValidator(any());
    StorageConfigHandler handler = handler(validator, Duration.ofMillis(200));

    PlatformServiceException e =
        assertThrows(
            PlatformServiceException.class,
            () -> handler.createStorageConfig(customer.getUuid(), v2Nfs("slow")));
    release.countDown();
    assertEquals(422, e.getHttpStatus());
    assertEquals(
        StorageConfigHandler.VALIDATION_TIMEOUT,
        e.getContentJson().get("error").get("code").asText());
    assertNull(CustomerConfig.get(customer.getUuid(), "slow"));
  }

  @Test
  public void busyPoolAnswers503() throws Exception {
    // One thread, so that a slow check makes the next one find the pool full.
    PlatformExecutorFactory oneThread =
        new PlatformExecutorFactory(
            ConfigFactory.parseString(
                "yb.storage_config_validation { core_threads = 0, max_threads = 1,"
                    + " thread_ttl = 1 minute, queue_capacity = 0 }"),
            mock(ShutdownHookHandler.class));
    CustomerConfigValidator validator = mock(CustomerConfigValidator.class);
    CountDownLatch started = new CountDownLatch(1);
    CountDownLatch release = new CountDownLatch(1);
    doAnswer(
            invocation -> {
              started.countDown();
              // Like an SDK call, ignores the interrupt of the handler's cancel.
              Uninterruptibles.awaitUninterruptibly(release, 10, TimeUnit.SECONDS);
              return null;
            })
        .when(validator)
        .validateConfigDataWithValidator(any());
    StorageConfigHandler handler = handler(validator, Duration.ofMillis(100), oneThread);

    // Times out, but keeps the only thread until released.
    assertThrows(
        PlatformServiceException.class,
        () -> handler.validateStorageConfig(customer.getUuid(), v2Nfs("slow")));
    assertTrue(started.await(10, TimeUnit.SECONDS));

    PlatformServiceException e =
        assertThrows(
            PlatformServiceException.class,
            () -> handler.validateStorageConfig(customer.getUuid(), v2Nfs("next")));
    release.countDown();
    assertEquals(503, e.getHttpStatus());
    assertEquals(
        StorageConfigHandler.VALIDATION_BUSY, e.getContentJson().get("error").get("code").asText());
  }

  private static api.v2.models.StorageConfigSpec v2Nfs(String name) {
    return new api.v2.models.StorageConfigSpec()
        .name(name)
        .storage(
            new api.v2.models.NfsStorageSpec()
                .storageType(api.v2.models.StorageType.StorageTypeNFS)
                .backupLocation("/mnt"));
  }
}
