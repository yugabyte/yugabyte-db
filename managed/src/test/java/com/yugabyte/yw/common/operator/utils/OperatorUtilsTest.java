// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.operator.utils;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
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
import com.yugabyte.yw.common.audit.otel.OtelCollectorUtil;
import com.yugabyte.yw.common.backuprestore.ybc.YbcManager;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.export.TelemetryConfig;
import com.yugabyte.yw.common.kms.util.AwsEARServiceUtil.AwsKmsAuthConfigField;
import com.yugabyte.yw.common.kms.util.AzuEARServiceUtil.AzuKmsAuthConfigField;
import com.yugabyte.yw.common.kms.util.CiphertrustEARServiceUtil.CipherTrustKmsAuthConfigField;
import com.yugabyte.yw.common.kms.util.GcpEARServiceUtil.GcpKmsAuthConfigField;
import com.yugabyte.yw.common.kms.util.KeyProvider;
import com.yugabyte.yw.common.kms.util.OciEARServiceUtil.OciKmsAuthConfigField;
import com.yugabyte.yw.common.kms.util.OciEARServiceUtil.OciKmsAuthType;
import com.yugabyte.yw.common.kms.util.hashicorpvault.HashicorpVaultConfigParams;
import com.yugabyte.yw.common.operator.KubernetesResourceDetails;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Backup.BackupState;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.KmsConfig;
import com.yugabyte.yw.models.Release;
import com.yugabyte.yw.models.ReleaseArtifact;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.helpers.MetricCollectionLevel;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.helpers.TimeUnit;
import com.yugabyte.yw.models.helpers.exporters.audit.AuditLogConfig;
import com.yugabyte.yw.models.helpers.exporters.audit.UniverseLogsExporterConfig;
import com.yugabyte.yw.models.helpers.exporters.audit.YSQLAuditConfig;
import com.yugabyte.yw.models.helpers.exporters.metrics.MetricsExportConfig;
import com.yugabyte.yw.models.helpers.exporters.metrics.ScrapeConfigTargetType;
import com.yugabyte.yw.models.helpers.exporters.metrics.UniverseMetricsExporterConfig;
import com.yugabyte.yw.models.helpers.exporters.query.QueryLogConfig;
import com.yugabyte.yw.models.helpers.exporters.query.UniverseQueryLogsExporterConfig;
import com.yugabyte.yw.models.helpers.exporters.query.YSQLQueryLogConfig;
import com.yugabyte.yw.models.helpers.exporters.server.ControllerLogConfig;
import com.yugabyte.yw.models.helpers.exporters.server.MasterLogConfig;
import com.yugabyte.yw.models.helpers.exporters.server.NodeAgentLogConfig;
import com.yugabyte.yw.models.helpers.exporters.server.ServerLogLevel;
import com.yugabyte.yw.models.helpers.exporters.server.TServerLogConfig;
import com.yugabyte.yw.models.helpers.exporters.server.UniverseServerLogsExporterConfig;
import com.yugabyte.yw.models.helpers.exporters.server.YnpLogConfig;
import com.yugabyte.yw.models.helpers.exporters.server.YsqlConnMgrLogConfig;
import com.yugabyte.yw.models.helpers.telemetry.ExportType;
import io.fabric8.kubernetes.api.model.ConfigMap;
import io.fabric8.kubernetes.api.model.KubernetesResourceList;
import io.fabric8.kubernetes.api.model.ObjectMeta;
import io.fabric8.kubernetes.api.model.ObjectMetaBuilder;
import io.fabric8.kubernetes.api.model.Secret;
import io.fabric8.kubernetes.client.Config;
import io.fabric8.kubernetes.client.KubernetesClient;
import io.fabric8.kubernetes.client.KubernetesClientException;
import io.fabric8.kubernetes.client.dsl.MixedOperation;
import io.fabric8.kubernetes.client.dsl.NonNamespaceOperation;
import io.fabric8.kubernetes.client.dsl.Resource;
import io.fabric8.kubernetes.client.informers.SharedIndexInformer;
import io.fabric8.kubernetes.client.server.mock.KubernetesMixedDispatcher;
import io.fabric8.kubernetes.client.server.mock.KubernetesMockServer;
import io.fabric8.kubernetes.client.utils.Serialization;
import io.fabric8.mockwebserver.Context;
import io.fabric8.mockwebserver.ServerRequest;
import io.fabric8.mockwebserver.ServerResponse;
import io.yugabyte.operator.v1alpha1.KMSConfig;
import io.yugabyte.operator.v1alpha1.KMSConfigSpec;
import io.yugabyte.operator.v1alpha1.YBUniverse;
import io.yugabyte.operator.v1alpha1.YBUniverseSpec;
import io.yugabyte.operator.v1alpha1.kmsconfigspec.Aws;
import io.yugabyte.operator.v1alpha1.kmsconfigspec.Azure;
import io.yugabyte.operator.v1alpha1.kmsconfigspec.CipherTrust;
import io.yugabyte.operator.v1alpha1.kmsconfigspec.Gcp;
import io.yugabyte.operator.v1alpha1.kmsconfigspec.Oci;
import io.yugabyte.operator.v1alpha1.kmsconfigspec.Vault;
import io.yugabyte.operator.v1alpha1.kmsconfigspec.aws.AccessKeyIdSecret;
import io.yugabyte.operator.v1alpha1.kmsconfigspec.aws.CmkPolicySecret;
import io.yugabyte.operator.v1alpha1.kmsconfigspec.aws.SecretAccessKeySecret;
import io.yugabyte.operator.v1alpha1.kmsconfigspec.azure.ClientSecretSecret;
import io.yugabyte.operator.v1alpha1.kmsconfigspec.ciphertrust.RefreshTokenSecret;
import io.yugabyte.operator.v1alpha1.kmsconfigspec.ciphertrust.UserCredentials;
import io.yugabyte.operator.v1alpha1.kmsconfigspec.ciphertrust.usercredentials.PasswordSecret;
import io.yugabyte.operator.v1alpha1.kmsconfigspec.gcp.CredentialsSecret;
import io.yugabyte.operator.v1alpha1.kmsconfigspec.oci.PrivateKeySecret;
import io.yugabyte.operator.v1alpha1.kmsconfigspec.vault.AppRole;
import io.yugabyte.operator.v1alpha1.kmsconfigspec.vault.TokenSecret;
import io.yugabyte.operator.v1alpha1.kmsconfigspec.vault.approle.SecretIdSecret;
import io.yugabyte.operator.v1alpha1.ybuniversespec.Telemetry;
import io.yugabyte.operator.v1alpha1.ybuniversespec.telemetry.Metrics;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Base64;
import java.util.EnumMap;
import java.util.EnumSet;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Queue;
import java.util.Set;
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
    spec.put("useTablespaces", true);
    spec.put("useRoles", true);
    spec.put("usePrivileges", false);
    return spec;
  }

  private ObjectNode getIncrementalBackupParamsJson() {
    ObjectNode spec = Json.newObject();
    spec.put("keyspace", "testdb");
    spec.put("backupType", "PGSQL_TABLE_TYPE");
    spec.put("storageConfig", "operator-storage");
    spec.put("universe", "operator-universe");
    spec.put("incrementalBackupBase", "full-backup");
    spec.put("useTablespaces", true);
    spec.put("useRoles", true);
    spec.put("usePrivileges", false);
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
    assertEquals(true, scheduleParams.useTablespaces);
    assertEquals(true, scheduleParams.getUseRoles());
    assertEquals(false, scheduleParams.getUsePrivileges());
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
    assertEquals(true, backupParams.useTablespaces);
    assertEquals(true, backupParams.getUseRoles());
    assertEquals(false, backupParams.getUsePrivileges());
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

  /** Marks the test universe as backed by a YBUniverse custom resource named {@code crName}. */
  private void setUniverseResourceDetails(String crName, String namespace) {
    Universe.saveDetails(
        testUniverse.getUniverseUUID(),
        universe -> {
          UniverseDefinitionTaskParams details = universe.getUniverseDetails();
          details.setKubernetesResourceDetails(new KubernetesResourceDetails(crName, namespace));
          universe.setUniverseDetails(details);
        });
    testUniverse = Universe.getOrBadRequest(testUniverse.getUniverseUUID());
  }

  /**
   * Builds a YBUniverse custom resource. A null {@code ybaResourceId} leaves the resource
   * unannotated; {@code specUniverseName} exercises the legacy naming scheme, which YBA still
   * resolves by but no longer names new universes after.
   */
  private YBUniverse makeYbUniverse(
      String crName, String namespace, String specUniverseName, UUID ybaResourceId) {
    ObjectMetaBuilder metaBuilder =
        new ObjectMetaBuilder()
            .withName(crName)
            .withNamespace(namespace)
            .withUid(UUID.randomUUID().toString());
    if (ybaResourceId != null) {
      metaBuilder.withAnnotations(
          Map.of(ResourceAnnotationKeys.YBA_RESOURCE_ID, ybaResourceId.toString()));
    }
    YBUniverse ybUniverse = new YBUniverse();
    ybUniverse.setMetadata(metaBuilder.build());
    YBUniverseSpec spec = new YBUniverseSpec();
    spec.setUniverseName(specUniverseName);
    ybUniverse.setSpec(spec);
    return ybUniverse;
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
  public void testCreateReleaseCrLocalFileSkipped() throws Exception {
    Release release = Release.create("2025.2.0.0", "LTS");
    com.yugabyte.yw.models.ReleaseLocalFile k8sLocalFile =
        com.yugabyte.yw.models.ReleaseLocalFile.create("/tmp/k8s-artifact.tgz");
    com.yugabyte.yw.models.ReleaseLocalFile x86_64LocalFile =
        com.yugabyte.yw.models.ReleaseLocalFile.create("/tmp/x86_64-artifact.tgz");
    ReleaseArtifact k8sArtifact =
        ReleaseArtifact.create(
            "sha2561234", ReleaseArtifact.Platform.KUBERNETES, null, k8sLocalFile.getFileUUID());
    ReleaseArtifact x86_64Artifact =
        ReleaseArtifact.create(
            "sha256123456",
            ReleaseArtifact.Platform.LINUX,
            PublicCloudConstants.Architecture.x86_64,
            x86_64LocalFile.getFileUUID());
    release.addArtifact(k8sArtifact);
    release.addArtifact(x86_64Artifact);

    boolean imported =
        operatorUtils.createReleaseCr(
            release, k8sArtifact, x86_64Artifact, "namespace", "awsSecret");

    assertFalse(imported);
    resetMockKubernetesClientForChecking();
    KubernetesResourceList<io.yugabyte.operator.v1alpha1.Release> releases =
        kubernetesClient
            .resources(io.yugabyte.operator.v1alpha1.Release.class)
            .inNamespace("namespace")
            .list();
    assertEquals(0, releases.getItems().size());
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
    params.useTablespaces = true;
    params.setUseRoles(true);
    params.setUsePrivileges(false);
    backupSchedule.setTaskParams(Json.toJson(params));
    backupSchedule.save();

    // The custom resource is named differently from the universe, which is the normal case for an
    // operator-created universe: its YBA name carries a hash suffix the resource name lacks.
    setUniverseResourceDetails("operator-universe-cr", "test-namespace");

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
    // spec.universe must be the YBUniverse resource name, since that is what
    // getUniverseFromNameAndNamespace looks the resource up by.
    assertEquals(spec.getUniverse(), "operator-universe-cr");
    assertEquals("PGSQL_TABLE_TYPE", spec.getBackupType().toString());
    assertEquals("foo", spec.getKeyspace()); // From ModelFactory.createScheduleBackup
    assertEquals(
        3600L * 60 * 1000,
        spec.getSchedulingFrequency().longValue()); // From ModelFactory.createScheduleBackup
    assertEquals(true, spec.getUseTablespaces());
    assertEquals(true, spec.getUseRoles());
    assertEquals(false, spec.getUsePrivileges());
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
    setUniverseResourceDetails("operator-universe-cr", "test-namespace");

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
    Mockito.doNothing().when(mockUniverseImporter).setTserverVolumeSpecFromUniverse(any(), any());
    Mockito.doNothing().when(mockUniverseImporter).setMasterVolumeSpecFromUniverse(any(), any());
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
    verify(mockUniverseImporter).setTserverVolumeSpecFromUniverse(any(), eq(testUniverse));
    verify(mockUniverseImporter).setMasterVolumeSpecFromUniverse(any(), eq(testUniverse));
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
    Mockito.doNothing().when(mockUniverseImporter).setTserverVolumeSpecFromUniverse(any(), any());
    Mockito.doNothing().when(mockUniverseImporter).setMasterVolumeSpecFromUniverse(any(), any());
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
    Mockito.doNothing().when(mockUniverseImporter).setTserverVolumeSpecFromUniverse(any(), any());
    Mockito.doNothing().when(mockUniverseImporter).setMasterVolumeSpecFromUniverse(any(), any());
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
    Mockito.doNothing().when(mockUniverseImporter).setTserverVolumeSpecFromUniverse(any(), any());
    Mockito.doNothing().when(mockUniverseImporter).setMasterVolumeSpecFromUniverse(any(), any());
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
    Mockito.doNothing().when(mockUniverseImporter).setTserverVolumeSpecFromUniverse(any(), any());
    Mockito.doNothing().when(mockUniverseImporter).setMasterVolumeSpecFromUniverse(any(), any());
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

  /*--- maybeAddYbaResourceId tests ---*/

  @SuppressWarnings("unchecked")
  private MixedOperation<ConfigMap, KubernetesResourceList<ConfigMap>, Resource<ConfigMap>>
      createMockResourceClient(Resource<ConfigMap> mockResource) {
    MixedOperation<ConfigMap, KubernetesResourceList<ConfigMap>, Resource<ConfigMap>>
        mockResourceClient = Mockito.mock(MixedOperation.class);
    NonNamespaceOperation<ConfigMap, KubernetesResourceList<ConfigMap>, Resource<ConfigMap>>
        mockInNamespace = Mockito.mock(NonNamespaceOperation.class);
    when(mockResourceClient.inNamespace(anyString())).thenReturn(mockInNamespace);
    when(mockInNamespace.withName(anyString())).thenReturn(mockResource);
    return mockResourceClient;
  }

  private ConfigMap createConfigMapWithAnnotations(Map<String, String> annotations) {
    ConfigMap configMap = new ConfigMap();
    ObjectMeta meta = new ObjectMeta();
    meta.setName("test-resource");
    meta.setNamespace("test-ns");
    if (annotations != null) {
      meta.setAnnotations(new HashMap<>(annotations));
    }
    configMap.setMetadata(meta);
    return configMap;
  }

  @Test
  @SuppressWarnings("unchecked")
  public void testMaybeAddYbaResourceIdSuccess() {
    ConfigMap configMap = createConfigMapWithAnnotations(null);
    UUID resourceId = UUID.randomUUID();
    Resource<ConfigMap> mockResource = Mockito.mock(Resource.class);
    when(mockResource.edit(any(java.util.function.UnaryOperator.class)))
        .thenAnswer(
            inv -> {
              java.util.function.UnaryOperator<ConfigMap> editor = inv.getArgument(0);
              return editor.apply(createConfigMapWithAnnotations(null));
            });

    MixedOperation<ConfigMap, KubernetesResourceList<ConfigMap>, Resource<ConfigMap>>
        mockResourceClient = createMockResourceClient(mockResource);

    OperatorUtils.maybeAddYbaResourceId(configMap, resourceId, mockResourceClient);

    verify(mockResource).edit(any(java.util.function.UnaryOperator.class));
  }

  @Test
  @SuppressWarnings("unchecked")
  public void testMaybeAddYbaResourceIdAlreadyAnnotatedLocally() {
    Map<String, String> annotations = new HashMap<>();
    UUID existingId = UUID.randomUUID();
    annotations.put(ResourceAnnotationKeys.YBA_RESOURCE_ID, existingId.toString());
    ConfigMap configMap = createConfigMapWithAnnotations(annotations);
    UUID newResourceId = UUID.randomUUID();
    Resource<ConfigMap> mockResource = Mockito.mock(Resource.class);

    MixedOperation<ConfigMap, KubernetesResourceList<ConfigMap>, Resource<ConfigMap>>
        mockResourceClient = createMockResourceClient(mockResource);

    OperatorUtils.maybeAddYbaResourceId(configMap, newResourceId, mockResourceClient);

    verify(mockResource, Mockito.never()).edit(any(java.util.function.UnaryOperator.class));
  }

  @Test
  @SuppressWarnings("unchecked")
  public void testMaybeAddYbaResourceIdAlreadyAnnotatedOnServer() {
    ConfigMap configMap = createConfigMapWithAnnotations(null);
    UUID resourceId = UUID.randomUUID();
    Resource<ConfigMap> mockResource = Mockito.mock(Resource.class);

    Map<String, String> serverAnnotations = new HashMap<>();
    serverAnnotations.put(ResourceAnnotationKeys.YBA_RESOURCE_ID, UUID.randomUUID().toString());

    when(mockResource.edit(any(java.util.function.UnaryOperator.class)))
        .thenAnswer(
            inv -> {
              java.util.function.UnaryOperator<ConfigMap> editor = inv.getArgument(0);
              ConfigMap result = editor.apply(createConfigMapWithAnnotations(serverAnnotations));
              assertEquals(
                  "Server annotation should not be overwritten",
                  serverAnnotations.get(ResourceAnnotationKeys.YBA_RESOURCE_ID),
                  result
                      .getMetadata()
                      .getAnnotations()
                      .get(ResourceAnnotationKeys.YBA_RESOURCE_ID));
              return result;
            });

    MixedOperation<ConfigMap, KubernetesResourceList<ConfigMap>, Resource<ConfigMap>>
        mockResourceClient = createMockResourceClient(mockResource);

    OperatorUtils.maybeAddYbaResourceId(configMap, resourceId, mockResourceClient);
  }

  @Test
  @SuppressWarnings("unchecked")
  public void testMaybeAddYbaResourceIdNullMetadataThrows() {
    ConfigMap configMap = new ConfigMap();
    UUID resourceId = UUID.randomUUID();
    Resource<ConfigMap> mockResource = Mockito.mock(Resource.class);

    MixedOperation<ConfigMap, KubernetesResourceList<ConfigMap>, Resource<ConfigMap>>
        mockResourceClient = createMockResourceClient(mockResource);

    RuntimeException ex =
        assertThrows(
            RuntimeException.class,
            () -> OperatorUtils.maybeAddYbaResourceId(configMap, resourceId, mockResourceClient));
    assertTrue(ex.getMessage().contains("Metadata is null"));
  }

  @Test
  @SuppressWarnings("unchecked")
  public void testMaybeAddYbaResourceIdKubernetesExceptionDoesNotThrow() {
    ConfigMap configMap = createConfigMapWithAnnotations(null);
    UUID resourceId = UUID.randomUUID();
    Resource<ConfigMap> mockResource = Mockito.mock(Resource.class);

    when(mockResource.edit(any(java.util.function.UnaryOperator.class)))
        .thenThrow(new KubernetesClientException("Not Found", 404, null));

    MixedOperation<ConfigMap, KubernetesResourceList<ConfigMap>, Resource<ConfigMap>>
        mockResourceClient = createMockResourceClient(mockResource);

    OperatorUtils.maybeAddYbaResourceId(configMap, resourceId, mockResourceClient);
  }

  @Test
  @SuppressWarnings("unchecked")
  public void testMaybeAddYbaResourceIdWithExistingAnnotationsPreservesThem() {
    Map<String, String> existingAnnotations = new HashMap<>();
    existingAnnotations.put("some-other-key", "some-value");
    ConfigMap configMap = createConfigMapWithAnnotations(existingAnnotations);
    UUID resourceId = UUID.randomUUID();
    Resource<ConfigMap> mockResource = Mockito.mock(Resource.class);
    when(mockResource.edit(any(java.util.function.UnaryOperator.class)))
        .thenAnswer(
            inv -> {
              java.util.function.UnaryOperator<ConfigMap> editor = inv.getArgument(0);
              ConfigMap result =
                  editor.apply(
                      createConfigMapWithAnnotations(
                          new HashMap<>(Map.of("some-other-key", "some-value"))));
              Map<String, String> resultAnnotations = result.getMetadata().getAnnotations();
              assertEquals(
                  resourceId.toString(),
                  resultAnnotations.get(ResourceAnnotationKeys.YBA_RESOURCE_ID));
              assertEquals("some-value", resultAnnotations.get("some-other-key"));
              return result;
            });

    MixedOperation<ConfigMap, KubernetesResourceList<ConfigMap>, Resource<ConfigMap>>
        mockResourceClient = createMockResourceClient(mockResource);

    OperatorUtils.maybeAddYbaResourceId(configMap, resourceId, mockResourceClient);
  }

  /*--- getKMSConfigFormDataFromCr (Hashicorp Vault) tests ---*/

  private KMSConfig baseKmsConfigCr(KMSConfigSpec.Provider provider) {
    KMSConfig kmsConfig = new KMSConfig();
    ObjectMeta metadata = new ObjectMeta();
    metadata.setName("vault-kms");
    metadata.setNamespace("test-namespace");
    kmsConfig.setMetadata(metadata);
    KMSConfigSpec spec = new KMSConfigSpec();
    spec.setName("vault-kms-config");
    spec.setProvider(provider);
    kmsConfig.setSpec(spec);
    return kmsConfig;
  }

  private static String field(ObjectNode node, String key) {
    return node.get(key).asText();
  }

  @Test
  public void testGetKMSConfigFormDataHashicorpToken() {
    KMSConfig kmsConfig = baseKmsConfigCr(KMSConfigSpec.Provider.HASHICORP);
    Vault vault = new Vault();
    vault.setAddress("http://vault:8200");
    vault.setAuthType(Vault.AuthType.TOKEN);
    TokenSecret tokenSecret = new TokenSecret();
    tokenSecret.setName("vault-token");
    tokenSecret.setKey("token");
    vault.setTokenSecret(tokenSecret);
    kmsConfig.getSpec().setVault(vault);

    doReturn(new Secret()).when(operatorUtils).getSecret(anyString(), nullable(String.class));
    doReturn("root-token").when(operatorUtils).parseSecretForKey(any(Secret.class), anyString());

    ObjectNode formData = operatorUtils.getKMSConfigFormDataFromCr(kmsConfig);

    assertEquals("vault-kms-config", formData.get("name").asText());
    assertEquals("http://vault:8200", field(formData, HashicorpVaultConfigParams.HC_VAULT_ADDRESS));
    assertEquals("root-token", field(formData, HashicorpVaultConfigParams.HC_VAULT_TOKEN));
    assertEquals("transit/", field(formData, HashicorpVaultConfigParams.HC_VAULT_MOUNT_PATH));
    assertEquals("transit", field(formData, HashicorpVaultConfigParams.HC_VAULT_ENGINE));
    assertEquals("key_yugabyte", field(formData, HashicorpVaultConfigParams.HC_VAULT_KEY_NAME));
    // TOKEN auth must not carry AppRole/authNamespace fields.
    assertFalse(formData.has(HashicorpVaultConfigParams.HC_VAULT_ROLE_ID));
    assertFalse(formData.has(HashicorpVaultConfigParams.HC_VAULT_SECRET_ID));
    assertFalse(formData.has(HashicorpVaultConfigParams.HC_VAULT_AUTH_NAMESPACE));
  }

  @Test
  public void testGetKMSConfigFormDataHashicorpAppRolePrefixesMountPath() {
    KMSConfig kmsConfig = baseKmsConfigCr(KMSConfigSpec.Provider.HASHICORP);
    Vault vault = new Vault();
    vault.setAddress("http://vault:8200");
    vault.setAuthType(Vault.AuthType.APPROLE);
    vault.setAuthNamespace("admin");
    AppRole appRole = new AppRole();
    appRole.setRoleID("role-id");
    SecretIdSecret secretIdSecret = new SecretIdSecret();
    secretIdSecret.setName("vault-approle-secret-id");
    secretIdSecret.setKey("secret-id");
    appRole.setSecretIdSecret(secretIdSecret);
    vault.setAppRole(appRole);
    kmsConfig.getSpec().setVault(vault);

    doReturn(new Secret()).when(operatorUtils).getSecret(anyString(), nullable(String.class));
    doReturn("secret-id-value")
        .when(operatorUtils)
        .parseSecretForKey(any(Secret.class), anyString());

    ObjectNode formData = operatorUtils.getKMSConfigFormDataFromCr(kmsConfig);

    assertEquals("role-id", field(formData, HashicorpVaultConfigParams.HC_VAULT_ROLE_ID));
    assertEquals("secret-id-value", field(formData, HashicorpVaultConfigParams.HC_VAULT_SECRET_ID));
    assertEquals("admin", field(formData, HashicorpVaultConfigParams.HC_VAULT_AUTH_NAMESPACE));
    // The default mount path is prefixed with the auth namespace for APPROLE.
    assertEquals("admin/transit/", field(formData, HashicorpVaultConfigParams.HC_VAULT_MOUNT_PATH));
    // APPROLE auth must not carry a token.
    assertFalse(formData.has(HashicorpVaultConfigParams.HC_VAULT_TOKEN));
  }

  @Test
  public void testGetKMSConfigFormDataAwsStaticCredentials() {
    KMSConfig kmsConfig = baseKmsConfigCr(KMSConfigSpec.Provider.AWS);
    Aws aws = new Aws();
    aws.setRegion("us-west-2");
    aws.setCmkID("cmk-1234");
    aws.setEndpoint("https://kms.us-west-2.amazonaws.com");
    AccessKeyIdSecret accessKeyIdSecret = new AccessKeyIdSecret();
    accessKeyIdSecret.setName("aws-access-key");
    accessKeyIdSecret.setKey("access-key-id");
    aws.setAccessKeyIdSecret(accessKeyIdSecret);
    SecretAccessKeySecret secretAccessKeySecret = new SecretAccessKeySecret();
    secretAccessKeySecret.setName("aws-secret-key");
    secretAccessKeySecret.setKey("secret-access-key");
    aws.setSecretAccessKeySecret(secretAccessKeySecret);
    kmsConfig.getSpec().setAws(aws);

    doReturn(new Secret()).when(operatorUtils).getSecret(anyString(), nullable(String.class));
    doReturn("resolved-secret")
        .when(operatorUtils)
        .parseSecretForKey(any(Secret.class), anyString());

    ObjectNode formData = operatorUtils.getKMSConfigFormDataFromCr(kmsConfig);

    assertEquals("us-west-2", field(formData, AwsKmsAuthConfigField.REGION.fieldName));
    assertEquals("resolved-secret", field(formData, AwsKmsAuthConfigField.ACCESS_KEY_ID.fieldName));
    assertEquals(
        "resolved-secret", field(formData, AwsKmsAuthConfigField.SECRET_ACCESS_KEY.fieldName));
    assertEquals("cmk-1234", field(formData, AwsKmsAuthConfigField.CMK_ID.fieldName));
    assertEquals(
        "https://kms.us-west-2.amazonaws.com",
        field(formData, AwsKmsAuthConfigField.ENDPOINT.fieldName));
  }

  @Test
  public void testGetKMSConfigFormDataAwsCmkPolicy() {
    KMSConfig kmsConfig = baseKmsConfigCr(KMSConfigSpec.Provider.AWS);
    Aws aws = new Aws();
    aws.setRegion("us-west-2");
    aws.setUseIAMProfile(true);
    CmkPolicySecret cmkPolicySecret = new CmkPolicySecret();
    cmkPolicySecret.setName("cmk-policy");
    cmkPolicySecret.setKey("policy.json");
    aws.setCmkPolicySecret(cmkPolicySecret);
    kmsConfig.getSpec().setAws(aws);

    doReturn(new Secret()).when(operatorUtils).getSecret(anyString(), nullable(String.class));
    doReturn("{\"Version\":\"2012-10-17\"}")
        .when(operatorUtils)
        .parseSecretForKey(any(Secret.class), anyString());

    ObjectNode formData = operatorUtils.getKMSConfigFormDataFromCr(kmsConfig);

    assertEquals(
        "{\"Version\":\"2012-10-17\"}",
        field(formData, AwsKmsAuthConfigField.CMK_POLICY.fieldName));
  }

  @Test
  public void testGetKMSConfigFormDataAwsIamProfileOmitsCredentials() {
    KMSConfig kmsConfig = baseKmsConfigCr(KMSConfigSpec.Provider.AWS);
    Aws aws = new Aws();
    aws.setRegion("us-east-1");
    aws.setUseIAMProfile(true);
    kmsConfig.getSpec().setAws(aws);

    ObjectNode formData = operatorUtils.getKMSConfigFormDataFromCr(kmsConfig);

    assertEquals("us-east-1", field(formData, AwsKmsAuthConfigField.REGION.fieldName));
    // With the host IAM profile no static credentials are emitted; the backend falls back to the
    // default AWS credential chain.
    assertFalse(formData.has(AwsKmsAuthConfigField.ACCESS_KEY_ID.fieldName));
    assertFalse(formData.has(AwsKmsAuthConfigField.SECRET_ACCESS_KEY.fieldName));
    // cmkID, endpoint and cmkPolicy are optional and were not set.
    assertFalse(formData.has(AwsKmsAuthConfigField.CMK_ID.fieldName));
    assertFalse(formData.has(AwsKmsAuthConfigField.ENDPOINT.fieldName));
    assertFalse(formData.has(AwsKmsAuthConfigField.CMK_POLICY.fieldName));
  }

  @Test
  public void testGetKMSConfigFormDataGcpWithCredentials() {
    KMSConfig kmsConfig = baseKmsConfigCr(KMSConfigSpec.Provider.GCP);
    Gcp gcp = new Gcp();
    gcp.setLocation("us-east1");
    gcp.setKeyRingName("yb-key-ring");
    gcp.setCryptoKeyName("yb-crypto-key");
    gcp.setProtectionLevel(Gcp.ProtectionLevel.HSM);
    gcp.setEndpoint("https://cloudkms.googleapis.com");
    CredentialsSecret credentialsSecret = new CredentialsSecret();
    credentialsSecret.setName("gcp-creds");
    credentialsSecret.setKey("credentials.json");
    gcp.setCredentialsSecret(credentialsSecret);
    kmsConfig.getSpec().setGcp(gcp);

    doReturn(new Secret()).when(operatorUtils).getSecret(anyString(), nullable(String.class));
    doReturn("{\"type\":\"service_account\",\"project_id\":\"my-project\"}")
        .when(operatorUtils)
        .parseSecretForKey(any(Secret.class), anyString());

    ObjectNode formData = operatorUtils.getKMSConfigFormDataFromCr(kmsConfig);

    assertEquals("us-east1", field(formData, GcpKmsAuthConfigField.LOCATION_ID.fieldName));
    assertEquals("yb-key-ring", field(formData, GcpKmsAuthConfigField.KEY_RING_ID.fieldName));
    assertEquals("yb-crypto-key", field(formData, GcpKmsAuthConfigField.CRYPTO_KEY_ID.fieldName));
    assertEquals("HSM", field(formData, GcpKmsAuthConfigField.PROTECTION_LEVEL.fieldName));
    assertEquals(
        "https://cloudkms.googleapis.com",
        field(formData, GcpKmsAuthConfigField.GCP_KMS_ENDPOINT.fieldName));
    // The credentials JSON is stored as a nested object, not a string.
    JsonNode gcpConfig = formData.get(GcpKmsAuthConfigField.GCP_CONFIG.fieldName);
    assertTrue(gcpConfig.isObject());
    assertEquals("my-project", gcpConfig.get("project_id").asText());
  }

  @Test
  public void testGetKMSConfigFormDataGcpMissingCredentialsThrows() {
    KMSConfig kmsConfig = baseKmsConfigCr(KMSConfigSpec.Provider.GCP);
    Gcp gcp = new Gcp();
    gcp.setKeyRingName("yb-key-ring");
    gcp.setCryptoKeyName("yb-crypto-key");
    kmsConfig.getSpec().setGcp(gcp);

    // credentialsSecret is required for GCP (the project ID is read from the credentials JSON).
    assertThrows(RuntimeException.class, () -> operatorUtils.getKMSConfigFormDataFromCr(kmsConfig));
  }

  @Test
  public void testGetKMSConfigFormDataAzureServicePrincipal() {
    KMSConfig kmsConfig = baseKmsConfigCr(KMSConfigSpec.Provider.AZU);
    Azure azure = new Azure();
    azure.setClientID("client-id");
    azure.setTenantID("tenant-id");
    azure.setKeyVaultURL("https://myvault.vault.azure.net/");
    azure.setKeyName("yb-key");
    azure.setKeySize(3072L);
    ClientSecretSecret clientSecretSecret = new ClientSecretSecret();
    clientSecretSecret.setName("azure-client-secret");
    clientSecretSecret.setKey("client-secret");
    azure.setClientSecretSecret(clientSecretSecret);
    kmsConfig.getSpec().setAzure(azure);

    doReturn(new Secret()).when(operatorUtils).getSecret(anyString(), nullable(String.class));
    doReturn("client-secret-value")
        .when(operatorUtils)
        .parseSecretForKey(any(Secret.class), anyString());

    ObjectNode formData = operatorUtils.getKMSConfigFormDataFromCr(kmsConfig);

    assertEquals("client-id", field(formData, AzuKmsAuthConfigField.CLIENT_ID.fieldName));
    assertEquals("tenant-id", field(formData, AzuKmsAuthConfigField.TENANT_ID.fieldName));
    assertEquals(
        "https://myvault.vault.azure.net/",
        field(formData, AzuKmsAuthConfigField.AZU_VAULT_URL.fieldName));
    assertEquals("yb-key", field(formData, AzuKmsAuthConfigField.AZU_KEY_NAME.fieldName));
    assertEquals("RSA", field(formData, AzuKmsAuthConfigField.AZU_KEY_ALGORITHM.fieldName));
    assertEquals("3072", field(formData, AzuKmsAuthConfigField.AZU_KEY_SIZE.fieldName));
    assertEquals(
        "client-secret-value", field(formData, AzuKmsAuthConfigField.CLIENT_SECRET.fieldName));
  }

  @Test
  public void testGetKMSConfigFormDataAzureManagedIdentityOmitsSecret() {
    KMSConfig kmsConfig = baseKmsConfigCr(KMSConfigSpec.Provider.AZU);
    Azure azure = new Azure();
    azure.setClientID("managed-identity-client-id");
    azure.setTenantID("tenant-id");
    azure.setKeyVaultURL("https://myvault.vault.azure.net/");
    azure.setKeyName("yb-key");
    azure.setUseManagedIdentity(true);
    kmsConfig.getSpec().setAzure(azure);

    ObjectNode formData = operatorUtils.getKMSConfigFormDataFromCr(kmsConfig);

    // clientID and tenantID are always required, even with managed identity.
    assertEquals(
        "managed-identity-client-id", field(formData, AzuKmsAuthConfigField.CLIENT_ID.fieldName));
    assertEquals("tenant-id", field(formData, AzuKmsAuthConfigField.TENANT_ID.fieldName));
    // Managed identity omits the client secret; defaults are applied.
    assertFalse(formData.has(AzuKmsAuthConfigField.CLIENT_SECRET.fieldName));
    assertEquals("RSA", field(formData, AzuKmsAuthConfigField.AZU_KEY_ALGORITHM.fieldName));
    assertEquals("2048", field(formData, AzuKmsAuthConfigField.AZU_KEY_SIZE.fieldName));
  }

  @Test
  public void testGetKMSConfigFormDataCiphertrustUserCredentials() {
    KMSConfig kmsConfig = baseKmsConfigCr(KMSConfigSpec.Provider.CIPHERTRUST);
    CipherTrust cipherTrust = new CipherTrust();
    cipherTrust.setManagerURL("https://web.ciphertrustmanager.local");
    cipherTrust.setKeyName("yb-key");
    cipherTrust.setKeySize(256L);
    cipherTrust.setAuthType(CipherTrust.AuthType.USER_CREDENTIALS);
    UserCredentials userCredentials = new UserCredentials();
    userCredentials.setUsername("admin");
    PasswordSecret passwordSecret = new PasswordSecret();
    passwordSecret.setName("ciphertrust-password");
    passwordSecret.setKey("password");
    userCredentials.setPasswordSecret(passwordSecret);
    cipherTrust.setUserCredentials(userCredentials);
    kmsConfig.getSpec().setCipherTrust(cipherTrust);

    doReturn(new Secret()).when(operatorUtils).getSecret(anyString(), nullable(String.class));
    doReturn("password-value")
        .when(operatorUtils)
        .parseSecretForKey(any(Secret.class), anyString());

    ObjectNode formData = operatorUtils.getKMSConfigFormDataFromCr(kmsConfig);

    assertEquals(
        "https://web.ciphertrustmanager.local",
        field(formData, CipherTrustKmsAuthConfigField.CIPHERTRUST_MANAGER_URL.fieldName));
    assertEquals("yb-key", field(formData, CipherTrustKmsAuthConfigField.KEY_NAME.fieldName));
    assertEquals("AES", field(formData, CipherTrustKmsAuthConfigField.KEY_ALGORITHM.fieldName));
    assertEquals("256", field(formData, CipherTrustKmsAuthConfigField.KEY_SIZE.fieldName));
    // USER_CREDENTIALS maps to the backend PASSWORD auth type.
    assertEquals("PASSWORD", field(formData, CipherTrustKmsAuthConfigField.AUTH_TYPE.fieldName));
    assertEquals("admin", field(formData, CipherTrustKmsAuthConfigField.USERNAME.fieldName));
    assertEquals(
        "password-value", field(formData, CipherTrustKmsAuthConfigField.PASSWORD.fieldName));
    assertFalse(formData.has(CipherTrustKmsAuthConfigField.REFRESH_TOKEN.fieldName));
  }

  @Test
  public void testGetKMSConfigFormDataCiphertrustRefreshToken() {
    KMSConfig kmsConfig = baseKmsConfigCr(KMSConfigSpec.Provider.CIPHERTRUST);
    CipherTrust cipherTrust = new CipherTrust();
    cipherTrust.setManagerURL("https://web.ciphertrustmanager.local");
    cipherTrust.setKeyName("yb-key");
    cipherTrust.setAuthType(CipherTrust.AuthType.REFRESH_TOKEN);
    RefreshTokenSecret refreshTokenSecret = new RefreshTokenSecret();
    refreshTokenSecret.setName("ciphertrust-refresh-token");
    refreshTokenSecret.setKey("refresh-token");
    cipherTrust.setRefreshTokenSecret(refreshTokenSecret);
    kmsConfig.getSpec().setCipherTrust(cipherTrust);

    doReturn(new Secret()).when(operatorUtils).getSecret(anyString(), nullable(String.class));
    doReturn("refresh-token-value")
        .when(operatorUtils)
        .parseSecretForKey(any(Secret.class), anyString());

    ObjectNode formData = operatorUtils.getKMSConfigFormDataFromCr(kmsConfig);

    assertEquals(
        "REFRESH_TOKEN", field(formData, CipherTrustKmsAuthConfigField.AUTH_TYPE.fieldName));
    assertEquals(
        "refresh-token-value",
        field(formData, CipherTrustKmsAuthConfigField.REFRESH_TOKEN.fieldName));
    // Defaults are applied and the user-credentials fields are absent.
    assertEquals("256", field(formData, CipherTrustKmsAuthConfigField.KEY_SIZE.fieldName));
    assertFalse(formData.has(CipherTrustKmsAuthConfigField.USERNAME.fieldName));
    assertFalse(formData.has(CipherTrustKmsAuthConfigField.PASSWORD.fieldName));
  }

  @Test
  public void testGetKMSConfigFormDataOci() {
    KMSConfig kmsConfig = baseKmsConfigCr(KMSConfigSpec.Provider.OCI);
    Oci oci = new Oci();
    oci.setUserOCID("ocid1.user.oc1..user");
    oci.setTenancyOCID("ocid1.tenancy.oc1..tenancy");
    oci.setFingerprint("20:3b:97:13:55:1c");
    oci.setRegion("us-ashburn-1");
    oci.setCompartmentOCID("ocid1.compartment.oc1..comp");
    oci.setVaultOCID("ocid1.vault.oc1..vault");
    PrivateKeySecret privateKeySecret = new PrivateKeySecret();
    privateKeySecret.setName("oci-private-key");
    privateKeySecret.setKey("private-key.pem");
    oci.setPrivateKeySecret(privateKeySecret);
    kmsConfig.getSpec().setOci(oci);

    doReturn(new Secret()).when(operatorUtils).getSecret(anyString(), nullable(String.class));
    doReturn("-----BEGIN PRIVATE KEY-----")
        .when(operatorUtils)
        .parseSecretForKey(any(Secret.class), anyString());

    ObjectNode formData = operatorUtils.getKMSConfigFormDataFromCr(kmsConfig);

    assertEquals(
        "ocid1.user.oc1..user", field(formData, OciKmsAuthConfigField.ociUserId.fieldName));
    assertEquals(
        "ocid1.tenancy.oc1..tenancy",
        field(formData, OciKmsAuthConfigField.ociTenancyId.fieldName));
    assertEquals(
        "20:3b:97:13:55:1c", field(formData, OciKmsAuthConfigField.ociFingerprint.fieldName));
    assertEquals("us-ashburn-1", field(formData, OciKmsAuthConfigField.ociRegion.fieldName));
    assertEquals(
        "ocid1.compartment.oc1..comp",
        field(formData, OciKmsAuthConfigField.ociCompartmentId.fieldName));
    assertEquals(
        "ocid1.vault.oc1..vault", field(formData, OciKmsAuthConfigField.ociVaultId.fieldName));
    // keyName was not set, so the CR default is applied.
    assertEquals("yba-master-key", field(formData, OciKmsAuthConfigField.ociKeyName.fieldName));
    assertEquals(
        "-----BEGIN PRIVATE KEY-----",
        field(formData, OciKmsAuthConfigField.ociPrivateKeyContent.fieldName));
    // keyOCID not set -> absent (YBA creates the key with keyName).
    assertFalse(formData.has(OciKmsAuthConfigField.ociKeyOcid.fieldName));
  }

  @Test
  public void testGetKMSConfigFormDataOciMissingPrivateKeyThrows() {
    KMSConfig kmsConfig = baseKmsConfigCr(KMSConfigSpec.Provider.OCI);
    Oci oci = new Oci();
    oci.setUserOCID("ocid1.user.oc1..user");
    oci.setTenancyOCID("ocid1.tenancy.oc1..tenancy");
    oci.setFingerprint("20:3b:97:13:55:1c");
    oci.setRegion("us-ashburn-1");
    oci.setCompartmentOCID("ocid1.compartment.oc1..comp");
    oci.setVaultOCID("ocid1.vault.oc1..vault");
    kmsConfig.getSpec().setOci(oci);

    // The API signing key is required to authenticate against OCI KMS.
    assertThrows(RuntimeException.class, () -> operatorUtils.getKMSConfigFormDataFromCr(kmsConfig));
  }

  @Test
  public void testGetKMSConfigFormDataOciApiKeyOmitsAuthType() {
    KMSConfig kmsConfig = baseKmsConfigCr(KMSConfigSpec.Provider.OCI);
    Oci oci = new Oci();
    oci.setUserOCID("ocid1.user.oc1..user");
    oci.setTenancyOCID("ocid1.tenancy.oc1..tenancy");
    oci.setFingerprint("20:3b:97:13:55:1c");
    oci.setRegion("us-ashburn-1");
    oci.setCompartmentOCID("ocid1.compartment.oc1..comp");
    oci.setVaultOCID("ocid1.vault.oc1..vault");
    PrivateKeySecret privateKeySecret = new PrivateKeySecret();
    privateKeySecret.setName("oci-private-key");
    privateKeySecret.setKey("private-key.pem");
    oci.setPrivateKeySecret(privateKeySecret);
    kmsConfig.getSpec().setOci(oci);

    doReturn(new Secret()).when(operatorUtils).getSecret(anyString(), nullable(String.class));
    doReturn("-----BEGIN PRIVATE KEY-----")
        .when(operatorUtils)
        .parseSecretForKey(any(Secret.class), anyString());

    ObjectNode formData = operatorUtils.getKMSConfigFormDataFromCr(kmsConfig);

    // API_KEY is the backend default when ociAuthType is blank. Leaving it unset keeps the auth
    // config byte-identical to one produced before instance-principal support existed, so the
    // reconciler does not see pre-existing configs as changed.
    assertFalse(formData.has(OciKmsAuthConfigField.ociAuthType.fieldName));
  }

  @Test
  public void testGetKMSConfigFormDataOciInstancePrincipalOmitsCredentials() {
    KMSConfig kmsConfig = baseKmsConfigCr(KMSConfigSpec.Provider.OCI);
    Oci oci = new Oci();
    oci.setUseInstancePrincipal(true);
    oci.setRegion("us-ashburn-1");
    oci.setCompartmentOCID("ocid1.compartment.oc1..comp");
    oci.setVaultOCID("ocid1.vault.oc1..vault");
    oci.setKeyName("yb-operator-oci-key");
    kmsConfig.getSpec().setOci(oci);

    ObjectNode formData = operatorUtils.getKMSConfigFormDataFromCr(kmsConfig);

    assertEquals(
        OciKmsAuthType.INSTANCE_PRINCIPAL.name(),
        field(formData, OciKmsAuthConfigField.ociAuthType.fieldName));
    assertEquals("us-ashburn-1", field(formData, OciKmsAuthConfigField.ociRegion.fieldName));
    assertEquals(
        "ocid1.compartment.oc1..comp",
        field(formData, OciKmsAuthConfigField.ociCompartmentId.fieldName));
    assertEquals(
        "ocid1.vault.oc1..vault", field(formData, OciKmsAuthConfigField.ociVaultId.fieldName));
    assertEquals(
        "yb-operator-oci-key", field(formData, OciKmsAuthConfigField.ociKeyName.fieldName));
    // The instance principal is resolved from the host metadata service, so none of the API-key
    // credentials are emitted.
    assertFalse(formData.has(OciKmsAuthConfigField.ociUserId.fieldName));
    assertFalse(formData.has(OciKmsAuthConfigField.ociTenancyId.fieldName));
    assertFalse(formData.has(OciKmsAuthConfigField.ociFingerprint.fieldName));
    assertFalse(formData.has(OciKmsAuthConfigField.ociPrivateKeyContent.fieldName));
  }

  @Test
  public void testGetKMSConfigFormDataOciInstancePrincipalIgnoresPrivateKeySecret() {
    KMSConfig kmsConfig = baseKmsConfigCr(KMSConfigSpec.Provider.OCI);
    Oci oci = new Oci();
    oci.setUseInstancePrincipal(true);
    oci.setRegion("us-ashburn-1");
    oci.setCompartmentOCID("ocid1.compartment.oc1..comp");
    oci.setVaultOCID("ocid1.vault.oc1..vault");
    PrivateKeySecret privateKeySecret = new PrivateKeySecret();
    privateKeySecret.setName("oci-private-key");
    privateKeySecret.setKey("private-key.pem");
    oci.setPrivateKeySecret(privateKeySecret);
    kmsConfig.getSpec().setOci(oci);

    ObjectNode formData = operatorUtils.getKMSConfigFormDataFromCr(kmsConfig);

    // A leftover privateKeySecret is not read at all, so no Secret lookup is attempted.
    verify(operatorUtils, Mockito.never()).getSecret(anyString(), nullable(String.class));
    assertFalse(formData.has(OciKmsAuthConfigField.ociPrivateKeyContent.fieldName));
  }

  @Test
  public void testGetKMSConfigFormDataOciInstancePrincipalWithExistingKey() {
    KMSConfig kmsConfig = baseKmsConfigCr(KMSConfigSpec.Provider.OCI);
    Oci oci = new Oci();
    oci.setUseInstancePrincipal(true);
    oci.setRegion("us-sanjose-1");
    oci.setCompartmentOCID("ocid1.compartment.oc1..comp");
    oci.setVaultOCID("ocid1.vault.oc1.us-sanjose-1.vault");
    oci.setKeyName("yb-operator-oci-key");
    oci.setKeyOCID("ocid1.key.oc1.us-sanjose-1.key");
    kmsConfig.getSpec().setOci(oci);

    ObjectNode formData = operatorUtils.getKMSConfigFormDataFromCr(kmsConfig);

    // keyOCID is orthogonal to the auth type and must still be passed through.
    assertEquals(
        "ocid1.key.oc1.us-sanjose-1.key",
        field(formData, OciKmsAuthConfigField.ociKeyOcid.fieldName));
    assertEquals(
        OciKmsAuthType.INSTANCE_PRINCIPAL.name(),
        field(formData, OciKmsAuthConfigField.ociAuthType.fieldName));
  }

  /*--- Telemetry export: section comparison (telemetrySectionDiffers) ---*/

  // Internal-model fixtures, shaped the way the config is actually persisted: every section carries
  // the server-derived fields (exportActive, the per-section 'enabled' flags, the metrics defaults)
  // that a CR never authors.

  private static UniverseLogsExporterConfig auditExporter(UUID exporterUuid) {
    UniverseLogsExporterConfig exporter = new UniverseLogsExporterConfig();
    exporter.setExporterUuid(exporterUuid);
    exporter.setAdditionalTags(new HashMap<>(Map.of("env", "prod")));
    return exporter;
  }

  private static UniverseServerLogsExporterConfig serverExporter(UUID exporterUuid) {
    UniverseServerLogsExporterConfig exporter = new UniverseServerLogsExporterConfig();
    exporter.setExporterUuid(exporterUuid);
    return exporter;
  }

  private static AuditLogConfig storedAuditLogConfig(UUID exporterUuid) {
    AuditLogConfig config = new AuditLogConfig();
    YSQLAuditConfig ysql = new YSQLAuditConfig();
    // Derived: the mapper force-sets this whenever the sub-object is present.
    ysql.setEnabled(true);
    ysql.setClasses(
        EnumSet.of(
            YSQLAuditConfig.YSQLAuditStatementClass.WRITE,
            YSQLAuditConfig.YSQLAuditStatementClass.DDL));
    ysql.setLogParameter(true);
    config.setYsqlAuditConfig(ysql);
    config.setUniverseLogsExporterConfig(new ArrayList<>(List.of(auditExporter(exporterUuid))));
    // Derived: the mapper sets this from isNotEmpty(exporters).
    config.setExportActive(true);
    return config;
  }

  private static QueryLogConfig storedQueryLogConfig(UUID exporterUuid) {
    QueryLogConfig config = new QueryLogConfig();
    YSQLQueryLogConfig ysql = new YSQLQueryLogConfig();
    ysql.setEnabled(true);
    ysql.setLogConnections(true);
    config.setYsqlQueryLogConfig(ysql);
    UniverseQueryLogsExporterConfig exporter = new UniverseQueryLogsExporterConfig();
    exporter.setExporterUuid(exporterUuid);
    config.setUniverseLogsExporterConfig(new ArrayList<>(List.of(exporter)));
    config.setExportActive(true);
    return config;
  }

  private static MetricsExportConfig storedMetricsExportConfig(UUID exporterUuid) {
    MetricsExportConfig config = new MetricsExportConfig();
    // Defaulted: 30 / 20 / NORMAL come from the model, the target set from the operator.
    config.setScrapeIntervalSeconds(30);
    config.setScrapeTimeoutSeconds(20);
    config.setCollectionLevel(MetricCollectionLevel.NORMAL);
    config.setScrapeConfigTargets(EnumSet.copyOf(OtelCollectorUtil.K8S_SUPPORTED_SCRAPE_TARGETS));
    UniverseMetricsExporterConfig exporter = new UniverseMetricsExporterConfig();
    exporter.setExporterUuid(exporterUuid);
    config.setUniverseMetricsExporterConfig(new ArrayList<>(List.of(exporter)));
    return config;
  }

  private static MasterLogConfig storedMasterLogConfig(UUID exporterUuid) {
    MasterLogConfig config = new MasterLogConfig();
    config.setMinLevel(ServerLogLevel.INFO);
    config.setNoiseSampleDropRatio(0.99);
    config.setUniverseLogsExporterConfig(new ArrayList<>(List.of(serverExporter(exporterUuid))));
    return config;
  }

  private static TServerLogConfig storedTserverLogConfig(UUID exporterUuid) {
    TServerLogConfig config = new TServerLogConfig();
    config.setMinLevel(ServerLogLevel.WARNING);
    config.setUniverseLogsExporterConfig(new ArrayList<>(List.of(serverExporter(exporterUuid))));
    return config;
  }

  private static YsqlConnMgrLogConfig storedYsqlConnMgrLogConfig(UUID exporterUuid) {
    YsqlConnMgrLogConfig config = new YsqlConnMgrLogConfig();
    config.setUniverseLogsExporterConfig(new ArrayList<>(List.of(serverExporter(exporterUuid))));
    return config;
  }

  private static ControllerLogConfig storedControllerLogConfig(UUID exporterUuid) {
    ControllerLogConfig config = new ControllerLogConfig();
    config.setUniverseLogsExporterConfig(new ArrayList<>(List.of(serverExporter(exporterUuid))));
    return config;
  }

  /** One TelemetryConfig per export type, populating only that type's section. */
  private static Map<ExportType, TelemetryConfig> singleSectionConfigs(UUID exporterUuid) {
    Map<ExportType, TelemetryConfig> configs = new EnumMap<>(ExportType.class);
    configs.put(
        ExportType.AUDIT_LOGS,
        TelemetryConfig.builder().auditLogConfig(storedAuditLogConfig(exporterUuid)).build());
    configs.put(
        ExportType.QUERY_LOGS,
        TelemetryConfig.builder().queryLogConfig(storedQueryLogConfig(exporterUuid)).build());
    configs.put(
        ExportType.METRICS,
        TelemetryConfig.builder()
            .metricsExportConfig(storedMetricsExportConfig(exporterUuid))
            .build());
    configs.put(
        ExportType.MASTER_LOGS,
        TelemetryConfig.builder().masterLogConfig(storedMasterLogConfig(exporterUuid)).build());
    configs.put(
        ExportType.TSERVER_LOGS,
        TelemetryConfig.builder().tserverLogConfig(storedTserverLogConfig(exporterUuid)).build());
    configs.put(
        ExportType.YSQL_CONN_MGR_LOGS,
        TelemetryConfig.builder()
            .ysqlConnMgrLogConfig(storedYsqlConnMgrLogConfig(exporterUuid))
            .build());
    configs.put(
        ExportType.CONTROLLER_LOGS,
        TelemetryConfig.builder()
            .controllerLogConfig(storedControllerLogConfig(exporterUuid))
            .build());
    return configs;
  }

  /**
   * Guard test for a newly added {@link ExportType}. The per-section comparators in {@link
   * OperatorUtils#telemetrySectionDiffers} are hand-written - they cannot derive from {@code
   * ExportType.values()} the way {@code TelemetryConfig.diff} does, because they have to exclude
   * the server-derived fields - so a new export type would otherwise silently never trigger a
   * telemetry reconcile. Adding a value to {@code ExportType} fails this test: it has no fixture
   * below, and {@code telemetrySectionDiffers} throws on the unhandled case.
   */
  @Test
  public void testTelemetrySectionDiffersCoversEveryExportType() {
    UUID exporterUuid = UUID.randomUUID();
    Map<ExportType, TelemetryConfig> singleSection = singleSectionConfigs(exporterUuid);

    // The VM-only set is pinned rather than derived, so a new unsupported type also has to be
    // acknowledged here (and omitted from the universe CRD).
    Set<ExportType> vmOnly = EnumSet.noneOf(ExportType.class);
    for (ExportType type : ExportType.values()) {
      if (!type.isSupportedOnKubernetes()) {
        vmOnly.add(type);
      }
    }
    assertEquals(
        "The set of Kubernetes-unsupported export types changed. Decide whether the new type"
            + " belongs in the universe CRD before updating this expectation.",
        EnumSet.of(ExportType.NODE_AGENT_LOGS, ExportType.YNP_LOGS),
        vmOnly);

    NodeAgentLogConfig nodeAgentLogConfig = new NodeAgentLogConfig();
    nodeAgentLogConfig.setUniverseLogsExporterConfig(
        new ArrayList<>(List.of(serverExporter(exporterUuid))));
    YnpLogConfig ynpLogConfig = new YnpLogConfig();
    ynpLogConfig.setUniverseLogsExporterConfig(
        new ArrayList<>(List.of(serverExporter(exporterUuid))));
    TelemetryConfig vmOnlySections =
        TelemetryConfig.builder()
            .nodeAgentLogConfig(nodeAgentLogConfig)
            .ynpLogConfig(ynpLogConfig)
            .build();

    for (ExportType type : ExportType.values()) {
      if (vmOnly.contains(type)) {
        assertFalse(
            type + " is VM-only and must never trigger a telemetry reconcile on Kubernetes",
            OperatorUtils.telemetrySectionDiffers(type, vmOnlySections, new TelemetryConfig()));
        continue;
      }
      TelemetryConfig desired = singleSection.get(type);
      assertNotNull(
          "Export type "
              + type
              + " has no comparator fixture. Add its case to"
              + " OperatorUtils.telemetrySectionDiffers and a fixture to singleSectionConfigs(),"
              + " otherwise the operator will never reconcile it.",
          desired);
      assertTrue(
          type + ": a newly configured section must differ from a config that has none",
          OperatorUtils.telemetrySectionDiffers(type, desired, new TelemetryConfig()));
      assertFalse(
          type + ": an unchanged section must not differ from itself",
          OperatorUtils.telemetrySectionDiffers(type, desired, desired));
      // Each comparator must read only its own section - a copy-paste that compares the wrong field
      // would make one section's change fire another section's branch.
      for (ExportType other : ExportType.values()) {
        if (other == type) {
          continue;
        }
        assertFalse(
            "Populating only " + type + " must not report a difference for " + other,
            OperatorUtils.telemetrySectionDiffers(other, desired, new TelemetryConfig()));
      }
    }
  }

  // Note: the derived-field rules themselves (audit/query exportActive, the per-section 'enabled'
  // flags, and that the comparison does not mutate the stored objects) are covered against real
  // converter output in UniverseTelemetrySpecConverterTest. What follows is what that suite does
  // not
  // reach: the ExportType guard, and the exporter-level and metrics-level rules.

  @Test
  public void testTelemetrySectionDiffersTreatsNullAndEmptyExporterListsAsEqual() {
    ControllerLogConfig storedSection = new ControllerLogConfig();
    // Older stored rows can hold null where the shared mapper now always writes an empty list.
    storedSection.setUniverseLogsExporterConfig(null);
    ControllerLogConfig desiredSection = new ControllerLogConfig();
    desiredSection.setUniverseLogsExporterConfig(new ArrayList<>());

    assertFalse(
        "null and empty exporter lists both mean 'no exporter'",
        OperatorUtils.telemetrySectionDiffers(
            ExportType.CONTROLLER_LOGS,
            TelemetryConfig.builder().controllerLogConfig(desiredSection).build(),
            TelemetryConfig.builder().controllerLogConfig(storedSection).build()));
  }

  @Test
  public void testTelemetrySectionDiffersIgnoresUnsetMetricsExporterDefaults() {
    UUID exporterUuid = UUID.randomUUID();
    MetricsExportConfig stored = storedMetricsExportConfig(exporterUuid);
    // A stored row predating the metricsPrefix/additionalTags defaults holds nulls the CR cannot
    // express, so they must not read as a permanent difference.
    stored.getUniverseMetricsExporterConfig().get(0).setMetricsPrefix(null);
    stored.getUniverseMetricsExporterConfig().get(0).setAdditionalTags(null);
    MetricsExportConfig desired = storedMetricsExportConfig(exporterUuid);

    assertFalse(
        OperatorUtils.telemetrySectionDiffers(
            ExportType.METRICS,
            TelemetryConfig.builder().metricsExportConfig(desired).build(),
            TelemetryConfig.builder().metricsExportConfig(stored).build()));

    // An authored metricsPrefix is still compared.
    desired.getUniverseMetricsExporterConfig().get(0).setMetricsPrefix("ybdb.");
    assertTrue(
        OperatorUtils.telemetrySectionDiffers(
            ExportType.METRICS,
            TelemetryConfig.builder().metricsExportConfig(desired).build(),
            TelemetryConfig.builder().metricsExportConfig(stored).build()));
  }

  @Test
  public void testTelemetrySectionDiffersComparesAuthoredMetricsFields() {
    UUID exporterUuid = UUID.randomUUID();
    TelemetryConfig stored =
        TelemetryConfig.builder()
            .metricsExportConfig(storedMetricsExportConfig(exporterUuid))
            .build();

    MetricsExportConfig interval = storedMetricsExportConfig(exporterUuid);
    interval.setScrapeIntervalSeconds(60);
    assertTrue(
        OperatorUtils.telemetrySectionDiffers(
            ExportType.METRICS,
            TelemetryConfig.builder().metricsExportConfig(interval).build(),
            stored));

    MetricsExportConfig level = storedMetricsExportConfig(exporterUuid);
    level.setCollectionLevel(MetricCollectionLevel.ALL);
    assertTrue(
        OperatorUtils.telemetrySectionDiffers(
            ExportType.METRICS,
            TelemetryConfig.builder().metricsExportConfig(level).build(),
            stored));

    MetricsExportConfig targets = storedMetricsExportConfig(exporterUuid);
    targets.setScrapeConfigTargets(EnumSet.of(ScrapeConfigTargetType.MASTER_EXPORT));
    assertTrue(
        OperatorUtils.telemetrySectionDiffers(
            ExportType.METRICS,
            TelemetryConfig.builder().metricsExportConfig(targets).build(),
            stored));
  }

  /*--- Telemetry export: getDesiredTelemetryConfig ---*/

  private static final String TELEMETRY_NAMESPACE = "test-namespace";
  private static final String TELEMETRY_PROVIDER_CR = "datadog-prod";

  private void createTelemetryProviderCr(String name, String state, UUID resourceUuid) {
    io.yugabyte.operator.v1alpha1.TelemetryProvider cr =
        new io.yugabyte.operator.v1alpha1.TelemetryProvider();
    ObjectMeta metadata = new ObjectMeta();
    metadata.setName(name);
    metadata.setNamespace(TELEMETRY_NAMESPACE);
    cr.setMetadata(metadata);
    io.yugabyte.operator.v1alpha1.TelemetryProviderSpec spec =
        new io.yugabyte.operator.v1alpha1.TelemetryProviderSpec();
    spec.setProvider(io.yugabyte.operator.v1alpha1.TelemetryProviderSpec.Provider.DATA_DOG);
    cr.setSpec(spec);
    io.yugabyte.operator.v1alpha1.TelemetryProviderStatus status =
        new io.yugabyte.operator.v1alpha1.TelemetryProviderStatus();
    status.setState(state);
    status.setResourceUUID(resourceUuid == null ? null : resourceUuid.toString());
    cr.setStatus(status);
    kubernetesClient
        .resources(io.yugabyte.operator.v1alpha1.TelemetryProvider.class)
        .inNamespace(TELEMETRY_NAMESPACE)
        .resource(cr)
        .create();
  }

  private static io.yugabyte.operator.v1alpha1.YBUniverse ybUniverseWithTelemetry(
      Telemetry telemetry) {
    io.yugabyte.operator.v1alpha1.YBUniverse ybUniverse =
        new io.yugabyte.operator.v1alpha1.YBUniverse();
    ObjectMeta metadata = new ObjectMeta();
    metadata.setName("telemetry-universe");
    metadata.setNamespace(TELEMETRY_NAMESPACE);
    ybUniverse.setMetadata(metadata);
    io.yugabyte.operator.v1alpha1.YBUniverseSpec spec =
        new io.yugabyte.operator.v1alpha1.YBUniverseSpec();
    spec.setTelemetry(telemetry);
    ybUniverse.setSpec(spec);
    return ybUniverse;
  }

  private static io.yugabyte.operator.v1alpha1.ybuniversespec.telemetry.metrics.Exporters
      crMetricsExporter() {
    io.yugabyte.operator.v1alpha1.ybuniversespec.telemetry.metrics.Exporters exporter =
        new io.yugabyte.operator.v1alpha1.ybuniversespec.telemetry.metrics.Exporters();
    exporter.setTelemetryProvider(TELEMETRY_PROVIDER_CR);
    return exporter;
  }

  private static Telemetry crTelemetryWithMetrics() {
    Metrics metrics = new Metrics();
    metrics.setExporters(List.of(crMetricsExporter()));
    Telemetry telemetry = new Telemetry();
    telemetry.setMetrics(metrics);
    return telemetry;
  }

  @Test
  public void testResolveReadyTelemetryProviderUuidResolvesReadyCr() throws Exception {
    UUID providerUuid = UUID.randomUUID();
    createTelemetryProviderCr(TELEMETRY_PROVIDER_CR, "Ready", providerUuid);

    assertEquals(
        providerUuid,
        operatorUtils.resolveReadyTelemetryProviderUuid(
            TELEMETRY_PROVIDER_CR, TELEMETRY_NAMESPACE));
  }

  @Test
  public void testResolveReadyTelemetryProviderUuidResolvesInUseCr() throws Exception {
    // InUse is what the TelemetryProvider reconciler reports after refusing to delete a provider a
    // universe still references; that must not stop the universe from reconciling.
    UUID providerUuid = UUID.randomUUID();
    createTelemetryProviderCr(TELEMETRY_PROVIDER_CR, "InUse", providerUuid);

    assertEquals(
        providerUuid,
        operatorUtils.resolveReadyTelemetryProviderUuid(
            TELEMETRY_PROVIDER_CR, TELEMETRY_NAMESPACE));
  }

  @Test
  public void testGetDesiredTelemetryConfigMissingProviderCrFails() {
    io.yugabyte.operator.v1alpha1.YBUniverse ybUniverse =
        ybUniverseWithTelemetry(crTelemetryWithMetrics());

    Exception ex =
        assertThrows(Exception.class, () -> operatorUtils.getDesiredTelemetryConfig(ybUniverse));
    assertTrue(
        "expected a message naming the missing CR, got: " + ex.getMessage(),
        ex.getMessage().contains(TELEMETRY_PROVIDER_CR) && ex.getMessage().contains("not found"));
  }

  @Test
  public void testGetDesiredTelemetryConfigNotReadyProviderCrFails() {
    createTelemetryProviderCr(TELEMETRY_PROVIDER_CR, "Error", null);
    io.yugabyte.operator.v1alpha1.YBUniverse ybUniverse =
        ybUniverseWithTelemetry(crTelemetryWithMetrics());

    Exception ex =
        assertThrows(Exception.class, () -> operatorUtils.getDesiredTelemetryConfig(ybUniverse));
    assertTrue(
        "expected a message naming the CR and its state, got: " + ex.getMessage(),
        ex.getMessage().contains(TELEMETRY_PROVIDER_CR) && ex.getMessage().contains("not ready"));
  }

  @Test
  public void testGetDesiredTelemetryConfigReadyProviderWithoutUuidFails() {
    // The readiness gate needs both halves: state Ready is not enough without a resolved UUID.
    createTelemetryProviderCr(TELEMETRY_PROVIDER_CR, "Ready", null);
    io.yugabyte.operator.v1alpha1.YBUniverse ybUniverse =
        ybUniverseWithTelemetry(crTelemetryWithMetrics());

    Exception ex =
        assertThrows(Exception.class, () -> operatorUtils.getDesiredTelemetryConfig(ybUniverse));
    assertTrue(
        "expected a message about the missing resolved UUID, got: " + ex.getMessage(),
        ex.getMessage().contains(TELEMETRY_PROVIDER_CR)
            && ex.getMessage().contains("no resolved provider UUID"));
  }

  @Test
  public void testGetDesiredTelemetryConfigDefaultsScrapeTargetsToK8sSupported() throws Exception {
    UUID providerUuid = UUID.randomUUID();
    createTelemetryProviderCr(TELEMETRY_PROVIDER_CR, "Ready", providerUuid);
    // metrics with no scrapeConfigTargets: the shared mapper would default this to
    // EnumSet.allOf(ScrapeConfigTargetType.class), which includes the two VM-only targets that
    // verifyParams rejects on a Kubernetes universe, so every reconcile would 400.
    io.yugabyte.operator.v1alpha1.YBUniverse ybUniverse =
        ybUniverseWithTelemetry(crTelemetryWithMetrics());

    TelemetryConfig desired = operatorUtils.getDesiredTelemetryConfig(ybUniverse);

    Set<ScrapeConfigTargetType> targets = desired.getMetricsExportConfig().getScrapeConfigTargets();
    assertEquals(OtelCollectorUtil.K8S_SUPPORTED_SCRAPE_TARGETS, targets);
    assertFalse(
        "NODE_EXPORT is VM-only and must never be defaulted in",
        targets.contains(ScrapeConfigTargetType.NODE_EXPORT));
    assertFalse(
        "NODE_AGENT_EXPORT is VM-only and must never be defaulted in",
        targets.contains(ScrapeConfigTargetType.NODE_AGENT_EXPORT));
    // The exporter's TelemetryProvider CR name is resolved to the YBA UUID.
    assertEquals(
        providerUuid,
        desired
            .getMetricsExportConfig()
            .getUniverseMetricsExporterConfig()
            .get(0)
            .getExporterUuid());
    // The other metrics defaults come out as the REST path leaves them.
    assertEquals(Integer.valueOf(30), desired.getMetricsExportConfig().getScrapeIntervalSeconds());
    assertEquals(Integer.valueOf(20), desired.getMetricsExportConfig().getScrapeTimeoutSeconds());
    assertEquals(
        MetricCollectionLevel.NORMAL, desired.getMetricsExportConfig().getCollectionLevel());
  }

  @Test
  public void testGetDesiredTelemetryConfigExplicitScrapeTargetsAreHonored() throws Exception {
    createTelemetryProviderCr(TELEMETRY_PROVIDER_CR, "Ready", UUID.randomUUID());
    Metrics metrics = new Metrics();
    metrics.setExporters(List.of(crMetricsExporter()));
    metrics.setScrapeConfigTargets(
        List.of(
            Metrics.ScrapeConfigTargets.MASTER_EXPORT, Metrics.ScrapeConfigTargets.TSERVER_EXPORT));
    Telemetry telemetry = new Telemetry();
    telemetry.setMetrics(metrics);

    TelemetryConfig desired =
        operatorUtils.getDesiredTelemetryConfig(ybUniverseWithTelemetry(telemetry));

    assertEquals(
        EnumSet.of(ScrapeConfigTargetType.MASTER_EXPORT, ScrapeConfigTargetType.TSERVER_EXPORT),
        desired.getMetricsExportConfig().getScrapeConfigTargets());
  }

  @Test
  public void testGetDesiredTelemetryConfigNoTelemetryBlockYieldsAllNullSections()
      throws Exception {
    io.yugabyte.operator.v1alpha1.YBUniverse ybUniverse = ybUniverseWithTelemetry(null);

    TelemetryConfig desired = operatorUtils.getDesiredTelemetryConfig(ybUniverse);

    assertFalse("no telemetry block must mean no exports at all", desired.hasAnyConfig());
    for (ExportType type : ExportType.values()) {
      assertNull(type + " must be null when the CR has no telemetry block", desired.section(type));
    }
  }

  @Test
  public void testGetDesiredTelemetryConfigEmptyTelemetryBlockYieldsAllNullSections()
      throws Exception {
    io.yugabyte.operator.v1alpha1.YBUniverse ybUniverse = ybUniverseWithTelemetry(new Telemetry());

    TelemetryConfig desired = operatorUtils.getDesiredTelemetryConfig(ybUniverse);

    assertFalse("an empty telemetry block must mean no exports", desired.hasAnyConfig());
  }

  @Test
  public void testGetDesiredTelemetryConfigNeverSetsVmOnlySections() throws Exception {
    createTelemetryProviderCr(TELEMETRY_PROVIDER_CR, "Ready", UUID.randomUUID());
    io.yugabyte.operator.v1alpha1.YBUniverse ybUniverse =
        ybUniverseWithTelemetry(crTelemetryWithMetrics());

    TelemetryConfig desired = operatorUtils.getDesiredTelemetryConfig(ybUniverse);

    // node-agent and YNP logs are absent from the universe CRD, so they can never be authored.
    assertNull(desired.getNodeAgentLogConfig());
    assertNull(desired.getYnpLogConfig());
  }

  /**
   * The import path builds a CR spec out of a stored auth config, and the reconciler turns that CR
   * back into an auth config. If the two disagree the reconciler sees an edit on every resync, so
   * the round trip has to land on the config it started from. Vault AppRole is the sharpest case:
   * the auth config holds a namespace-qualified mount path while the CR holds a relative one.
   */
  @Test
  public void testBuildKMSConfigSpecHashicorpAppRoleRoundTrips() {
    ObjectNode authConfig = Json.newObject();
    authConfig.put(HashicorpVaultConfigParams.HC_VAULT_ADDRESS, "http://vault:8200");
    authConfig.put(HashicorpVaultConfigParams.HC_VAULT_KEY_NAME, "key_yugabyte");
    authConfig.put(HashicorpVaultConfigParams.HC_VAULT_ENGINE, "transit");
    authConfig.put(HashicorpVaultConfigParams.HC_VAULT_ROLE_ID, "role-id");
    authConfig.put(HashicorpVaultConfigParams.HC_VAULT_SECRET_ID, "secret-id");
    authConfig.put(HashicorpVaultConfigParams.HC_VAULT_AUTH_NAMESPACE, "yb-ns");
    authConfig.put(HashicorpVaultConfigParams.HC_VAULT_MOUNT_PATH, "yb-ns/transit/");
    KmsConfig cfg =
        KmsConfig.createKMSConfig(
            testCustomer.getUuid(), KeyProvider.HASHICORP, authConfig, "vault-kms-config");

    // Only the AppRole secret ID is a credential, so only it needs a Secret.
    assertEquals(
        Map.of(HashicorpVaultConfigParams.HC_VAULT_SECRET_ID, "secret-id"),
        OperatorUtils.getKMSConfigSecretValues(cfg));

    KMSConfigSpec spec =
        operatorUtils.buildKMSConfigSpec(
            cfg,
            "test-namespace",
            Map.of(HashicorpVaultConfigParams.HC_VAULT_SECRET_ID, "vault-secret-id"));

    assertEquals(KMSConfigSpec.Provider.HASHICORP, spec.getProvider());
    assertEquals(Vault.AuthType.APPROLE, spec.getVault().getAuthType());
    assertEquals("role-id", spec.getVault().getAppRole().getRoleID());
    assertEquals("vault-secret-id", spec.getVault().getAppRole().getSecretIdSecret().getName());
    assertEquals(
        HashicorpVaultConfigParams.HC_VAULT_SECRET_ID,
        spec.getVault().getAppRole().getSecretIdSecret().getKey());
    // The mount path on the CR is relative to the auth namespace.
    assertEquals("transit/", spec.getVault().getMountPath());

    KMSConfig kmsConfigCr = baseKmsConfigCr(KMSConfigSpec.Provider.HASHICORP);
    kmsConfigCr.setSpec(spec);
    doReturn(new Secret()).when(operatorUtils).getSecret(anyString(), nullable(String.class));
    doReturn("secret-id").when(operatorUtils).parseSecretForKey(any(Secret.class), anyString());

    ObjectNode roundTripped = operatorUtils.getKMSConfigFormDataFromCr(kmsConfigCr);

    // The form data carries the config name alongside the auth config fields; the rest of it has
    // to match the auth config the spec was built from, exactly.
    assertEquals("vault-kms-config", roundTripped.remove("name").asText());
    assertEquals(authConfig, roundTripped);
  }

  /**
   * A config authenticating with the host IAM profile stores no credentials at all, so nothing has
   * to move into a Secret and the spec must not ask for one.
   */
  @Test
  public void testBuildKMSConfigSpecAwsIamProfileNeedsNoSecrets() {
    ObjectNode authConfig = Json.newObject();
    authConfig.put(AwsKmsAuthConfigField.REGION.fieldName, "us-west-2");
    authConfig.put(AwsKmsAuthConfigField.CMK_ID.fieldName, "arn:aws:kms:us-west-2:1:key/cmk");
    KmsConfig cfg =
        KmsConfig.createKMSConfig(
            testCustomer.getUuid(), KeyProvider.AWS, authConfig, "aws-kms-config");

    assertTrue(OperatorUtils.getKMSConfigSecretValues(cfg).isEmpty());

    KMSConfigSpec spec = operatorUtils.buildKMSConfigSpec(cfg, "test-namespace", Map.of());

    assertEquals(true, spec.getAws().getUseIAMProfile());
    assertNull(spec.getAws().getAccessKeyIdSecret());
    assertNull(spec.getAws().getSecretAccessKeySecret());
    assertEquals("us-west-2", spec.getAws().getRegion());
    // The CMK is carried over so that a later edit does not create a second one.
    assertEquals("arn:aws:kms:us-west-2:1:key/cmk", spec.getAws().getCmkID());
  }

  // A universe import stores the provider's kubeconfig in a Secret, and the reconciler names the
  // file after that Secret - so the name always differs from the one the provider was created
  // with even though the credentials are identical. Comparing names reported drift forever and
  // drove an endless edit retry. PLAT-22036.
  @Test
  public void testHasKubeConfigChangedComparesContentNotFileName() throws Exception {
    java.io.File kubeConfig = java.io.File.createTempFile("sa-kubeconfig", ".yaml");
    kubeConfig.deleteOnExit();
    String content = "apiVersion: v1\nkind: Config\nusers:\n- name: sa\n";
    java.nio.file.Files.write(kubeConfig.toPath(), content.getBytes(StandardCharsets.UTF_8));

    Map<String, String> existing = new HashMap<>();
    existing.put("KUBECONFIG", kubeConfig.getAbsolutePath());

    // Same credentials, but reached by a Secret-derived file name, as after an import.
    Map<String, String> importedSameContent = new HashMap<>();
    importedSameContent.put("KUBECONFIG_NAME", "my-provider-provider-kubeconfig.conf");
    importedSameContent.put("KUBECONFIG_CONTENT", content);
    assertFalse(
        "a renamed but identical kubeconfig is not a change",
        operatorUtils.hasKubeConfigChanged(existing, importedSameContent));

    // Trailing whitespace is the one difference an externally written file picks up.
    Map<String, String> trailingNewline = new HashMap<>();
    trailingNewline.put("KUBECONFIG_NAME", "my-provider-provider-kubeconfig.conf");
    trailingNewline.put("KUBECONFIG_CONTENT", content + "\n\n");
    assertFalse(
        "a trailing newline is not a change",
        operatorUtils.hasKubeConfigChanged(existing, trailingNewline));

    // Genuinely different credentials under that same name must still be caught.
    Map<String, String> changed = new HashMap<>();
    changed.put("KUBECONFIG_NAME", "my-provider-provider-kubeconfig.conf");
    changed.put("KUBECONFIG_CONTENT", content + "# rotated\n");
    assertTrue(
        "different kubeconfig content is a change",
        operatorUtils.hasKubeConfigChanged(existing, changed));

    // Same keys, different order - deliberately treated as a change rather than parsed and
    // deep-compared, since deciding which YAML differences are semantically irrelevant risks
    // silently ignoring a real credential change.
    Map<String, String> reordered = new HashMap<>();
    reordered.put("KUBECONFIG_NAME", "my-provider-provider-kubeconfig.conf");
    reordered.put("KUBECONFIG_CONTENT", "kind: Config\napiVersion: v1\nusers:\n- name: sa\n");
    assertTrue(
        "reordered yaml is conservatively treated as a change",
        operatorUtils.hasKubeConfigChanged(existing, reordered));

    // With no content to compare (non-operator callers) the name comparison still applies.
    Map<String, String> nameOnly = new HashMap<>();
    nameOnly.put("KUBECONFIG_NAME", "some-other-name.conf");
    assertTrue(
        "without content the file name is still the fallback",
        operatorUtils.hasKubeConfigChanged(existing, nameOnly));

    // An in-cluster provider carries neither, and must not report drift.
    assertFalse(
        "in-cluster credentials on both sides are not a change",
        operatorUtils.hasKubeConfigChanged(new HashMap<>(), new HashMap<>()));
  }

  // The reconcile loop compares the kubeconfig every pass, but the file only changes on a
  // rotation, so it is cached and dropped when the reconciler submits an edit for that provider.
  @Test
  public void testKubeConfigFileIsCachedUntilTheProviderIsEdited() throws Exception {
    UUID providerUUID = UUID.randomUUID();
    java.io.File providerDir =
        new java.io.File(System.getProperty("java.io.tmpdir"), providerUUID.toString());
    assertTrue("temp provider dir", providerDir.mkdirs() || providerDir.isDirectory());
    java.io.File kubeConfig = new java.io.File(providerDir, "kubeconfig.yaml");
    kubeConfig.deleteOnExit();
    providerDir.deleteOnExit();

    String original = "apiVersion: v1\nkind: Config\nusers:\n- name: original\n";
    java.nio.file.Files.write(kubeConfig.toPath(), original.getBytes(StandardCharsets.UTF_8));

    Map<String, String> existing = new HashMap<>();
    existing.put("KUBECONFIG", kubeConfig.getAbsolutePath());
    Map<String, String> desired = new HashMap<>();
    desired.put("KUBECONFIG_CONTENT", original);

    assertFalse(
        "identical content is not a change", operatorUtils.hasKubeConfigChanged(existing, desired));

    // Rewriting the file behind the cache must not be observed - that is the point of caching.
    java.nio.file.Files.write(
        kubeConfig.toPath(),
        "apiVersion: v1\nkind: Config\nusers:\n- name: rotated\n".getBytes(StandardCharsets.UTF_8));
    assertFalse(
        "the cached copy is still served", operatorUtils.hasKubeConfigChanged(existing, desired));

    // An edit for this provider drops its entries, so the next read picks the new file up.
    operatorUtils.invalidateKubeConfigCache(providerUUID);
    assertTrue(
        "after invalidation the rotated file is read",
        operatorUtils.hasKubeConfigChanged(existing, desired));
  }

  /*--- getUniverseFromCr tests ---*/

  @Test
  public void testGetUniverseFromCrResolvesImportedUniverseByAnnotation() {
    // An imported universe keeps the name it already had in YBA, while getUniverseName() derives
    // "imported-cr-<hash>" from the resource metadata, so the annotation is the only link.
    YBUniverse ybUniverse =
        makeYbUniverse(
            "imported-cr",
            "test-namespace",
            null /* specUniverseName */,
            testUniverse.getUniverseUUID());

    Optional<Universe> universe = OperatorUtils.getUniverseFromCr(testCustomer.getId(), ybUniverse);

    assertTrue(universe.isPresent());
    assertEquals(testUniverse.getUniverseUUID(), universe.get().getUniverseUUID());
  }

  @Test
  public void testGetUniverseFromCrFallsBackToMetadataName() {
    YBUniverse ybUniverse =
        makeYbUniverse(
            "unannotated-cr", "test-namespace", null /* specUniverseName */, null /* resourceId */);
    Universe metadataNamedUniverse =
        ModelFactory.createUniverse(
            OperatorUtils.getYbaResourceName(ybUniverse.getMetadata()), testCustomer.getId());

    Optional<Universe> universe = OperatorUtils.getUniverseFromCr(testCustomer.getId(), ybUniverse);

    assertTrue(universe.isPresent());
    assertEquals(metadataNamedUniverse.getUniverseUUID(), universe.get().getUniverseUUID());
  }

  @Test
  public void testGetUniverseFromCrFallsBackToSpecNameWhenUnannotated() {
    YBUniverse ybUniverse =
        makeYbUniverse(
            "imported-cr", "test-namespace", testUniverse.getName(), null /* ybaResourceId */);

    Optional<Universe> universe = OperatorUtils.getUniverseFromCr(testCustomer.getId(), ybUniverse);

    assertTrue(universe.isPresent());
    assertEquals(testUniverse.getUniverseUUID(), universe.get().getUniverseUUID());
  }

  @Test
  public void testGetUniverseFromCrFallsBackToNameWhenAnnotationIsStale() {
    YBUniverse ybUniverse =
        makeYbUniverse("imported-cr", "test-namespace", testUniverse.getName(), UUID.randomUUID());

    Optional<Universe> universe = OperatorUtils.getUniverseFromCr(testCustomer.getId(), ybUniverse);

    assertTrue(universe.isPresent());
    assertEquals(testUniverse.getUniverseUUID(), universe.get().getUniverseUUID());
  }

  @Test
  public void testGetUniverseFromCrDoesNotCrossCustomerBoundary() {
    Customer otherCustomer = ModelFactory.testCustomer("other-customer");
    YBUniverse ybUniverse =
        makeYbUniverse(
            "imported-cr",
            "test-namespace",
            testUniverse.getName(),
            testUniverse.getUniverseUUID());

    Optional<Universe> universe =
        OperatorUtils.getUniverseFromCr(otherCustomer.getId(), ybUniverse);

    assertFalse(universe.isPresent());
  }

  @Test
  public void testGetUniverseFromCrNullResource() {
    assertFalse(
        OperatorUtils.getUniverseFromCr(testCustomer.getId(), null /* ybUniverse */).isPresent());
  }

  /*--- getUniverseFromNameAndNamespace tests ---*/

  @Test
  public void testGetUniverseFromNameAndNamespaceResolvesImportedUniverse() throws Exception {
    String namespace = "test-namespace";
    YBUniverse ybUniverse =
        makeYbUniverse(
            "imported-cr", namespace, null /* specUniverseName */, testUniverse.getUniverseUUID());
    kubernetesClient
        .resources(YBUniverse.class)
        .inNamespace(namespace)
        .resource(ybUniverse)
        .create();

    Universe universe =
        operatorUtils.getUniverseFromNameAndNamespace(
            testCustomer.getId(), "imported-cr", namespace);

    assertNotNull(universe);
    assertEquals(testUniverse.getUniverseUUID(), universe.getUniverseUUID());
  }

  @Test
  public void testGetUniverseFromNameAndNamespaceMissingCustomResource() throws Exception {
    assertNull(
        operatorUtils.getUniverseFromNameAndNamespace(
            testCustomer.getId(), "does-not-exist", "test-namespace"));
  }

  @Test
  public void testCreateBackupScheduleCrWithoutResourceDetailsThrows() {
    Schedule backupSchedule =
        ModelFactory.createScheduleBackupRequestParams(
            testCustomer.getUuid(),
            testUniverse.getUniverseUUID(),
            testStorageConfig.getConfigUUID(),
            TaskType.BackupUniverse);
    backupSchedule.save();

    // testUniverse has no Kubernetes resource details, so there is no resource name to point at.
    Exception ex =
        assertThrows(
            Exception.class,
            () ->
                operatorUtils.createBackupScheduleCr(
                    backupSchedule, "test-schedule", "test-storage-config", "test-namespace"));
    // createBackupScheduleCr wraps anything it catches, so the guard message lands on the cause.
    assertNotNull(ex.getCause());
    assertTrue(ex.getCause().getMessage().contains("has no Kubernetes resource details"));
  }
}
