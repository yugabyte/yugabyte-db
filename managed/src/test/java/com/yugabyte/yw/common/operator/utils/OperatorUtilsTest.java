// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.operator.utils;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyLong;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.ArgumentMatchers.nullable;
import static org.mockito.Mockito.doCallRealMethod;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.cloud.PublicCloudConstants;
import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.KubernetesManagerFactory;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.common.ValidatingFormFactory;
import com.yugabyte.yw.common.backuprestore.ybc.YbcManager;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Backup.BackupState;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Release;
import com.yugabyte.yw.models.ReleaseArtifact;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.helpers.TimeUnit;
import io.fabric8.kubernetes.api.model.KubernetesResourceList;
import io.fabric8.kubernetes.api.model.Secret;
import io.fabric8.kubernetes.client.Config;
import io.fabric8.kubernetes.client.KubernetesClient;
import io.fabric8.kubernetes.client.informers.SharedIndexInformer;
import io.fabric8.kubernetes.client.server.mock.KubernetesMixedDispatcher;
import io.fabric8.kubernetes.client.server.mock.KubernetesMockServer;
import io.fabric8.kubernetes.client.utils.Serialization;
import io.fabric8.mockwebserver.Context;
import io.fabric8.mockwebserver.ServerRequest;
import io.fabric8.mockwebserver.ServerResponse;
import java.util.Base64;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Queue;
import java.util.UUID;
import okhttp3.mockwebserver.Dispatcher;
import okhttp3.mockwebserver.MockWebServer;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mockito;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.CommonTypes.TableType;
import play.data.FormFactory;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class OperatorUtilsTest extends FakeDBApplication {

  private KubernetesClient kubernetesClient;
  private KubernetesMockServer kubernetesMockServer;
  private KubernetesClientFactory kubernetesClientFactory;

  private RuntimeConfGetter mockConfGetter;
  private Config k8sClientConfig;
  private YbcManager mockYbcManager;
  private ValidatingFormFactory mockValidatingFormFactory;
  private BeanValidator mockBeanValidator;
  private FormFactory mockFormFactory;
  private YBClientService mockYbClientService;
  private ReleaseManager mockReleaseManager;
  private UniverseImporter mockUniverseImporter;
  private OperatorUtils operatorUtils;
  private Universe testUniverse;
  private Customer testCustomer;
  private CustomerConfig testStorageConfig;
  private ObjectMapper mapper;

  @Before
  public void setup() throws Exception {
    Map<ServerRequest, Queue<ServerResponse>> responses = new HashMap<>();
    Dispatcher dispatcher = new KubernetesMixedDispatcher(responses);
    this.kubernetesMockServer =
        new KubernetesMockServer(
            new Context(Serialization.jsonMapper()),
            new MockWebServer(),
            responses,
            dispatcher,
            true /* enable https */);
    this.kubernetesMockServer.init();
    this.kubernetesClient = kubernetesMockServer.createClient();
    assertNotNull(kubernetesClient);
    kubernetesClientFactory = Mockito.mock(KubernetesClientFactory.class);
    when(kubernetesClientFactory.getKubernetesClientWithConfig(any(Config.class)))
        .thenReturn(kubernetesClient);
    mockConfGetter = Mockito.mock(RuntimeConfGetter.class);
    mockYbcManager = Mockito.mock(YbcManager.class);
    mockFormFactory = Mockito.mock(FormFactory.class);
    mockBeanValidator = Mockito.mock(BeanValidator.class);
    mockYbClientService = Mockito.mock(YBClientService.class);
    mockValidatingFormFactory = spy(new ValidatingFormFactory(mockFormFactory, mockBeanValidator));
    doCallRealMethod()
        .when(mockValidatingFormFactory)
        .getFormDataOrBadRequest(any(JsonNode.class), any());
    mockReleaseManager = Mockito.mock(ReleaseManager.class);
    mockUniverseImporter = Mockito.mock(UniverseImporter.class);
    operatorUtils =
        spy(
            new OperatorUtils(
                mockConfGetter,
                mockReleaseManager,
                mockYbcManager,
                mockValidatingFormFactory,
                mockYbClientService,
                kubernetesClientFactory,
                mockUniverseImporter,
                Mockito.mock(KubernetesManagerFactory.class)));

    testCustomer = ModelFactory.testCustomer();
    testUniverse = ModelFactory.createUniverse("operator-universe", testCustomer.getId());
    testStorageConfig = ModelFactory.createS3StorageConfig(testCustomer, "operator-storage");
    when(mockConfGetter.getGlobalConf(GlobalConfKeys.KubernetesOperatorCustomerUUID))
        .thenReturn(testCustomer.getUuid().toString());
    mapper = new ObjectMapper();
  }

  @After
  public void tearDown() {
    this.kubernetesMockServer.destroy();
  }

  private ObjectNode getScheduleBackupParamsJson() {
    ObjectNode spec = Json.newObject();
    spec.put("keyspace", "testdb");
    spec.put("backupType", "PGSQL_TABLE_TYPE");
    spec.put("storageConfig", "operator-storage");
    spec.put("universe", "operator-universe");
    spec.put("schedulingFrequency", "3600000");
    spec.put("incrementalBackupFrequency", "900000");
    return spec;
  }

  private ObjectNode getIncrementalBackupParamsJson() {
    ObjectNode spec = Json.newObject();
    spec.put("keyspace", "testdb");
    spec.put("backupType", "PGSQL_TABLE_TYPE");
    spec.put("storageConfig", "operator-storage");
    spec.put("universe", "operator-universe");
    spec.put("incrementalBackupBase", "full-backup");
    return spec;
  }

  @Test
  public void testGenerateBackupParamsScheduledBackupSuccess() throws Exception {
    doReturn(testStorageConfig.getConfigUUID())
        .when(operatorUtils)
        .getStorageConfigUUIDFromName(anyString(), nullable(SharedIndexInformer.class));
    doReturn(testUniverse)
        .when(operatorUtils)
        .getUniverseFromNameAndNamespace(anyLong(), anyString(), nullable(String.class));

    ObjectNode spec = getScheduleBackupParamsJson();
    BackupRequestParams scheduleParams = operatorUtils.getBackupRequestFromCr(spec, null, null);
    assertEquals(scheduleParams.schedulingFrequency, 3600000);
    assertEquals(scheduleParams.frequencyTimeUnit, TimeUnit.MILLISECONDS);
    assertEquals(scheduleParams.incrementalBackupFrequency, 900000);
    assertEquals(scheduleParams.incrementalBackupFrequencyTimeUnit, TimeUnit.MILLISECONDS);
    assertEquals(scheduleParams.getUniverseUUID(), testUniverse.getUniverseUUID());
    assertEquals(scheduleParams.storageConfigUUID, testStorageConfig.getConfigUUID());
    assertEquals(scheduleParams.keyspaceTableList.size(), 1);
    assertEquals(scheduleParams.keyspaceTableList.get(0).keyspace, "testdb");
    assertEquals(scheduleParams.backupType, TableType.PGSQL_TABLE_TYPE);
  }

  @Test
  public void testGenerateBackupParamsScheduledBackupFailUniverseNotFound() throws Exception {
    doReturn(null)
        .when(operatorUtils)
        .getUniverseFromNameAndNamespace(anyLong(), anyString(), nullable(String.class));

    ObjectNode spec = getScheduleBackupParamsJson();
    Exception ex =
        assertThrows(Exception.class, () -> operatorUtils.getBackupRequestFromCr(spec, null, null));
    assertEquals(ex.getMessage(), "No universe found with name operator-universe");
  }

  @Test
  public void testGenerateBackupParamsScheduledBackupFailStorageConfigNotFound() throws Exception {
    doReturn(null)
        .when(operatorUtils)
        .getStorageConfigUUIDFromName(anyString(), nullable(SharedIndexInformer.class));
    doReturn(testUniverse)
        .when(operatorUtils)
        .getUniverseFromNameAndNamespace(anyLong(), anyString(), nullable(String.class));

    ObjectNode spec = getScheduleBackupParamsJson();
    Exception ex =
        assertThrows(Exception.class, () -> operatorUtils.getBackupRequestFromCr(spec, null, null));
    assertEquals(ex.getMessage(), "No storage config found with name operator-storage");
  }

  @Test
  public void testGenerateBackupParamsIncrementalBackupSuccess() throws Exception {
    doReturn(UUID.fromString(testStorageConfig.getConfigUUID().toString()))
        .when(operatorUtils)
        .getStorageConfigUUIDFromName(anyString(), nullable(SharedIndexInformer.class));
    doReturn(testUniverse)
        .when(operatorUtils)
        .getUniverseFromNameAndNamespace(anyLong(), anyString(), nullable(String.class));
    Backup backup =
        ModelFactory.createBackup(
            testCustomer.getUuid(),
            testUniverse.getUniverseUUID(),
            testStorageConfig.getConfigUUID());
    doReturn(backup)
        .when(operatorUtils)
        .getBaseBackup(anyString(), nullable(String.class), any(Customer.class));
    backup.setState(BackupState.Completed);
    backup.save();

    ObjectNode spec = getIncrementalBackupParamsJson();
    BackupRequestParams backupParams = operatorUtils.getBackupRequestFromCr(spec, null, null);
    assertEquals(backupParams.getUniverseUUID(), testUniverse.getUniverseUUID());
    assertEquals(backupParams.storageConfigUUID, testStorageConfig.getConfigUUID());
    assertEquals(backupParams.keyspaceTableList.size(), 1);
    assertEquals(backupParams.keyspaceTableList.get(0).keyspace, "testdb");
    assertEquals(backupParams.backupType, TableType.PGSQL_TABLE_TYPE);
    assertEquals(backupParams.baseBackupUUID, backup.getBackupUUID());
  }

  @Test
  public void testGenerateBackupParamsIncrementalBackupDifferentStorageConfig() throws Exception {
    doReturn(testStorageConfig.getConfigUUID())
        .when(operatorUtils)
        .getStorageConfigUUIDFromName(anyString(), nullable(SharedIndexInformer.class));
    doReturn(testUniverse)
        .when(operatorUtils)
        .getUniverseFromNameAndNamespace(anyLong(), anyString(), nullable(String.class));
    // Different storageConfig
    CustomerConfig config_2 = ModelFactory.createS3StorageConfig(testCustomer, "test-2");
    Backup backup =
        ModelFactory.createBackup(
            testCustomer.getUuid(), testUniverse.getUniverseUUID(), config_2.getConfigUUID());
    doReturn(backup)
        .when(operatorUtils)
        .getBaseBackup(anyString(), nullable(String.class), any(Customer.class));
    backup.setState(BackupState.Completed);
    backup.save();

    ObjectNode spec = getIncrementalBackupParamsJson();
    Exception ex =
        assertThrows(Exception.class, () -> operatorUtils.getBackupRequestFromCr(spec, null, null));
    assertEquals(
        ex.getMessage(),
        "Invalid cr values: Storage config and Universe should be same for incremental backup");
  }

  @Test
  public void testGenerateBackupParamsIncrementalBackupDifferentUniverse() throws Exception {
    doReturn(testStorageConfig.getConfigUUID())
        .when(operatorUtils)
        .getStorageConfigUUIDFromName(anyString(), nullable(SharedIndexInformer.class));
    doReturn(testUniverse)
        .when(operatorUtils)
        .getUniverseFromNameAndNamespace(anyLong(), anyString(), nullable(String.class));
    // Different storageConfig
    Universe testUniverse_2 = ModelFactory.createUniverse("test-2", testCustomer.getId());
    Backup backup =
        ModelFactory.createBackup(
            testCustomer.getUuid(),
            testUniverse_2.getUniverseUUID(),
            testStorageConfig.getConfigUUID());
    doReturn(backup)
        .when(operatorUtils)
        .getBaseBackup(anyString(), nullable(String.class), any(Customer.class));
    backup.setState(BackupState.Completed);
    backup.save();

    ObjectNode spec = getIncrementalBackupParamsJson();
    Exception ex =
        assertThrows(Exception.class, () -> operatorUtils.getBackupRequestFromCr(spec, null, null));
    assertEquals(
        ex.getMessage(),
        "Invalid cr values: Storage config and Universe should be same for incremental backup");
  }

  private void resetMockKubernetesClientForChecking() {
    kubernetesClient = kubernetesMockServer.createClient();
  }

  @Test
  public void testCreateReleaseCrS3() throws Exception {
    Release release = Release.create("2025.2.0.0", "LTS");
    ReleaseArtifact.S3File k8sS3File = new ReleaseArtifact.S3File();
    k8sS3File.path = "https://example.com/k8s-artifact.tgz";
    k8sS3File.accessKeyId = "accessKeyId";
    ReleaseArtifact k8sArtifact =
        ReleaseArtifact.create("sha2561234", ReleaseArtifact.Platform.KUBERNETES, null, k8sS3File);
    ReleaseArtifact.S3File x86_64S3File = new ReleaseArtifact.S3File();
    x86_64S3File.path = "https://example.com/x86_64-artifact.tgz";
    x86_64S3File.accessKeyId = "accessKeyId";
    ReleaseArtifact x86_64Artifact =
        ReleaseArtifact.create(
            "sha256123456",
            ReleaseArtifact.Platform.LINUX,
            PublicCloudConstants.Architecture.x86_64,
            x86_64S3File);
    release.addArtifact(k8sArtifact);
    release.addArtifact(x86_64Artifact);
    operatorUtils.createReleaseCr(release, k8sArtifact, x86_64Artifact, "namespace", "awsSecret");
    resetMockKubernetesClientForChecking();
    KubernetesResourceList<io.yugabyte.operator.v1alpha1.Release> releases =
        kubernetesClient
            .resources(io.yugabyte.operator.v1alpha1.Release.class)
            .inNamespace("namespace")
            .list();
    assertEquals(1, releases.getItems().size());
    assertEquals("2025.2.0.0", releases.getItems().get(0).getMetadata().getName());
    assertEquals(
        "https://example.com/k8s-artifact.tgz",
        releases
            .getItems()
            .get(0)
            .getSpec()
            .getConfig()
            .getDownloadConfig()
            .getS3()
            .getPaths()
            .getHelmChart());
    assertEquals(
        "https://example.com/x86_64-artifact.tgz",
        releases
            .getItems()
            .get(0)
            .getSpec()
            .getConfig()
            .getDownloadConfig()
            .getS3()
            .getPaths()
            .getX86_64());
    assertEquals(
        "awsSecret",
        releases
            .getItems()
            .get(0)
            .getSpec()
            .getConfig()
            .getDownloadConfig()
            .getS3()
            .getSecretAccessKeySecret()
            .getName());
    assertEquals(
        "namespace",
        releases
            .getItems()
            .get(0)
            .getSpec()
            .getConfig()
            .getDownloadConfig()
            .getS3()
            .getSecretAccessKeySecret()
            .getNamespace());
  }

  @Test
  public void testCreateReleaseCrHttp() throws Exception {
    Release release = Release.create("2025.2.0.0", "LTS");
    String k8sUrl = "https://example.com/k8s-artifact.tgz";
    String x86_64Url = "https://example.com/x86_64-artifact.tgz";
    ReleaseArtifact k8sArtifact =
        ReleaseArtifact.create("sha2561234", ReleaseArtifact.Platform.KUBERNETES, null, k8sUrl);
    ReleaseArtifact x86_64Artifact =
        ReleaseArtifact.create(
            "sha256123456",
            ReleaseArtifact.Platform.LINUX,
            PublicCloudConstants.Architecture.x86_64,
            x86_64Url);
    release.addArtifact(k8sArtifact);
    release.addArtifact(x86_64Artifact);
    operatorUtils.createReleaseCr(release, k8sArtifact, x86_64Artifact, "namespace", "awsSecret");
    resetMockKubernetesClientForChecking();
    KubernetesResourceList<io.yugabyte.operator.v1alpha1.Release> releases =
        kubernetesClient
            .resources(io.yugabyte.operator.v1alpha1.Release.class)
            .inNamespace("namespace")
            .list();
    assertEquals(1, releases.getItems().size());
    assertEquals("2025.2.0.0", releases.getItems().get(0).getMetadata().getName());
    assertEquals(
        k8sUrl,
        releases
            .getItems()
            .get(0)
            .getSpec()
            .getConfig()
            .getDownloadConfig()
            .getHttp()
            .getPaths()
            .getHelmChart());
    assertEquals(
        x86_64Url,
        releases
            .getItems()
            .get(0)
            .getSpec()
            .getConfig()
            .getDownloadConfig()
            .getHttp()
            .getPaths()
            .getX86_64());
  }

  @Test
  public void testCreateReleaseCrGcs() throws Exception {
    Release release = Release.create("2025.2.0.0", "LTS");
    ReleaseArtifact.GCSFile k8sGcsFile = new ReleaseArtifact.GCSFile();
    k8sGcsFile.path = "https://example.com/k8s-artifact.tgz";
    k8sGcsFile.credentialsJson = "credentialsJson";
    ReleaseArtifact k8sArtifact =
        ReleaseArtifact.create("sha2561234", ReleaseArtifact.Platform.KUBERNETES, null, k8sGcsFile);
    ReleaseArtifact.GCSFile x86_64GcsFile = new ReleaseArtifact.GCSFile();
    x86_64GcsFile.path = "https://example.com/x86_64-artifact.tgz";
    x86_64GcsFile.credentialsJson = "credentialsJson";
    ReleaseArtifact x86_64Artifact =
        ReleaseArtifact.create(
            "sha256123456",
            ReleaseArtifact.Platform.LINUX,
            PublicCloudConstants.Architecture.x86_64,
            x86_64GcsFile);
    operatorUtils.createReleaseCr(release, k8sArtifact, x86_64Artifact, "namespace", "awsSecret");
    resetMockKubernetesClientForChecking();
    KubernetesResourceList<io.yugabyte.operator.v1alpha1.Release> releases =
        kubernetesClient
            .resources(io.yugabyte.operator.v1alpha1.Release.class)
            .inNamespace("namespace")
            .list();
    assertEquals(1, releases.getItems().size());
    assertEquals("2025.2.0.0", releases.getItems().get(0).getMetadata().getName());
    assertEquals(
        "https://example.com/k8s-artifact.tgz",
        releases
            .getItems()
            .get(0)
            .getSpec()
            .getConfig()
            .getDownloadConfig()
            .getGcs()
            .getPaths()
            .getHelmChart());
    assertEquals(
        "https://example.com/x86_64-artifact.tgz",
        releases
            .getItems()
            .get(0)
            .getSpec()
            .getConfig()
            .getDownloadConfig()
            .getGcs()
            .getPaths()
            .getX86_64());
    assertEquals(
        "awsSecret",
        releases
            .getItems()
            .get(0)
            .getSpec()
            .getConfig()
            .getDownloadConfig()
            .getGcs()
            .getCredentialsJsonSecret()
            .getName());
    assertEquals(
        "namespace",
        releases
            .getItems()
            .get(0)
            .getSpec()
            .getConfig()
            .getDownloadConfig()
            .getGcs()
            .getCredentialsJsonSecret()
            .getNamespace());
  }

  @Test
  public void testCreateSecretCr() throws Exception {
    operatorUtils.createSecretCr("secret", "namespace", "key", "value");
    resetMockKubernetesClientForChecking();
    Secret secret = kubernetesClient.secrets().inNamespace("namespace").withName("secret").get();
    assertNotNull(secret);
    assertEquals(
        Base64.getEncoder().encodeToString("value".getBytes()), secret.getData().get("key"));
  }

  @Test
  public void testCreateStorageConfigCrS3() throws Exception {
    CustomerConfig s3Config = ModelFactory.createS3StorageConfig(testCustomer, "test-s3-config");
    ObjectNode s3Data = s3Config.getData();
    s3Data.put("AWS_ACCESS_KEY_ID", "test-access-key");
    s3Data.put("AWS_HOST_BASE", "s3.amazonaws.com");
    s3Data.put("IAM_INSTANCE_PROFILE", false);
    s3Data.put("BACKUP_LOCATION", "s3://test-bucket/backups");
    s3Data.put("PATH_STYLE_ACCESS", true);
    s3Config.setData(s3Data);
    s3Config.save();

    operatorUtils.createStorageConfigCr(s3Config, "test-namespace", "aws-secret");

    resetMockKubernetesClientForChecking();
    KubernetesResourceList<io.yugabyte.operator.v1alpha1.StorageConfig> storageConfigs =
        kubernetesClient
            .resources(io.yugabyte.operator.v1alpha1.StorageConfig.class)
            .inNamespace("test-namespace")
            .list();
    assertEquals(storageConfigs.getItems().size(), 1);

    io.yugabyte.operator.v1alpha1.StorageConfig config = storageConfigs.getItems().get(0);
    assertEquals("test-s3-config", config.getMetadata().getName());
    assertEquals("test-namespace", config.getMetadata().getNamespace());
    assertEquals("STORAGE_S3", config.getSpec().getConfig_type().toString());

    io.yugabyte.operator.v1alpha1.storageconfigspec.Data data = config.getSpec().getData();
    assertEquals("test-access-key", data.getAWS_ACCESS_KEY_ID());
    assertEquals("s3.amazonaws.com", data.getAWS_HOST_BASE());
    assertFalse(data.getUSE_IAM());
    assertEquals("s3://test-bucket/backups", data.getBACKUP_LOCATION());
    assertTrue(data.getPATH_STYLE_ACCESS());

    assertEquals("aws-secret", config.getSpec().getAwsSecretAccessKeySecret().getName());
    assertEquals("test-namespace", config.getSpec().getAwsSecretAccessKeySecret().getNamespace());
  }

  @Test
  public void testCreateStorageConfigCrGCS() throws Exception {
    CustomerConfig gcsConfig = ModelFactory.createGcsStorageConfig(testCustomer, "test-gcs-config");
    ObjectNode gcsData = gcsConfig.getData();
    gcsData.put("BACKUP_LOCATION", "gs://test-bucket/backups");
    gcsData.put("USE_GCP_IAM", true);
    gcsConfig.setData(gcsData);
    gcsConfig.save();

    operatorUtils.createStorageConfigCr(gcsConfig, "test-namespace", "gcs-secret");

    resetMockKubernetesClientForChecking();
    KubernetesResourceList<io.yugabyte.operator.v1alpha1.StorageConfig> storageConfigs =
        kubernetesClient
            .resources(io.yugabyte.operator.v1alpha1.StorageConfig.class)
            .inNamespace("test-namespace")
            .list();
    assertEquals(storageConfigs.getItems().size(), 1);

    io.yugabyte.operator.v1alpha1.StorageConfig config = storageConfigs.getItems().get(0);
    assertEquals("test-gcs-config", config.getMetadata().getName());
    assertEquals("test-namespace", config.getMetadata().getNamespace());
    assertEquals("STORAGE_GCS", config.getSpec().getConfig_type().toString());

    io.yugabyte.operator.v1alpha1.storageconfigspec.Data data = config.getSpec().getData();
    assertTrue(data.getUSE_IAM());
    assertEquals("gs://test-bucket/backups", data.getBACKUP_LOCATION());

    assertEquals("gcs-secret", config.getSpec().getGcsCredentialsJsonSecret().getName());
    assertEquals("test-namespace", config.getSpec().getGcsCredentialsJsonSecret().getNamespace());
  }

  @Test
  public void testCreateStorageConfigCrNFS() throws Exception {
    CustomerConfig nfsConfig =
        ModelFactory.createNfsStorageConfig(testCustomer, "test-nfs-config", "/mnt/nfs/backups");

    operatorUtils.createStorageConfigCr(nfsConfig, "test-namespace", null);

    resetMockKubernetesClientForChecking();
    KubernetesResourceList<io.yugabyte.operator.v1alpha1.StorageConfig> storageConfigs =
        kubernetesClient
            .resources(io.yugabyte.operator.v1alpha1.StorageConfig.class)
            .inNamespace("test-namespace")
            .list();
    assertEquals(storageConfigs.getItems().size(), 1);

    io.yugabyte.operator.v1alpha1.StorageConfig config = storageConfigs.getItems().get(0);
    assertEquals("test-nfs-config", config.getMetadata().getName());
    assertEquals("test-namespace", config.getMetadata().getNamespace());
    assertEquals("STORAGE_NFS", config.getSpec().getConfig_type().toString());

    io.yugabyte.operator.v1alpha1.storageconfigspec.Data data = config.getSpec().getData();
    assertEquals("/mnt/nfs/backups", data.getBACKUP_LOCATION());
  }

  @Test
  public void testCreateStorageConfigCrAzure() throws Exception {
    CustomerConfig azureConfig =
        ModelFactory.createAZStorageConfig(testCustomer, "test-azure-config");
    ObjectNode azureData = azureConfig.getData();
    azureData.put("BACKUP_LOCATION", "https://testaccount.blob.core.windows.net/backups");
    azureConfig.setData(azureData);
    azureConfig.save();

    operatorUtils.createStorageConfigCr(azureConfig, "test-namespace", "azure-secret");

    resetMockKubernetesClientForChecking();
    KubernetesResourceList<io.yugabyte.operator.v1alpha1.StorageConfig> storageConfigs =
        kubernetesClient
            .resources(io.yugabyte.operator.v1alpha1.StorageConfig.class)
            .inNamespace("test-namespace")
            .list();
    assertEquals(storageConfigs.getItems().size(), 1);

    io.yugabyte.operator.v1alpha1.StorageConfig config = storageConfigs.getItems().get(0);
    assertEquals("test-azure-config", config.getMetadata().getName());
    assertEquals("test-namespace", config.getMetadata().getNamespace());
    assertEquals("STORAGE_AZ", config.getSpec().getConfig_type().toString());

    io.yugabyte.operator.v1alpha1.storageconfigspec.Data data = config.getSpec().getData();
    assertEquals("https://testaccount.blob.core.windows.net/backups", data.getBACKUP_LOCATION());

    assertEquals("azure-secret", config.getSpec().getAzureStorageSasTokenSecret().getName());
    assertEquals("test-namespace", config.getSpec().getAzureStorageSasTokenSecret().getNamespace());
  }

  @Test
  public void testCreateStorageConfigCrUnsupportedType() throws Exception {
    CustomerConfig unsupportedConfig =
        ModelFactory.createS3StorageConfig(testCustomer, "test-unsupported");
    // Change the name to something unsupported
    unsupportedConfig.setName("UNSUPPORTED");
    unsupportedConfig.save();

    Exception ex =
        assertThrows(
            Exception.class,
            () -> operatorUtils.createStorageConfigCr(unsupportedConfig, "test-namespace", null));
    assertEquals(
        "Unable to create storage config: UNSUPPORTED type: StorageConfig", ex.getMessage());
  }

  @Test
  public void testCreateBackupScheduleCr() throws Exception {
    Schedule backupSchedule =
        ModelFactory.createScheduleBackupRequestParams(
            testCustomer.getUuid(),
            testUniverse.getUniverseUUID(),
            testStorageConfig.getConfigUUID(),
            TaskType.BackupUniverse);

    // Set additional schedule properties
    BackupRequestParams params =
        Json.fromJson(backupSchedule.getTaskParams(), BackupRequestParams.class);
    params.schedulingFrequency = 3600L;
    params.frequencyTimeUnit = TimeUnit.MINUTES;
    backupSchedule.setTaskParams(Json.toJson(params));
    backupSchedule.save();

    operatorUtils.createBackupScheduleCr(
        backupSchedule, "test-schedule", "test-storage-config", "test-namespace");

    resetMockKubernetesClientForChecking();
    KubernetesResourceList<io.yugabyte.operator.v1alpha1.BackupSchedule> backupSchedules =
        kubernetesClient
            .resources(io.yugabyte.operator.v1alpha1.BackupSchedule.class)
            .inNamespace("test-namespace")
            .list();
    assertEquals(backupSchedules.getItems().size(), 1);

    io.yugabyte.operator.v1alpha1.BackupSchedule schedule = backupSchedules.getItems().get(0);
    assertEquals("test-schedule", schedule.getMetadata().getName());
    assertEquals("test-namespace", schedule.getMetadata().getNamespace());

    io.yugabyte.operator.v1alpha1.BackupScheduleSpec spec = schedule.getSpec();
    assertEquals(spec.getStorageConfig(), "test-storage-config");
    assertEquals(spec.getUniverse(), "operator-universe");
    assertEquals("PGSQL_TABLE_TYPE", spec.getBackupType().toString());
    assertEquals("foo", spec.getKeyspace()); // From ModelFactory.createScheduleBackup
    assertEquals(
        3600L * 60 * 1000,
        spec.getSchedulingFrequency().longValue()); // From ModelFactory.createScheduleBackup
  }

  @Test
  public void testCreateBackupScheduleCrAlreadyExists() throws Exception {
    Schedule backupSchedule =
        ModelFactory.createScheduleBackupRequestParams(
            testCustomer.getUuid(),
            testUniverse.getUniverseUUID(),
            testStorageConfig.getConfigUUID(),
            TaskType.BackupUniverse);
    backupSchedule.save();

    // Create the first backup schedule
    operatorUtils.createBackupScheduleCr(
        backupSchedule, "test-schedule", "test-storage-config", "test-namespace");
    resetMockKubernetesClientForChecking();
    when(kubernetesClientFactory.getKubernetesClientWithConfig(any(Config.class)))
        .thenReturn(kubernetesClient);
    // Try to create the same backup schedule again
    operatorUtils.createBackupScheduleCr(
        backupSchedule, "test-schedule", "test-storage-config", "test-namespace");
    resetMockKubernetesClientForChecking();
    // Should only have one backup schedule (no duplicates)
    KubernetesResourceList<io.yugabyte.operator.v1alpha1.BackupSchedule> backupSchedules =
        kubernetesClient
            .resources(io.yugabyte.operator.v1alpha1.BackupSchedule.class)
            .inNamespace("test-namespace")
            .list();
    assertEquals(backupSchedules.getItems().size(), 1);
  }

  /*--- createUniverseCr tests ---*/

  @Test
  public void testCreateUniverseCrSuccess() throws Exception {
    // Setup test data
    String providerName = "test-provider";
    String namespace = "test-namespace";
    String ycqlSecretName = "ycql-secret";
    String ysqlSecretName = "ysql-secret";

    // Mock universe details
    Universe.UniverseUpdater updater =
        universe -> {
          UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
          universeDetails.getPrimaryCluster().userIntent.enableNodeToNodeEncrypt = true;
          universeDetails.getPrimaryCluster().userIntent.enableClientToNodeEncrypt = true;
          universeDetails.getPrimaryCluster().userIntent.ybSoftwareVersion = "2.20.0.0-b1";
          universeDetails.getPrimaryCluster().userIntent.enableIPV6 = false;
          universeDetails.getPrimaryCluster().userIntent.enableExposingService =
              UniverseDefinitionTaskParams.ExposingServiceState.EXPOSED;
          // Ensure YCQL and YSQL are enabled (default values)
          universeDetails.getPrimaryCluster().userIntent.enableYCQL = true;
          universeDetails.getPrimaryCluster().userIntent.enableYSQL = true;
          universe.setUniverseDetails(universeDetails);
        };
    Universe.saveDetails(testUniverse.getUniverseUUID(), updater);
    testUniverse = Universe.getOrBadRequest(testUniverse.getUniverseUUID());
    UniverseDefinitionTaskParams.UserIntent userIntent =
        testUniverse.getUniverseDetails().getPrimaryCluster().userIntent;

    // Mock universeImporter calls
    Mockito.doNothing()
        .when(mockUniverseImporter)
        .setYcqlSpec(any(), any(Boolean.class), any(Boolean.class));
    Mockito.doNothing()
        .when(mockUniverseImporter)
        .setYsqlSpec(any(), any(Boolean.class), any(Boolean.class));
    Mockito.doNothing().when(mockUniverseImporter).setGflagsSpecFromUniverse(any(), any());
    Mockito.doNothing().when(mockUniverseImporter).setDeviceInfoSpecFromUniverse(any(), any());
    Mockito.doNothing()
        .when(mockUniverseImporter)
        .setMasterDeviceInfoSpecFromUniverse(any(), any());
    Mockito.doNothing()
        .when(mockUniverseImporter)
        .setYbcThrottleParametersSpecFromUniverse(any(), any());
    Mockito.doNothing()
        .when(mockUniverseImporter)
        .setKubernetesOverridesSpecFromUniverse(any(), any());

    // Execute method
    operatorUtils.createUniverseCr(testUniverse, providerName, namespace);

    // Verify YBUniverse was created with correct spec
    resetMockKubernetesClientForChecking();
    io.yugabyte.operator.v1alpha1.YBUniverse createdUniverse =
        kubernetesClient
            .resources(io.yugabyte.operator.v1alpha1.YBUniverse.class)
            .inNamespace(namespace)
            .withName(testUniverse.getName())
            .get();

    assertNotNull(createdUniverse);
    assertEquals(testUniverse.getName(), createdUniverse.getMetadata().getName());
    assertEquals(namespace, createdUniverse.getMetadata().getNamespace());

    io.yugabyte.operator.v1alpha1.YBUniverseSpec spec = createdUniverse.getSpec();
    assertNotNull(spec);
    assertEquals(testUniverse.getName(), spec.getUniverseName());
    assertEquals(Long.valueOf(userIntent.numNodes), spec.getNumNodes());
    assertEquals(Long.valueOf(userIntent.replicationFactor), spec.getReplicationFactor());
    assertEquals(userIntent.enableNodeToNodeEncrypt, spec.getEnableNodeToNodeEncrypt());
    assertEquals(userIntent.enableClientToNodeEncrypt, spec.getEnableClientToNodeEncrypt());
    assertEquals(userIntent.ybSoftwareVersion, spec.getYbSoftwareVersion());
    assertEquals(providerName, spec.getProviderName());
    assertEquals(userIntent.enableIPV6, spec.getEnableIPV6());
    assertEquals(true, spec.getEnableLoadBalancer()); // enableExposingService == EXPOSED
    assertEquals(testUniverse.getUniverseDetails().universePaused, spec.getPaused());

    // Verify universeImporter methods were called
    verify(mockUniverseImporter)
        .setYcqlSpec(any(), eq(userIntent.enableYCQL), eq(userIntent.enableYCQLAuth));
    verify(mockUniverseImporter)
        .setYsqlSpec(any(), eq(userIntent.enableYSQL), eq(userIntent.enableYSQLAuth));
    verify(mockUniverseImporter).setGflagsSpecFromUniverse(any(), eq(testUniverse));
    verify(mockUniverseImporter).setDeviceInfoSpecFromUniverse(any(), eq(testUniverse));
    verify(mockUniverseImporter).setMasterDeviceInfoSpecFromUniverse(any(), eq(testUniverse));
    verify(mockUniverseImporter).setYbcThrottleParametersSpecFromUniverse(any(), eq(testUniverse));
    verify(mockUniverseImporter).setKubernetesOverridesSpecFromUniverse(any(), eq(testUniverse));
  }

  @Test
  public void testCreateUniverseCrWithDisabledServices() throws Exception {
    // Setup test data
    String providerName = "test-provider";
    String namespace = "test-namespace";
    String ycqlSecretName = null; // No YCQL
    String ysqlSecretName = null; // No YSQL

    // Mock universe details
    Universe.UniverseUpdater updater =
        universe -> {
          UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
          universeDetails.getPrimaryCluster().userIntent.enableYCQL = false;
          universeDetails.getPrimaryCluster().userIntent.enableYSQL = false;
          universeDetails.getPrimaryCluster().userIntent.enableExposingService =
              UniverseDefinitionTaskParams.ExposingServiceState.UNEXPOSED;
          universe.setUniverseDetails(universeDetails);
        };
    Universe.saveDetails(testUniverse.getUniverseUUID(), updater);
    testUniverse = Universe.getOrBadRequest(testUniverse.getUniverseUUID());

    // Mock universeImporter calls
    Mockito.doNothing().when(mockUniverseImporter).setGflagsSpecFromUniverse(any(), any());
    Mockito.doNothing().when(mockUniverseImporter).setDeviceInfoSpecFromUniverse(any(), any());
    Mockito.doNothing()
        .when(mockUniverseImporter)
        .setMasterDeviceInfoSpecFromUniverse(any(), any());
    Mockito.doNothing()
        .when(mockUniverseImporter)
        .setYbcThrottleParametersSpecFromUniverse(any(), any());
    Mockito.doNothing()
        .when(mockUniverseImporter)
        .setKubernetesOverridesSpecFromUniverse(any(), any());

    // Execute method
    operatorUtils.createUniverseCr(testUniverse, providerName, namespace);

    // Verify YBUniverse was created
    resetMockKubernetesClientForChecking();
    io.yugabyte.operator.v1alpha1.YBUniverse createdUniverse =
        kubernetesClient
            .resources(io.yugabyte.operator.v1alpha1.YBUniverse.class)
            .inNamespace(namespace)
            .withName(testUniverse.getName())
            .get();

    assertNotNull(createdUniverse);
    io.yugabyte.operator.v1alpha1.YBUniverseSpec spec = createdUniverse.getSpec();
    assertEquals(false, spec.getEnableLoadBalancer()); // enableExposingService == UNEXPOSED

    // Verify universeImporter methods were called with correct parameters
    verify(mockUniverseImporter).setYcqlSpec(any(), eq(false), eq(false));
    verify(mockUniverseImporter).setYsqlSpec(any(), eq(false), eq(false));
  }

  @Test
  public void testCreateUniverseCrAlreadyExists() throws Exception {
    // Setup test data
    String providerName = "test-provider";
    String namespace = "test-namespace";
    String ycqlSecretName = "ycql-secret";
    String ysqlSecretName = "ysql-secret";

    // Mock universeImporter calls before first creation
    Mockito.doNothing()
        .when(mockUniverseImporter)
        .setYcqlSpec(any(), any(Boolean.class), any(Boolean.class));
    Mockito.doNothing()
        .when(mockUniverseImporter)
        .setYsqlSpec(any(), any(Boolean.class), any(Boolean.class));
    Mockito.doNothing().when(mockUniverseImporter).setGflagsSpecFromUniverse(any(), any());
    Mockito.doNothing().when(mockUniverseImporter).setDeviceInfoSpecFromUniverse(any(), any());
    Mockito.doNothing()
        .when(mockUniverseImporter)
        .setMasterDeviceInfoSpecFromUniverse(any(), any());
    Mockito.doNothing()
        .when(mockUniverseImporter)
        .setYbcThrottleParametersSpecFromUniverse(any(), any());
    Mockito.doNothing()
        .when(mockUniverseImporter)
        .setKubernetesOverridesSpecFromUniverse(any(), any());

    // Create a universe CR first
    operatorUtils.createUniverseCr(testUniverse, providerName, namespace);
    resetMockKubernetesClientForChecking(); // Reset before next create attempt
    when(kubernetesClientFactory.getKubernetesClientWithConfig(any(Config.class)))
        .thenReturn(kubernetesClient);

    // Verify first creation was called
    verify(mockUniverseImporter).setYcqlSpec(any(), any(Boolean.class), any(Boolean.class));

    // Try to create again - should skip creation
    operatorUtils.createUniverseCr(testUniverse, providerName, namespace);

    // Verify only one universe CR exists
    resetMockKubernetesClientForChecking();
    List<io.yugabyte.operator.v1alpha1.YBUniverse> universes =
        kubernetesClient
            .resources(io.yugabyte.operator.v1alpha1.YBUniverse.class)
            .inNamespace(namespace)
            .list()
            .getItems();

    assertEquals(1, universes.size());
  }

  @Test
  public void testCreateUniverseCrWithNullSecrets() throws Exception {
    // Setup test data
    String providerName = "test-provider";
    String namespace = "test-namespace";
    String ycqlSecretName = null;
    String ysqlSecretName = null;

    // Mock universeImporter calls
    Mockito.doNothing().when(mockUniverseImporter).setGflagsSpecFromUniverse(any(), any());
    Mockito.doNothing().when(mockUniverseImporter).setDeviceInfoSpecFromUniverse(any(), any());
    Mockito.doNothing()
        .when(mockUniverseImporter)
        .setMasterDeviceInfoSpecFromUniverse(any(), any());
    Mockito.doNothing()
        .when(mockUniverseImporter)
        .setYbcThrottleParametersSpecFromUniverse(any(), any());
    Mockito.doNothing()
        .when(mockUniverseImporter)
        .setKubernetesOverridesSpecFromUniverse(any(), any());

    // Execute method
    operatorUtils.createUniverseCr(testUniverse, providerName, namespace);

    resetMockKubernetesClientForChecking();
    // Verify YBUniverse was created
    io.yugabyte.operator.v1alpha1.YBUniverse createdUniverse =
        kubernetesClient
            .resources(io.yugabyte.operator.v1alpha1.YBUniverse.class)
            .inNamespace(namespace)
            .withName(testUniverse.getName())
            .get();

    assertNotNull(createdUniverse);

    // Verify universeImporter methods were called with null secrets
    Mockito.verify(mockUniverseImporter).setYcqlSpec(any(), any(Boolean.class), eq(false));
    Mockito.verify(mockUniverseImporter).setYsqlSpec(any(), any(Boolean.class), eq(false));
  }

  @Test
  public void testCreateUniverseCrWithPausedUniverse() throws Exception {
    // Setup test data
    String providerName = "test-provider";
    String namespace = "test-namespace";
    String ycqlSecretName = "ycql-secret";
    String ysqlSecretName = "ysql-secret";

    // Set universe as paused
    testUniverse.getUniverseDetails().universePaused = true;

    // Mock universeImporter calls
    Mockito.doNothing()
        .when(mockUniverseImporter)
        .setYcqlSpec(any(), any(Boolean.class), any(Boolean.class));
    Mockito.doNothing()
        .when(mockUniverseImporter)
        .setYsqlSpec(any(), any(Boolean.class), any(Boolean.class));
    Mockito.doNothing().when(mockUniverseImporter).setGflagsSpecFromUniverse(any(), any());
    Mockito.doNothing().when(mockUniverseImporter).setDeviceInfoSpecFromUniverse(any(), any());
    Mockito.doNothing()
        .when(mockUniverseImporter)
        .setMasterDeviceInfoSpecFromUniverse(any(), any());
    Mockito.doNothing()
        .when(mockUniverseImporter)
        .setYbcThrottleParametersSpecFromUniverse(any(), any());
    Mockito.doNothing()
        .when(mockUniverseImporter)
        .setKubernetesOverridesSpecFromUniverse(any(), any());

    // Execute method
    operatorUtils.createUniverseCr(testUniverse, providerName, namespace);

    // Verify YBUniverse was created with paused state
    resetMockKubernetesClientForChecking();
    io.yugabyte.operator.v1alpha1.YBUniverse createdUniverse =
        kubernetesClient
            .resources(io.yugabyte.operator.v1alpha1.YBUniverse.class)
            .inNamespace(namespace)
            .withName(testUniverse.getName())
            .get();

    assertNotNull(createdUniverse);
    assertEquals(true, createdUniverse.getSpec().getPaused());
  }
}
