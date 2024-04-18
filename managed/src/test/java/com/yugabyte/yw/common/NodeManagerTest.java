// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.common;

import static com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType.CONTROLLER;
import static com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType.MASTER;
import static com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType.TSERVER;
import static com.yugabyte.yw.common.TestHelper.createTempFile;
import static com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeTaskSubType.Download;
import static com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeTaskSubType.Install;
import static com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeTaskType.Certs;
import static com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeTaskType.Everything;
import static com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeTaskType.GFlags;
import static com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeTaskType.Software;
import static com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeTaskType.ToggleTls;
import static org.hamcrest.CoreMatchers.allOf;
import static org.hamcrest.CoreMatchers.is;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.core.StringContains.containsString;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.reset;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.typesafe.config.Config;
import com.yugabyte.yw.cloud.PublicCloudConstants;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleClusterServerCtl;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleConfigureServers;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleCreateServer;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleDestroyServer;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleSetupServer;
import com.yugabyte.yw.commissioner.tasks.subtasks.ChangeInstanceType;
import com.yugabyte.yw.commissioner.tasks.subtasks.CreateRootVolumes;
import com.yugabyte.yw.commissioner.tasks.subtasks.InstanceActions;
import com.yugabyte.yw.commissioner.tasks.subtasks.ReplaceRootVolume;
import com.yugabyte.yw.commissioner.tasks.subtasks.TransferXClusterCerts;
import com.yugabyte.yw.common.NodeManager.CertRotateAction;
import com.yugabyte.yw.common.certmgmt.CertConfigType;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import com.yugabyte.yw.common.certmgmt.EncryptionInTransitUtil;
import com.yugabyte.yw.common.config.CustomerConfKeys;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.ProviderConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.common.utils.FileUtils;
import com.yugabyte.yw.forms.CertificateParams;
import com.yugabyte.yw.forms.CertsRotateParams.CertRotationType;
import com.yugabyte.yw.forms.NodeInstanceFormData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeTaskSubType;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.InstanceType.InstanceTypeDetails;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import java.io.File;
import java.io.IOException;
import java.security.NoSuchAlgorithmException;
import java.time.Duration;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Calendar;
import java.util.Date;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.TreeMap;
import java.util.UUID;
import java.util.function.Consumer;
import java.util.function.Predicate;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import junitparams.converters.Nullable;
import junitparams.naming.TestCaseName;
import org.apache.commons.lang3.RandomStringUtils;
import org.apache.commons.lang3.StringUtils;
import org.junit.Assert;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Captor;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.Mockito;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import play.libs.Json;

@RunWith(JUnitParamsRunner.class)
public class NodeManagerTest extends FakeDBApplication {

  @Rule public MockitoRule rule = MockitoJUnit.rule();
  @Mock ShellProcessHandler shellProcessHandler;

  @Mock ReleaseManager releaseManager;

  @Mock RuntimeConfigFactory runtimeConfigFactory;

  @Mock ConfigHelper mockConfigHelper;

  @InjectMocks NodeManager nodeManager;

  @Mock Config mockConfig;

  @Mock NodeAgentClient nodeAgentClient;

  @Mock RuntimeConfGetter mockConfGetter;

  @Mock ReleasesUtils mockReleasesUtils;

  private CertificateHelper certificateHelper;

  private final String DOCKER_NETWORK = "yugaware_bridge";
  private final String MASTER_ADDRESSES = "10.0.0.1:7100,10.0.0.2:7100,10.0.0.3:7100";
  private final String[] NODE_IPS = {"10.0.0.1", "10.0.0.2", "10.0.0.3"};
  private final String fakeMountPath1 = "/fake/path/d0";
  private final String fakeMountPath2 = "/fake/path/d1";
  private final String fakeMountPaths = fakeMountPath1 + "," + fakeMountPath2;
  private final String instanceTypeCode = "fake_instance_type";

  private static final List<String> PRECHECK_CERT_PATHS =
      Arrays.asList(
          "--root_cert_path",
          "--server_cert_path",
          "--server_key_path",
          "--client_cert_path",
          "--client_key_path",
          "--root_cert_path_client_to_server",
          "--server_cert_path_client_to_server",
          "--server_key_path_client_to_server",
          "--skip_cert_validation");

  private class TestData {
    public final Customer customer;
    public final Common.CloudType cloudType;
    public final PublicCloudConstants.StorageType storageType;
    public final Provider provider;
    public final Region region;
    public final AvailabilityZone zone;
    public final NodeInstance node;
    public String privateKey = "/path/to/private.key";
    public final List<String> baseCommand = new ArrayList<>();
    public final String replacementVolume = "test-volume";
    public final String newInstanceTypeCode = "test-c5.2xlarge";
    public final String rootDeviceName = "/dev/sda1";

    public TestData(
        Customer customer,
        Provider p,
        Common.CloudType cloud,
        PublicCloudConstants.StorageType storageType,
        int idx) {
      this.customer = customer;
      cloudType = cloud;
      this.storageType = storageType;
      provider = p;
      region = Region.create(provider, "region-1", "Region 1", "yb-image-1");
      zone = AvailabilityZone.createOrThrow(region, "az-1", "AZ 1", "subnet-1");
      InstanceType.upsert(
          provider.getUuid(), newInstanceTypeCode, 6.0, 16.0, new InstanceTypeDetails());
      NodeInstanceFormData.NodeInstanceData nodeData = new NodeInstanceFormData.NodeInstanceData();
      nodeData.ip = "fake_ip";
      nodeData.region = region.getCode();
      nodeData.zone = zone.getCode();
      nodeData.instanceType = instanceTypeCode;
      node = NodeInstance.create(zone.getUuid(), nodeData);
      // Update name.
      node.setNodeName("host-n" + idx);
      node.save();

      baseCommand.add("bin/ybcloud.sh");
      baseCommand.add(provider.getCode());
      baseCommand.add("--region");
      baseCommand.add(region.getCode());
      baseCommand.add("--zone");
      baseCommand.add(zone.getCode());

      if (cloudType == Common.CloudType.docker) {
        baseCommand.add("--network");
        baseCommand.add(DOCKER_NETWORK);
      }

      if (cloudType == Common.CloudType.onprem) {
        baseCommand.add("--node_metadata");
        baseCommand.add(node.getDetailsJson());
      }
    }
  }

  private List<TestData> testData;

  public List<TestData> getTestData(Customer customer, Common.CloudType cloud) {
    List<TestData> testDataList = new ArrayList<>();
    Provider provider = ModelFactory.newProvider(customer, cloud);
    InstanceType.upsert(provider.getUuid(), instanceTypeCode, 4.0, 12.0, new InstanceTypeDetails());
    if (cloud.equals(Common.CloudType.aws)) {
      testDataList.add(
          new TestData(customer, provider, cloud, PublicCloudConstants.StorageType.GP2, 1));
    } else if (cloud.equals(Common.CloudType.gcp)) {
      testDataList.add(
          new TestData(customer, provider, cloud, PublicCloudConstants.StorageType.IO1, 2));
    } else {
      testDataList.add(new TestData(customer, provider, cloud, null, 3));
    }
    return testDataList;
  }

  private Universe createUniverse() {
    UUID universeUUID = UUID.randomUUID();
    return ModelFactory.createUniverse("Test Universe-" + universeUUID, universeUUID);
  }

  private NodeTaskParams buildValidParams(
      TestData testData, NodeTaskParams params, Universe universe) {
    params.azUuid = testData.zone.getUuid();
    params.instanceType = testData.node.getInstanceTypeCode();
    params.nodeName = testData.node.getNodeName();
    params.nodeUuid = testData.node.getNodeUuid();
    params.setUniverseUUID(universe.getUniverseUUID());
    params.placementUuid = universe.getUniverseDetails().getPrimaryCluster().uuid;
    params.currentClusterType = ClusterType.PRIMARY;
    return params;
  }

  private void addValidDeviceInfo(TestData testData, NodeTaskParams params) {
    params.deviceInfo = new DeviceInfo();
    params.deviceInfo.mountPoints = fakeMountPaths;
    params.deviceInfo.volumeSize = 200;
    params.deviceInfo.numVolumes = 2;
    if (testData.cloudType.equals(Common.CloudType.aws)) {
      params.deviceInfo.storageType = testData.storageType;
      if (testData.storageType != null && testData.storageType.isIopsProvisioning()) {
        params.deviceInfo.diskIops = 240;
      }
      if (testData.storageType != null && testData.storageType.isThroughputProvisioning()) {
        params.deviceInfo.throughput = 250;
      }
    }
  }

  private AccessKey getOrCreate(UUID providerUUID, String keyCode, AccessKey.KeyInfo keyInfo) {
    AccessKey accessKey = AccessKey.get(providerUUID, keyCode);
    if (accessKey == null) {
      accessKey = AccessKey.create(providerUUID, keyCode, keyInfo);
    }
    return accessKey;
  }

  private List<String> getCertificatePaths(
      UserIntent userIntent, AnsibleConfigureServers.Params configureParams, String ybHomeDir) {
    return getCertificatePaths(
        userIntent,
        configureParams,
        configureParams.enableNodeToNodeEncrypt,
        configureParams.enableClientToNodeEncrypt,
        ybHomeDir);
  }

  private List<String> getCertificatePaths(
      UserIntent userIntent,
      AnsibleConfigureServers.Params configureParams,
      boolean isRootCARequired,
      boolean isClientRootCARequired,
      String yb_home_dir) {
    ArrayList<String> expectedCommand = new ArrayList<>();
    if (isRootCARequired) {
      expectedCommand.add("--certs_node_dir");
      expectedCommand.add(yb_home_dir + "/yugabyte-tls-config");

      CertificateInfo rootCert = CertificateInfo.get(configureParams.rootCA);
      if (rootCert == null) {
        throw new RuntimeException(
            "No valid rootCA found for " + configureParams.getUniverseUUID());
      }

      String rootCertPath = null, serverCertPath = null, serverKeyPath = null, certsLocation = null;

      switch (rootCert.getCertType()) {
        case SelfSigned:
          {
            rootCertPath = rootCert.getCertificate();
            serverCertPath = "cert.crt";
            serverKeyPath = "key.crt";
            certsLocation = NodeManager.CERT_LOCATION_PLATFORM;

            if (configureParams.rootAndClientRootCASame
                && configureParams.enableClientToNodeEncrypt) {
              expectedCommand.add("--client_cert_path");
              expectedCommand.add(certificateHelper.getClientCertFile(configureParams.rootCA));
              expectedCommand.add("--client_key_path");
              expectedCommand.add(certificateHelper.getClientKeyFile(configureParams.rootCA));
            }
            break;
          }
        case CustomCertHostPath:
          {
            CertificateParams.CustomCertInfo customCertInfo = rootCert.getCustomCertPathParams();
            rootCertPath = customCertInfo.rootCertPath;
            serverCertPath = customCertInfo.nodeCertPath;
            serverKeyPath = customCertInfo.nodeKeyPath;
            certsLocation = NodeManager.CERT_LOCATION_NODE;
            if (configureParams.rootAndClientRootCASame
                && configureParams.enableClientToNodeEncrypt
                && customCertInfo.clientCertPath != null
                && !customCertInfo.clientCertPath.isEmpty()
                && customCertInfo.clientKeyPath != null
                && !customCertInfo.clientKeyPath.isEmpty()) {
              expectedCommand.add("--client_cert_path");
              expectedCommand.add(customCertInfo.clientCertPath);
              expectedCommand.add("--client_key_path");
              expectedCommand.add(customCertInfo.clientKeyPath);
            }
            break;
          }
        case CustomServerCert:
          {
            throw new RuntimeException("rootCA cannot be of type CustomServerCert.");
          }
        case HashicorpVault:
          {
            rootCertPath = rootCert.getCertificate();
            serverCertPath = "cert.crt";
            serverKeyPath = "key.crt";
            certsLocation = NodeManager.CERT_LOCATION_PLATFORM;

            if (configureParams.rootAndClientRootCASame
                && configureParams.enableClientToNodeEncrypt) {
              expectedCommand.add("--client_cert_path");
              expectedCommand.add(certificateHelper.getClientCertFile(configureParams.rootCA));
              expectedCommand.add("--client_key_path");
              expectedCommand.add(certificateHelper.getClientKeyFile(configureParams.rootCA));
            }
            break;
          }
      }

      if (rootCertPath != null
          && serverCertPath != null
          && serverKeyPath != null
          && certsLocation != null) {
        expectedCommand.add("--root_cert_path");
        expectedCommand.add(rootCertPath);
        expectedCommand.add("--server_cert_path");
        expectedCommand.add(serverCertPath);
        expectedCommand.add("--server_key_path");
        expectedCommand.add(serverKeyPath);
        expectedCommand.add("--certs_location");
        expectedCommand.add(certsLocation);
      }
    }
    if (isClientRootCARequired) {
      expectedCommand.add("--certs_client_dir");
      expectedCommand.add(yb_home_dir + "/yugabyte-client-tls-config");

      CertificateInfo clientRootCert = CertificateInfo.get(configureParams.getClientRootCA());
      if (clientRootCert == null) {
        throw new RuntimeException(
            "No valid clientRootCA found for " + configureParams.getUniverseUUID());
      }

      String rootCertPath = null, serverCertPath = null, serverKeyPath = null, certsLocation = null;

      switch (clientRootCert.getCertType()) {
        case SelfSigned:
          {
            rootCertPath = clientRootCert.getCertificate();
            serverCertPath = "cert.crt";
            serverKeyPath = "%key.cert";
            certsLocation = NodeManager.CERT_LOCATION_PLATFORM;

            break;
          }
        case CustomCertHostPath:
          {
            CertificateParams.CustomCertInfo customCertInfo =
                clientRootCert.getCustomCertPathParams();
            rootCertPath = customCertInfo.rootCertPath;
            serverCertPath = customCertInfo.nodeCertPath;
            serverKeyPath = customCertInfo.nodeKeyPath;
            certsLocation = NodeManager.CERT_LOCATION_NODE;
            break;
          }
        case CustomServerCert:
          {
            CertificateInfo.CustomServerCertInfo customServerCertInfo =
                clientRootCert.getCustomServerCertInfo();
            rootCertPath = clientRootCert.getCertificate();
            serverCertPath = customServerCertInfo.serverCert;
            serverKeyPath = customServerCertInfo.serverKey;
            certsLocation = NodeManager.CERT_LOCATION_PLATFORM;
          }
        case HashicorpVault:
          {
            rootCertPath = clientRootCert.getCertificate();
            serverCertPath = "cert.crt";
            serverKeyPath = "%key.cert";
            certsLocation = NodeManager.CERT_LOCATION_PLATFORM;
            break;
          }
      }

      if (rootCertPath != null
          && serverCertPath != null
          && serverKeyPath != null
          && certsLocation != null) {
        expectedCommand.add("--root_cert_path_client_to_server");
        expectedCommand.add(rootCertPath);
        expectedCommand.add("--server_cert_path_client_to_server");
        expectedCommand.add(serverCertPath);
        expectedCommand.add("--server_key_path_client_to_server");
        expectedCommand.add(serverKeyPath);
        expectedCommand.add("--certs_location_client_to_server");
        expectedCommand.add(certsLocation);
      }
    }
    NodeManager.SkipCertValidationType skipType =
        NodeManager.getSkipCertValidationType(
            runtimeConfigFactory.forUniverse(
                Universe.getOrBadRequest(configureParams.getUniverseUUID())),
            userIntent,
            configureParams);

    if (skipType != NodeManager.SkipCertValidationType.NONE) {
      expectedCommand.add("--skip_cert_validation");
      expectedCommand.add(skipType.name());
    }

    return expectedCommand;
  }

  private UUID createUniverseWithCert(TestData t, AnsibleConfigureServers.Params params)
      throws IOException, NoSuchAlgorithmException {
    Calendar cal = Calendar.getInstance();
    Date today = cal.getTime();
    cal.add(Calendar.YEAR, 1); // to get previous year add -1
    Date nextYear = cal.getTime();
    UUID rootCAuuid = UUID.randomUUID();
    CertificateInfo cert;
    CertificateParams.CustomCertInfo customCertInfo = new CertificateParams.CustomCertInfo();
    customCertInfo.rootCertPath = "/path/to/cert.crt";
    customCertInfo.nodeCertPath = "/path/to/rootcert.crt";
    customCertInfo.nodeKeyPath = "/path/to/nodecert.crt";
    if (t.privateKey == null) {
      cert =
          CertificateInfo.create(
              rootCAuuid,
              t.provider.getCustomerUUID(),
              params.nodePrefix,
              today,
              nextYear,
              TestHelper.TMP_PATH + "/node_manager_test_ca.crt",
              customCertInfo);
    } else {
      when(mockConfig.getString("yb.storage.path")).thenReturn(TestHelper.TMP_PATH);
      UUID certUUID =
          certificateHelper.createRootCA(mockConfig, "foobar", t.provider.getCustomerUUID());
      cert = CertificateInfo.get(certUUID);
    }

    Universe u = createUniverse();
    u.getUniverseDetails().rootCA = cert.getUuid();
    buildValidParams(
        t,
        params,
        Universe.saveDetails(u.getUniverseUUID(), ApiUtils.mockUniverseUpdater(t.cloudType)));
    return cert.getUuid();
  }

  @Before
  public void setUp() {
    Customer customer = ModelFactory.testCustomer();
    RuntimeConfGetter confGetter = app.injector().instanceOf(RuntimeConfGetter.class);
    certificateHelper = new CertificateHelper(confGetter);
    testData = new ArrayList<TestData>();
    testData.addAll(getTestData(customer, Common.CloudType.aws));
    testData.addAll(getTestData(customer, Common.CloudType.gcp));
    testData.addAll(getTestData(customer, Common.CloudType.onprem));
    ReleaseManager.ReleaseMetadata releaseMetadata = new ReleaseManager.ReleaseMetadata();
    ReleaseContainer release =
        new ReleaseContainer(releaseMetadata, mockCloudUtilFactory, mockConfig, mockReleasesUtils);
    releaseMetadata.filePath = "/yb/release.tar.gz";
    when(releaseManager.getReleaseByVersion("0.0.1")).thenReturn(release);
    when(mockConfig.getString(NodeManager.BOOT_SCRIPT_PATH)).thenReturn("");
    when(mockConfGetter.getStaticConf()).thenReturn(mockConfig);
    when(mockConfGetter.getConfForScope(
            any(Provider.class), eq(ProviderConfKeys.universeBootScript)))
        .thenReturn("");
    when(mockConfig.getString(eq("yb.security.default.access.key")))
        .thenReturn(ApiUtils.DEFAULT_ACCESS_KEY_CODE);
    when(runtimeConfigFactory.forProvider(any())).thenReturn(mockConfig);
    when(runtimeConfigFactory.forUniverse(any())).thenReturn(app.config());
    when(runtimeConfigFactory.globalRuntimeConf()).thenReturn(mockConfig);
    when(nodeAgentClient.maybeGetNodeAgent(any(), any())).thenReturn(Optional.empty());
    createTempFile("node_manager_test_ca.crt", "test-cert");
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.ybcEnableVervbose)))
        .thenReturn(false);
    when(mockConfGetter.getConfForScope(any(Universe.class), eq(UniverseConfKeys.nfsDirs)))
        .thenReturn("/tmp/nfs,/nfs");
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.ybNumReleasesToKeepCloud)))
        .thenReturn(2);
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.ybNumReleasesToKeepDefault)))
        .thenReturn(3);
    when(mockConfGetter.getConfForScope(any(Universe.class), eq(UniverseConfKeys.numCoresToKeep)))
        .thenReturn(5);
    when(mockConfGetter.getConfForScope(any(Universe.class), eq(UniverseConfKeys.ansibleStrategy)))
        .thenReturn("linear");
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.ansibleConnectionTimeoutSecs)))
        .thenReturn(60);
    when(mockConfGetter.getConfForScope(any(Universe.class), eq(UniverseConfKeys.ansibleVerbosity)))
        .thenReturn(0);
    when(mockConfGetter.getConfForScope(any(Universe.class), eq(UniverseConfKeys.ansibleDebug)))
        .thenReturn(false);
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.ansibleDiffAlways)))
        .thenReturn(false);
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.dbMemPostgresMaxMemMb)))
        .thenReturn(0);
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.gflagsAllowUserOverride)))
        .thenReturn(false);
    when(mockConfGetter.getConfForScope(any(Universe.class), eq(UniverseConfKeys.ansibleLocalTemp)))
        .thenReturn("/tmp/ansible_tmp/");
    when(mockConfGetter.getGlobalConf(eq(GlobalConfKeys.ybTmpDirectoryPath))).thenReturn("/tmp");
    when(mockConfGetter.getGlobalConf(eq(GlobalConfKeys.installLocalesDbNodes))).thenReturn(false);
    when(mockConfGetter.getGlobalConf(eq(GlobalConfKeys.oidcFeatureEnhancements))).thenReturn(true);
    when(mockConfGetter.getGlobalConf(eq(GlobalConfKeys.ssh2Enabled))).thenReturn(false);
    when(mockConfGetter.getGlobalConf(eq(GlobalConfKeys.devopsCommandTimeout)))
        .thenReturn(Duration.ofHours(1));
    when(mockConfGetter.getGlobalConf(eq(GlobalConfKeys.destroyServerCommandTimeout)))
        .thenReturn(Duration.ofMinutes(10));
    when(mockConfGetter.getGlobalConf(eq(GlobalConfKeys.acceptableClockSkewWaitEnabled)))
        .thenReturn(true);
    when(mockConfGetter.getGlobalConf(eq(GlobalConfKeys.waitForClockSyncMaxAcceptableClockSkew)))
        .thenReturn(Duration.ofMillis(500));
    when(mockConfGetter.getGlobalConf(eq(GlobalConfKeys.waitForClockSyncTimeout)))
        .thenReturn(Duration.ofSeconds(300));

    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.notifyPeerOnRemoval)))
        .thenReturn(true);
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.notifyPeerOnRemoval)))
        .thenReturn(true);
    when(mockConfGetter.getConfForScope(any(Customer.class), eq(CustomerConfKeys.enableIMDSv2)))
        .thenReturn(false);
    when(mockConfGetter.getConfForScope(any(Customer.class), eq(CustomerConfKeys.cloudEnabled)))
        .thenReturn(false);
  }

  private String getMountPoints(AnsibleConfigureServers.Params taskParam) {
    if (taskParam.deviceInfo.mountPoints != null) {
      return taskParam.deviceInfo.mountPoints;
    } else if (taskParam.deviceInfo.numVolumes != null
        && !taskParam.getProvider().getCode().equals("onprem")) {
      List<String> mountPoints = new ArrayList<>();
      for (int i = 0; i < taskParam.deviceInfo.numVolumes; i++) {
        mountPoints.add("/mnt/d" + Integer.toString(i));
      }
      return String.join(",", mountPoints);
    }
    return null;
  }

  private Map<String, String> getExtraGflags(
      AnsibleConfigureServers.Params configureParams,
      NodeTaskParams params,
      UserIntent userIntent,
      TestData testData,
      boolean useHostname) {
    Map<String, String> gflags = new TreeMap<>();
    String processType = params.getProperty("processType");
    gflags.put("placement_cloud", configureParams.getProvider().getCode());
    gflags.put("placement_region", configureParams.getRegion().getCode());
    gflags.put("placement_zone", configureParams.getAZ().getCode());
    if (ServerType.MASTER.name().equals(processType)) {
      gflags.put("master_join_existing_universe", "true");
    }
    gflags.put("max_log_size", "256");
    if (ServerType.MASTER.name().equals(processType)) {
      gflags.put("undefok", "master_join_existing_universe,enable_ysql");
    } else {
      gflags.put("undefok", "enable_ysql");
    }
    gflags.put("placement_uuid", String.valueOf(configureParams.placementUuid));
    gflags.put("metric_node_name", configureParams.nodeName);

    String mount_points = getMountPoints(configureParams);
    if (mount_points != null) {
      gflags.put("fs_data_dirs", mount_points);
    }

    String index = testData.node.getDetails().nodeName.split("-n")[1];
    String private_ip = "10.0.0." + index;
    if (processType == null) {
      gflags.put("master_addresses", "");
    } else if (processType.equals(ServerType.TSERVER.name())) {
      if (useHostname) {
        gflags.put("server_broadcast_addresses", String.format("%s:%s", private_ip, "9100"));
      } else {
        gflags.put("server_broadcast_addresses", "");
      }
      gflags.put("rpc_bind_addresses", String.format("%s:%s", private_ip, "9100"));
      gflags.put("tserver_master_addrs", MASTER_ADDRESSES);
      gflags.put("webserver_port", "9000");
      gflags.put("webserver_interface", private_ip);
      gflags.put("redis_proxy_bind_address", String.format("%s:%s", private_ip, "6379"));

      if (userIntent.enableYEDIS) {
        gflags.put("redis_proxy_webserver_port", "11000");
      } else {
        gflags.put("start_redis_proxy", "false");
      }
    } else {
      if (useHostname) {
        gflags.put("server_broadcast_addresses", String.format("%s:%s", private_ip, "7100"));
      } else {
        gflags.put("server_broadcast_addresses", "");
      }
      if (!configureParams.isMasterInShellMode) {
        gflags.put("master_addresses", MASTER_ADDRESSES);
      } else {
        gflags.put("master_addresses", "");
      }
      gflags.put("rpc_bind_addresses", String.format("%s:%s", private_ip, "7100"));
      gflags.put("webserver_port", "7000");
      gflags.put("webserver_interface", private_ip);
    }

    String pgsqlProxyBindAddress = private_ip;
    String cqlProxyBindAddress = private_ip;
    if (useHostname) {
      pgsqlProxyBindAddress = "0.0.0.0";
      cqlProxyBindAddress = "0.0.0.0";
    }

    if (configureParams.enableYSQL) {
      gflags.put("enable_ysql", "true");
      gflags.put("pgsql_proxy_webserver_port", "13000");
      gflags.put(
          "pgsql_proxy_bind_address",
          String.format(
              "%s:%s",
              pgsqlProxyBindAddress,
              Universe.getOrBadRequest(configureParams.getUniverseUUID())
                  .getNode(configureParams.nodeName)
                  .ysqlServerRpcPort));
      if (configureParams.enableYSQLAuth) {
        gflags.put("ysql_enable_auth", "true");
        gflags.put("ysql_hba_conf_csv", "local all yugabyte trust");
      } else {
        gflags.put("ysql_enable_auth", "false");
      }
    } else {
      gflags.put("enable_ysql", "false");
    }
    if (configureParams.enableYCQL) {
      gflags.put("start_cql_proxy", "true");
      gflags.put("cql_proxy_webserver_port", "12000");
      gflags.put(
          "cql_proxy_bind_address",
          String.format(
              "%s:%s",
              cqlProxyBindAddress,
              Universe.getOrBadRequest(configureParams.getUniverseUUID())
                  .getNode(configureParams.nodeName)
                  .yqlServerRpcPort));
      if (configureParams.enableYCQLAuth) {
        gflags.put("use_cassandra_authentication", "true");
      } else {
        gflags.put("use_cassandra_authentication", "false");
      }
    } else {
      gflags.put("start_cql_proxy", "false");
    }

    gflags.put("cluster_uuid", String.valueOf(configureParams.getUniverseUUID()));
    if (configureParams.isMaster) {
      gflags.put("replication_factor", String.valueOf(userIntent.replicationFactor));
      gflags.put("load_balancer_initial_delay_secs", "480");
    }

    if (configureParams.currentClusterType == UniverseDefinitionTaskParams.ClusterType.PRIMARY
        && configureParams.setTxnTableWaitCountFlag) {
      gflags.put("txn_table_wait_min_ts_count", Integer.toString(userIntent.numNodes));
    }

    if (configureParams.callhomeLevel != null) {
      gflags.put(
          "callhome_collection_level", configureParams.callhomeLevel.toString().toLowerCase());
      if (configureParams.callhomeLevel.toString() == "NONE") {
        gflags.put("callhome_enabled", "false");
      }
    }

    String nodeToNodeString = String.valueOf(configureParams.enableNodeToNodeEncrypt);
    String clientToNodeString = String.valueOf(configureParams.enableClientToNodeEncrypt);
    String allowInsecureString = String.valueOf(configureParams.allowInsecure);
    String ybHomeDir = configureParams.getProvider().getYbHome();

    String certsDir = ybHomeDir + "/yugabyte-tls-config";
    String certsForClientDir = ybHomeDir + "/yugabyte-client-tls-config";

    gflags.put("use_node_to_node_encryption", nodeToNodeString);
    gflags.put("use_client_to_server_encryption", clientToNodeString);
    gflags.put("allow_insecure_connections", allowInsecureString);
    if (configureParams.enableClientToNodeEncrypt || configureParams.enableNodeToNodeEncrypt) {
      gflags.put(
          "cert_node_filename",
          Universe.getOrBadRequest(configureParams.getUniverseUUID())
              .getNode(configureParams.nodeName)
              .cloudInfo
              .private_ip);
    }
    if (EncryptionInTransitUtil.isRootCARequired(configureParams)) {
      gflags.put("certs_dir", certsDir);
    }
    if (EncryptionInTransitUtil.isClientRootCARequired(configureParams)) {
      gflags.put("certs_for_client_dir", certsForClientDir);
    }
    if (processType == ServerType.TSERVER.name()
        && runtimeConfigFactory
                .forUniverse(Universe.getOrBadRequest(configureParams.getUniverseUUID()))
                .getInt(NodeManager.POSTGRES_MAX_MEM_MB)
            > 0) {
      gflags.put("postmaster_cgroup", GFlagsUtil.YSQL_CGROUP_PATH);
    }
    return gflags;
  }

  private List<String> nodeCommand(
      NodeManager.NodeCommandType type, NodeTaskParams params, TestData testData, String nodeIp) {
    return nodeCommand(type, params, testData, new UserIntent(), nodeIp);
  }

  private void addInstanceTags(
      TestData testData,
      NodeTaskParams nodeTaskParam,
      Map<String, String> customTags,
      List<String> commandArgs) {
    if (testData.cloudType != Common.CloudType.onprem) {
      Map<String, String> instanceTags =
          (nodeTaskParam.clusters.isEmpty() || nodeTaskParam.clusters.get(0) == null)
              ? new TreeMap<>()
              : new TreeMap<>(nodeTaskParam.clusters.get(0).userIntent.instanceTags);
      if (customTags != null) {
        instanceTags.putAll(customTags);
      }
      addAdditionalInstanceTags(testData, nodeTaskParam, instanceTags);
      commandArgs.add("--instance_tags");
      commandArgs.add(Json.stringify(Json.toJson(instanceTags)));
    }
  }

  void addAdditionalInstanceTags(
      TestData testData, NodeTaskParams nodeTaskParam, Map<String, String> tags) {
    tags.put("customer-uuid", testData.customer.getUuid().toString());
    tags.put("universe-uuid", nodeTaskParam.getUniverseUUID().toString());
    tags.put("node-uuid", nodeTaskParam.nodeUuid.toString());
  }

  private List<String> nodeCommand(
      NodeManager.NodeCommandType type,
      NodeTaskParams params,
      TestData testData,
      UserIntent userIntent,
      String nodeIp) {
    return nodeCommand(type, params, testData, userIntent, nodeIp, false);
  }

  private List<String> nodeCommand(
      NodeManager.NodeCommandType type,
      NodeTaskParams params,
      TestData testData,
      UserIntent userIntent,
      String nodeIp,
      boolean canConfigureYbc) {
    Common.CloudType cloud = testData.cloudType;
    List<String> expectedCommand = new ArrayList<>();
    expectedCommand.add("instance");
    expectedCommand.add(type.toString().toLowerCase());
    switch (type) {
      case List:
        expectedCommand.add("--as_json");
        break;
      case Control:
        AnsibleClusterServerCtl.Params ctlParams = (AnsibleClusterServerCtl.Params) params;
        expectedCommand.add(ctlParams.process);
        expectedCommand.add(ctlParams.command);
        break;
      case Replace_Root_Volume:
        expectedCommand.add("--replacement_disk");
        expectedCommand.add(String.valueOf(testData.replacementVolume));
        if (cloud.equals(Common.CloudType.aws)) {
          expectedCommand.add("--root_device_name");
          expectedCommand.add(String.valueOf(testData.rootDeviceName));
        }
        break;
      case Create_Root_Volumes:
        CreateRootVolumes.Params crvParams = (CreateRootVolumes.Params) params;
        expectedCommand.add("--num_disks");
        expectedCommand.add(String.valueOf(crvParams.numVolumes));
        if (Common.CloudType.aws.equals(cloud)) {
          expectedCommand.add("--snapshot_creation_delay");
          expectedCommand.add("1");
          expectedCommand.add("--snapshot_creation_max_attempts");
          expectedCommand.add("1");
        }
        // intentional fall-thru
      case Create:
        AnsibleCreateServer.Params createParams = (AnsibleCreateServer.Params) params;
        if (!cloud.equals(Common.CloudType.onprem)) {
          expectedCommand.add("--instance_type");
          expectedCommand.add(createParams.instanceType);
          expectedCommand.add("--cloud_subnet");
          expectedCommand.add(createParams.subnetId);
          if (createParams.secondarySubnetId != null) {
            expectedCommand.add("--cloud_subnet_secondary");
            expectedCommand.add(createParams.secondarySubnetId);
          }
          String ybImage = testData.region.getYbImage();
          if (ybImage != null && !ybImage.isEmpty()) {
            expectedCommand.add("--machine_image");
            expectedCommand.add(ybImage);
          }
          if (createParams.assignPublicIP) {
            expectedCommand.add("--assign_public_ip");
          }
          addInstanceTags(testData, params, null, expectedCommand);
        }

        if (cloud.equals(Common.CloudType.aws)) {
          if (createParams.getCmkArn() != null) {
            expectedCommand.add("--cmk_res_name");
            expectedCommand.add(createParams.getCmkArn());
          }
          if (createParams.ipArnString != null) {
            expectedCommand.add("--iam_profile_arn");
            expectedCommand.add(createParams.ipArnString);
          }
        }
        break;
      case Provision:
        AnsibleSetupServer.Params setupParams = (AnsibleSetupServer.Params) params;
        if (cloud.equals(Common.CloudType.aws)) {
          expectedCommand.add("--instance_type");
          expectedCommand.add(setupParams.instanceType);
        }

        String ybImage = testData.region.getYbImage();
        if (ybImage != null && !ybImage.isEmpty()) {
          expectedCommand.add("--machine_image");
          expectedCommand.add(ybImage);
        }

        if ((cloud.equals(Common.CloudType.aws) || cloud.equals(Common.CloudType.gcp))
            && setupParams.useTimeSync) {
          expectedCommand.add("--use_chrony");
        }
        break;
      case Configure:
        AnsibleConfigureServers.Params configureParams = (AnsibleConfigureServers.Params) params;
        Map<String, String> gflags = new TreeMap<>(configureParams.gflags);

        expectedCommand.add("--master_addresses_for_tserver");
        expectedCommand.add(MASTER_ADDRESSES);
        expectedCommand.add("--num_cores_to_keep");
        expectedCommand.add("5");
        if (!configureParams.isMasterInShellMode) {
          expectedCommand.add("--master_addresses_for_master");
          expectedCommand.add(MASTER_ADDRESSES);
        }
        expectedCommand.add("--master_http_port");
        expectedCommand.add("7000");
        expectedCommand.add("--master_rpc_port");
        expectedCommand.add("7100");
        expectedCommand.add("--tserver_http_port");
        expectedCommand.add("9000");
        expectedCommand.add("--tserver_rpc_port");
        expectedCommand.add("9100");
        expectedCommand.add("--cql_proxy_rpc_port");
        expectedCommand.add("9042");
        expectedCommand.add("--redis_proxy_rpc_port");
        expectedCommand.add("6379");

        if (configureParams.ybSoftwareVersion != null) {
          expectedCommand.add("--package");
          expectedCommand.add("/yb/release.tar.gz");
        }
        Map<String, String> ybcFlags = new TreeMap<>();

        if (canConfigureYbc) {
          ybcFlags.put("v", "1");
          ybcFlags.put("server_address", nodeIp);
          ybcFlags.put("server_port", "18018");
          ybcFlags.put("yb_tserver_address", nodeIp);
          ybcFlags.put("log_dir", "/home/yugabyte/controller/logs");
          ybcFlags.put("yb_master_address", nodeIp);
          String nfsDirs =
              runtimeConfigFactory
                  .forUniverse(Universe.getOrBadRequest(configureParams.getUniverseUUID()))
                  .getString(NodeManager.YBC_NFS_DIRS);
          ybcFlags.put("nfs_dirs", nfsDirs);
        }

        // boolean useHostname = !NodeManager.isIpAddress(configureParams.nodeName);
        boolean useHostname = false;

        if (configureParams.getProperty("taskSubType") != null) {
          UpgradeTaskSubType taskSubType =
              UpgradeTaskParams.UpgradeTaskSubType.valueOf(
                  configureParams.getProperty("taskSubType"));
          String processType = configureParams.getProperty("processType");
          expectedCommand.add("--yb_process_type");
          expectedCommand.add(processType.toLowerCase());
          switch (taskSubType) {
            case Download:
              expectedCommand.add("--tags");
              expectedCommand.add("download-software");
              break;
            case Install:
              expectedCommand.add("--tags");
              expectedCommand.add("install-software");
              expectedCommand.add("--tags");
              expectedCommand.add("override_gflags");
              expectedCommand.add("--gflags");
              expectedCommand.add(Json.stringify(Json.toJson(gflags)));
              break;
            case None:
              break;
          }
        }

        if (configureParams.ybSoftwareVersion != null) {
          expectedCommand.add("--num_releases_to_keep");
          expectedCommand.add("3");
        }

        if (configureParams.type == Everything) {
          if ((configureParams.enableNodeToNodeEncrypt
              || configureParams.enableClientToNodeEncrypt)) {
            expectedCommand.addAll(
                getCertificatePaths(
                    userIntent, configureParams, configureParams.getProvider().getYbHome()));
          }
        } else if (configureParams.type == GFlags) {
          String processType = configureParams.getProperty("processType");
          expectedCommand.add("--yb_process_type");
          expectedCommand.add(processType.toLowerCase());
        } else if (configureParams.type == ToggleTls) {
          String nodeToNodeString = String.valueOf(configureParams.enableNodeToNodeEncrypt);
          String clientToNodeString = String.valueOf(configureParams.enableClientToNodeEncrypt);
          String allowInsecureString = String.valueOf(configureParams.allowInsecure);

          String ybHomeDir =
              Provider.getOrBadRequest(UUID.fromString(userIntent.provider)).getYbHome();
          String certsDir = ybHomeDir + "/yugabyte-tls-config";
          String certsForClientDir = ybHomeDir + "/yugabyte-client-tls-config";

          String subType = configureParams.getProperty("taskSubType");
          if (UpgradeTaskParams.UpgradeTaskSubType.CopyCerts.name().equals(subType)) {
            if (configureParams.enableNodeToNodeEncrypt
                || configureParams.enableClientToNodeEncrypt) {
              expectedCommand.add("--cert_rotate_action");
              expectedCommand.add(CertRotateAction.ROTATE_CERTS.toString());
            }
            if (configureParams.enableNodeToNodeEncrypt
                || (configureParams.rootAndClientRootCASame
                    && configureParams.enableClientToNodeEncrypt)) {
              gflags.put("certs_dir", certsDir);
              ybcFlags.put("certs_dir_name", certsDir);
            }
            if (configureParams.enableClientToNodeEncrypt) {
              gflags.put("certs_for_client_dir", certsForClientDir);
            }
            expectedCommand.addAll(getCertificatePaths(userIntent, configureParams, ybHomeDir));
          } else if (UpgradeTaskParams.UpgradeTaskSubType.Round1GFlagsUpdate.name()
              .equals(subType)) {
            gflags = new TreeMap<>(configureParams.gflags);
            if (configureParams.nodeToNodeChange > 0) {
              gflags.put("use_node_to_node_encryption", nodeToNodeString);
              gflags.put("use_client_to_server_encryption", clientToNodeString);
              gflags.put("allow_insecure_connections", "true");
              if (EncryptionInTransitUtil.isRootCARequired(configureParams)) {
                gflags.put("certs_dir", certsDir);
                ybcFlags.put("certs_dir_name", certsDir);
              }
              if (EncryptionInTransitUtil.isClientRootCARequired(configureParams)) {
                gflags.put("certs_for_client_dir", certsForClientDir);
              }
            } else if (configureParams.nodeToNodeChange < 0) {
              gflags.put("allow_insecure_connections", "true");
            } else {
              gflags.put("use_node_to_node_encryption", nodeToNodeString);
              gflags.put("use_client_to_server_encryption", clientToNodeString);
              gflags.put("allow_insecure_connections", allowInsecureString);
              if (EncryptionInTransitUtil.isRootCARequired(configureParams)) {
                gflags.put("certs_dir", certsDir);
                ybcFlags.put("certs_dir_name", certsDir);
              }
              if (EncryptionInTransitUtil.isClientRootCARequired(configureParams)) {
                gflags.put("certs_for_client_dir", certsForClientDir);
              }
            }
          } else if (UpgradeTaskParams.UpgradeTaskSubType.Round2GFlagsUpdate.name()
              .equals(subType)) {
            gflags = new TreeMap<>(configureParams.gflags);
            if (configureParams.nodeToNodeChange > 0) {
              gflags.put("allow_insecure_connections", allowInsecureString);
            } else if (configureParams.nodeToNodeChange < 0) {
              gflags.put("use_node_to_node_encryption", nodeToNodeString);
              gflags.put("use_client_to_server_encryption", clientToNodeString);
              gflags.put("allow_insecure_connections", allowInsecureString);
              if (EncryptionInTransitUtil.isRootCARequired(configureParams)) {
                gflags.put("certs_dir", certsDir);
                ybcFlags.put("certs_dir_name", certsDir);
              }
              if (EncryptionInTransitUtil.isClientRootCARequired(configureParams)) {
                gflags.put("certs_for_client_dir", certsForClientDir);
              }
            }
          }
        } else if (configureParams.type == Certs) {
          String processType = configureParams.getProperty("processType");
          expectedCommand.add("--yb_process_type");
          expectedCommand.add(processType.toLowerCase());

          String yb_home_dir =
              Provider.getOrBadRequest(UUID.fromString(userIntent.provider)).getYbHome();
          String certsNodeDir = yb_home_dir + "/yugabyte-tls-config";
          String certsForClientDir = yb_home_dir + "/yugabyte-client-tls-config";

          expectedCommand.add("--cert_rotate_action");
          expectedCommand.add(configureParams.certRotateAction.toString());

          CertificateInfo rootCert = null;
          if (configureParams.rootCA != null) {
            rootCert = CertificateInfo.get(configureParams.rootCA);
          }
          switch (configureParams.certRotateAction) {
            case APPEND_NEW_ROOT_CERT:
            case REMOVE_OLD_ROOT_CERT:
              {
                String rootCertPath = "";
                String certsLocation = "";
                if (rootCert.getCertType() == CertConfigType.SelfSigned) {
                  rootCertPath = rootCert.getCertificate();
                  certsLocation = NodeManager.CERT_LOCATION_PLATFORM;
                } else if (rootCert.getCertType() == CertConfigType.CustomCertHostPath) {
                  rootCertPath = rootCert.getCustomCertPathParams().rootCertPath;
                  certsLocation = NodeManager.CERT_LOCATION_NODE;
                } else if (rootCert.getCertType() == CertConfigType.HashicorpVault) {
                  rootCertPath = rootCert.getCertificate();
                  certsLocation = NodeManager.CERT_LOCATION_PLATFORM;
                }
                expectedCommand.add("--root_cert_path");
                expectedCommand.add(rootCertPath);
                expectedCommand.add("--certs_location");
                expectedCommand.add(certsLocation);
                expectedCommand.add("--certs_node_dir");
                expectedCommand.add(certsNodeDir);
              }
              break;
            case ROTATE_CERTS:
              expectedCommand.addAll(
                  getCertificatePaths(
                      userIntent,
                      configureParams,
                      configureParams.rootCARotationType != CertRotationType.None,
                      configureParams.clientRootCARotationType != CertRotationType.None,
                      yb_home_dir));
              break;
            case UPDATE_CERT_DIRS:
              {
                Map<String, String> gflagMap = new TreeMap<>();
                if (EncryptionInTransitUtil.isRootCARequired(configureParams)) {
                  gflagMap.put("certs_dir", certsNodeDir);
                  ybcFlags.put("certs_dir_name", certsNodeDir);
                }
                if (EncryptionInTransitUtil.isClientRootCARequired(configureParams)) {
                  gflagMap.put("certs_for_client_dir", certsForClientDir);
                }
                expectedCommand.add("--gflags");
                expectedCommand.add(Json.stringify(Json.toJson(gflagMap)));
                expectedCommand.add("--tags");
                expectedCommand.add("override_gflags");
                break;
              }
          }
        }

        if (configureParams.type != Everything
            && configureParams.type != Software
            && configureParams.type != Certs
            && !(configureParams.type == ToggleTls
                && configureParams.getProperty("taskSubType").equals("CopyCerts"))) {
          expectedCommand.add("--gflags");
          expectedCommand.add(Json.stringify(Json.toJson(gflags)));
          if (canConfigureYbc) {
            expectedCommand.add("--ybc_flags");
            expectedCommand.add(Json.stringify(Json.toJson(ybcFlags)));
          }

          expectedCommand.add("--tags");
          expectedCommand.add("override_gflags");
        }

        if (canConfigureYbc) {
          expectedCommand.add("--configure_ybc");
        }
        expectedCommand.add("--extra_gflags");
        expectedCommand.add(
            Json.stringify(
                Json.toJson(
                    getExtraGflags(configureParams, params, userIntent, testData, useHostname))));

        expectedCommand.add("--acceptable_clock_skew_wait_enabled");
        expectedCommand.add("--acceptable_clock_skew_sec");
        expectedCommand.add("0.500000000");
        expectedCommand.add("--acceptable_clock_skew_max_tries");
        expectedCommand.add("300");
        break;
      case Destroy:
        AnsibleDestroyServer.Params destroyParams = (AnsibleDestroyServer.Params) params;
        expectedCommand.add("--instance_type");
        expectedCommand.add(destroyParams.instanceType);
        expectedCommand.add("--node_ip");
        expectedCommand.add(destroyParams.nodeIP);
        expectedCommand.add("--node_uuid");
        expectedCommand.add(destroyParams.nodeUuid.toString());
        break;
      case Tags:
        InstanceActions.Params tagsParams = (InstanceActions.Params) params;
        if (Provider.InstanceTagsEnabledProviders.contains(cloud)) {
          addInstanceTags(testData, params, ImmutableMap.of("Cust", "Test"), expectedCommand);
          if (!tagsParams.deleteTags.isEmpty()) {
            expectedCommand.add("--remove_tags");
            expectedCommand.add(tagsParams.deleteTags);
          }
        }
        break;
      case Disk_Update:
        InstanceActions.Params duTaskParams = (InstanceActions.Params) params;
        expectedCommand.add("--instance_type");
        expectedCommand.add(duTaskParams.instanceType);
        break;
      case Change_Instance_Type:
        ChangeInstanceType.Params citTaskParams = (ChangeInstanceType.Params) params;
        expectedCommand.add("--instance_type");
        expectedCommand.add(citTaskParams.instanceType);
        expectedCommand.add("--pg_max_mem_mb");
        expectedCommand.add("0");
        break;
      case Transfer_XCluster_Certs:
        TransferXClusterCerts.Params txccTaskParams = (TransferXClusterCerts.Params) params;
        expectedCommand.add("--action");
        expectedCommand.add(txccTaskParams.action.toString());
        if (txccTaskParams.action == TransferXClusterCerts.Params.Action.COPY) {
          expectedCommand.add("--root_cert_path");
          expectedCommand.add(txccTaskParams.rootCertPath.toString());
        }
        expectedCommand.add("--replication_config_name");
        expectedCommand.add(txccTaskParams.replicationGroupName);
        if (txccTaskParams.producerCertsDirOnTarget != null) {
          expectedCommand.add("--producer_certs_dir");
          expectedCommand.add(txccTaskParams.producerCertsDirOnTarget.toString());
        }
        break;
      case Delete_Root_Volumes:
        if (Provider.InstanceTagsEnabledProviders.contains(cloud)) {
          addInstanceTags(testData, params, null, expectedCommand);
        }
        break;
    }
    if (params.deviceInfo != null) {
      DeviceInfo deviceInfo = params.deviceInfo;

      if (type != NodeManager.NodeCommandType.Replace_Root_Volume) {
        if (deviceInfo.numVolumes != null && !cloud.equals(Common.CloudType.onprem)) {
          expectedCommand.add("--num_volumes");
          expectedCommand.add(Integer.toString(deviceInfo.numVolumes));
        } else if (deviceInfo.mountPoints != null) {
          expectedCommand.add("--mount_points");
          expectedCommand.add(fakeMountPaths);
        }
        if (deviceInfo.volumeSize != null) {
          expectedCommand.add("--volume_size");
          expectedCommand.add(Integer.toString(deviceInfo.volumeSize));
        }
        if (type == NodeManager.NodeCommandType.Disk_Update) {
          if (deviceInfo.diskIops != null) {
            expectedCommand.add("--disk_iops");
            expectedCommand.add(Integer.toString(deviceInfo.diskIops));
          }
          if (deviceInfo.throughput != null) {
            expectedCommand.add("--disk_throughput");
            expectedCommand.add(Integer.toString(deviceInfo.throughput));
          }
        }
      }

      if (type == NodeManager.NodeCommandType.Provision
          || type == NodeManager.NodeCommandType.Create_Root_Volumes
          || type == NodeManager.NodeCommandType.Create) {
        if (deviceInfo.storageType != null) {
          if (type == NodeManager.NodeCommandType.Create
              || type == NodeManager.NodeCommandType.Create_Root_Volumes) {
            expectedCommand.add("--volume_type");
            expectedCommand.add(deviceInfo.storageType.toString().toLowerCase());
            if (deviceInfo.diskIops != null && deviceInfo.storageType.isIopsProvisioning()) {
              expectedCommand.add("--disk_iops");
              expectedCommand.add(Integer.toString(deviceInfo.diskIops));
            }
            if (deviceInfo.throughput != null
                && deviceInfo.storageType.isThroughputProvisioning()) {
              expectedCommand.add("--disk_throughput");
              expectedCommand.add(Integer.toString(deviceInfo.throughput));
            }
          }
          if (type == NodeManager.NodeCommandType.Provision && cloud.equals(Common.CloudType.gcp)) {
            expectedCommand.add("--volume_type");
            expectedCommand.add(deviceInfo.storageType.toString().toLowerCase());
          }
        }
        if (type == NodeManager.NodeCommandType.Provision) {
          String packagePath = mockConfig.getString("yb.thirdparty.packagePath");
          if (packagePath != null) {
            expectedCommand.add("--local_package_path");
            expectedCommand.add(packagePath);
          }
          expectedCommand.add("--pg_max_mem_mb");
          expectedCommand.add(
              Integer.toString(
                  runtimeConfigFactory
                      .forUniverse(Universe.getOrBadRequest(params.getUniverseUUID()))
                      .getInt(NodeManager.POSTGRES_MAX_MEM_MB)));
        }
      }
    }
    if (type == NodeManager.NodeCommandType.Create) {
      expectedCommand.add("--as_json");
    }
    expectedCommand.add("--remote_tmp_dir");
    expectedCommand.add("/tmp");
    expectedCommand.add(params.nodeName);
    return expectedCommand;
  }

  @Test
  public void testChangeInstanceTypeCommand() {
    int idx = 0;
    for (TestData t : testData) {
      ChangeInstanceType.Params params = new ChangeInstanceType.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().getUniverseUUID(), ApiUtils.mockUniverseUpdater(t.cloudType)));
      List<String> expectedCommand = t.baseCommand;
      params.instanceType = t.newInstanceTypeCode;
      expectedCommand.addAll(
          nodeCommand(NodeManager.NodeCommandType.Change_Instance_Type, params, t, NODE_IPS[idx]));
      reset(shellProcessHandler);
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Change_Instance_Type, params);
      verify(shellProcessHandler, times(1))
          .run(eq(expectedCommand), any(ShellProcessContext.class));
      idx++;
    }
  }

  @Test
  @Parameters({"true, false", "false, false", "true, true", "false, true"})
  @TestCaseName("{method}(isCopy:{0},isCustomProducerCertsDir:{1}) [{index}]")
  public void testTransferXClusterCertsCommand(boolean isCopy, boolean isCustomProducerCertsDir) {
    int idx = 0;
    for (TestData t : testData) {
      TransferXClusterCerts.Params params = new TransferXClusterCerts.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().getUniverseUUID(), ApiUtils.mockUniverseUpdater(t.cloudType)));
      List<String> expectedCommand = t.baseCommand;
      if (isCopy) {
        params.action = TransferXClusterCerts.Params.Action.COPY;
        params.rootCertPath = new File("rootCertPath");
      } else {
        params.action = TransferXClusterCerts.Params.Action.REMOVE;
      }
      params.replicationGroupName = "universe-uuid_MyRepl1";
      if (isCustomProducerCertsDir) {
        params.producerCertsDirOnTarget = new File("custom-producer-dir");
      }
      expectedCommand.addAll(
          nodeCommand(
              NodeManager.NodeCommandType.Transfer_XCluster_Certs, params, t, NODE_IPS[idx]));
      reset(shellProcessHandler);
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Transfer_XCluster_Certs, params);
      verify(shellProcessHandler, times(1))
          .run(eq(expectedCommand), any(ShellProcessContext.class));
      idx++;
    }
  }

  private void runAndVerifyNodeCommand(
      TestData t, NodeTaskParams params, NodeManager.NodeCommandType cmdType, String nodeIp) {
    List<String> expectedCommand = t.baseCommand;
    expectedCommand.addAll(nodeCommand(cmdType, params, t, nodeIp));

    nodeManager.nodeCommand(cmdType, params);
    verify(shellProcessHandler, times(1)).run(eq(expectedCommand), any(ShellProcessContext.class));
  }

  @Test
  public void testCreateRootVolumesCommand() {
    when(mockConfGetter.getGlobalConf(GlobalConfKeys.snapshotCreationDelay)).thenReturn(1);
    when(mockConfGetter.getGlobalConf(GlobalConfKeys.snapshotCreationMaxAttempts)).thenReturn(1);
    int idx = 0;
    for (TestData t : testData) {
      CreateRootVolumes.Params params = new CreateRootVolumes.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().getUniverseUUID(), ApiUtils.mockUniverseUpdater(t.cloudType)));
      addValidDeviceInfo(t, params);
      params.subnetId = t.zone.getSubnet();
      params.numVolumes = 1;

      runAndVerifyNodeCommand(
          t, params, NodeManager.NodeCommandType.Create_Root_Volumes, NODE_IPS[idx]);
      idx++;
    }
  }

  @Test
  public void testReplaceRootVolumeCommand() {
    int idx = 0;
    for (TestData t : testData) {
      ReplaceRootVolume.Params params = new ReplaceRootVolume.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().getUniverseUUID(), ApiUtils.mockUniverseUpdater(t.cloudType)));
      addValidDeviceInfo(t, params);
      params.replacementDisk = t.replacementVolume;
      if (t.cloudType.equals(Common.CloudType.aws)) {
        params.rootDeviceName = t.rootDeviceName;
      }

      runAndVerifyNodeCommand(
          t, params, NodeManager.NodeCommandType.Replace_Root_Volume, NODE_IPS[idx]);
      idx++;
    }
  }

  @Test
  public void testProvisionNodeCommand() {
    int idx = 0;
    for (TestData t : testData) {
      AnsibleSetupServer.Params params = new AnsibleSetupServer.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().getUniverseUUID(), ApiUtils.mockUniverseUpdater(t.cloudType)));
      addValidDeviceInfo(t, params);
      params.subnetId = t.zone.getSubnet();

      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(
          nodeCommand(NodeManager.NodeCommandType.Provision, params, t, NODE_IPS[idx]));
      reset(shellProcessHandler);
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Provision, params);
      verify(shellProcessHandler, times(1))
          .run(eq(expectedCommand), any(ShellProcessContext.class));
      idx++;
    }
  }

  @Test
  public void testCreateNodeCommandSecondarySubnet() {
    int idx = 0;
    when(mockConfig.getBoolean("yb.cloud.enabled")).thenReturn(true);
    when(mockConfGetter.getConfForScope(any(Customer.class), eq(CustomerConfKeys.cloudEnabled)))
        .thenReturn(false);
    for (TestData t : testData) {
      AnsibleCreateServer.Params params = new AnsibleCreateServer.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().getUniverseUUID(), ApiUtils.mockUniverseUpdater(t.cloudType)));
      addValidDeviceInfo(t, params);
      params.subnetId = t.zone.getSubnet();
      params.secondarySubnetId = "testSubnet";

      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(
          nodeCommand(NodeManager.NodeCommandType.Create, params, t, NODE_IPS[idx]));
      reset(shellProcessHandler);
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Create, params);
      verify(shellProcessHandler, times(1))
          .run(eq(expectedCommand), any(ShellProcessContext.class));
      idx++;
    }
  }

  @Test
  public void testProvisionNodeCommandWithLocalPackage() {
    String packagePath = "/tmp/third-party";
    new File(packagePath).mkdir();
    when(mockConfig.getString("yb.thirdparty.packagePath")).thenReturn(packagePath);
    int idx = 0;
    for (TestData t : testData) {
      AnsibleSetupServer.Params params = new AnsibleSetupServer.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().getUniverseUUID(), ApiUtils.mockUniverseUpdater(t.cloudType)));
      addValidDeviceInfo(t, params);
      params.subnetId = t.zone.getSubnet();

      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(
          nodeCommand(NodeManager.NodeCommandType.Provision, params, t, NODE_IPS[idx]));
      reset(shellProcessHandler);
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Provision, params);
      verify(shellProcessHandler, times(1))
          .run(eq(expectedCommand), any(ShellProcessContext.class));
      idx++;
    }

    File file = new File(packagePath);
    file.delete();
  }

  @Test
  public void testProvisionUseTimeSync() {
    for (TestData t : testData) {
      for (boolean useTimeSync : ImmutableList.of(true, false)) {
        AnsibleSetupServer.Params params = new AnsibleSetupServer.Params();
        buildValidParams(
            t,
            params,
            Universe.saveDetails(
                createUniverse().getUniverseUUID(), ApiUtils.mockUniverseUpdater(t.cloudType)));
        addValidDeviceInfo(t, params);
        params.useTimeSync = useTimeSync;
        reset(shellProcessHandler);
        ArgumentCaptor<List> arg = ArgumentCaptor.forClass(List.class);
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Provision, params);
        verify(shellProcessHandler, times(1)).run(arg.capture(), any(ShellProcessContext.class));
        // For AWS and useTimeSync knob set to true, we want to find the flag.
        List<String> cmdArgs = arg.getValue();
        assertNotNull(cmdArgs);
        assertTrue(
            cmdArgs.contains("--use_chrony")
                == ((t.cloudType.equals(Common.CloudType.aws)
                        || t.cloudType.equals(Common.CloudType.gcp))
                    && useTimeSync));
      }
    }
  }

  @Test
  public void testProvisionWithAWSTags() {
    int idx = 0;
    for (TestData t : testData) {
      AnsibleSetupServer.Params params = new AnsibleSetupServer.Params();
      UUID univUUID = createUniverse().getUniverseUUID();
      Universe universe = Universe.saveDetails(univUUID, ApiUtils.mockUniverseUpdater(t.cloudType));
      buildValidParams(t, params, universe);
      addValidDeviceInfo(t, params);
      if (t.cloudType.equals(Common.CloudType.aws)) {
        ApiUtils.insertInstanceTags(univUUID);
        setInstanceTags(params);
      }

      ArrayList<String> expectedCommandArrayList = new ArrayList<>();
      expectedCommandArrayList.addAll(t.baseCommand);
      expectedCommandArrayList.addAll(
          nodeCommand(NodeManager.NodeCommandType.Provision, params, t, NODE_IPS[idx]));
      reset(shellProcessHandler);
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Provision, params);
      verify(shellProcessHandler, times(1))
          .run(eq(expectedCommandArrayList), any(ShellProcessContext.class));
      idx++;
    }
  }

  private void setInstanceTags(NodeTaskParams params) {
    Map<String, String> instanceTags = ImmutableMap.of("Cust", "Test");
    if (params instanceof InstanceActions.Params) {
      ((InstanceActions.Params) params).tags = instanceTags;
    } else {
      UserIntent userIntent = new UserIntent();
      userIntent.instanceTags = instanceTags;
      params.clusters.add(new Cluster(ClusterType.PRIMARY, userIntent));
    }
  }

  private void runAndTestProvisionWithAccessKeyAndSG(String sgId) {
    int idx = 0;
    for (TestData t : testData) {
      t.region.setSecurityGroupId(sgId);
      t.region.update();
      // Create AccessKey
      AccessKey.KeyInfo keyInfo = new AccessKey.KeyInfo();
      keyInfo.privateKey = "/path/to/private.key";
      keyInfo.publicKey = "/path/to/public.key";
      keyInfo.vaultFile = "/path/to/vault_file";
      keyInfo.vaultPasswordFile = "/path/to/vault_password";
      getOrCreate(t.provider.getUuid(), "demo-access", keyInfo);

      t.provider.getDetails().sshPort = 3333;
      t.provider.getDetails().airGapInstall = true;
      t.provider.save();

      // Set up task params
      UniverseDefinitionTaskParams.UserIntent userIntent =
          new UniverseDefinitionTaskParams.UserIntent();
      userIntent.numNodes = 3;
      userIntent.accessKeyCode = "demo-access";
      userIntent.regionList = new ArrayList<UUID>();
      userIntent.regionList.add(t.region.getUuid());
      userIntent.providerType = t.cloudType;
      AnsibleSetupServer.Params params = new AnsibleSetupServer.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().getUniverseUUID(), ApiUtils.mockUniverseUpdater(userIntent)));
      addValidDeviceInfo(t, params);

      // Set up expected command
      int accessKeyIndexOffset = 9;
      if (t.cloudType.equals(Common.CloudType.aws)
          && params.deviceInfo.storageType.equals(PublicCloudConstants.StorageType.IO1)) {
        accessKeyIndexOffset += 2;
      }

      List<String> expectedCommand = new ArrayList<>(t.baseCommand);
      expectedCommand.addAll(
          nodeCommand(NodeManager.NodeCommandType.Provision, params, t, NODE_IPS[idx]));
      List<String> accessKeyCommands =
          new ArrayList<>(
              ImmutableList.of(
                  "--vars_file",
                  "/path/to/vault_file",
                  "--vault_password_file",
                  "/path/to/vault_password",
                  "--private_key_file",
                  "/path/to/private.key"));
      if (t.cloudType.equals(Common.CloudType.aws)) {
        accessKeyCommands.add("--key_pair_name");
        accessKeyCommands.add(userIntent.accessKeyCode);
        // String customSecurityGroupId = t.region.getSecurityGroupId();
        // if (customSecurityGroupId != null) {
        // accessKeyCommands.add("--security_group_id");
        // accessKeyCommands.add(customSecurityGroupId);
        // }
      }
      accessKeyCommands.add("--custom_ssh_port");
      accessKeyCommands.add("3333");
      accessKeyCommands.add("--air_gap");
      accessKeyCommands.add("--install_node_exporter");
      accessKeyCommands.add("--node_exporter_port");
      accessKeyCommands.add("9300");
      accessKeyCommands.add("--node_exporter_user");
      accessKeyCommands.add("prometheus");
      expectedCommand.addAll(expectedCommand.size() - accessKeyIndexOffset, accessKeyCommands);
      reset(shellProcessHandler);
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Provision, params);
      verify(shellProcessHandler, times(1))
          .run(eq(expectedCommand), any(ShellProcessContext.class));
      idx++;
    }
  }

  @Test
  public void testProvisionNodeCommandWithAccessKeyNoSG() {
    String sgId = "custom_sg_id";
    runAndTestProvisionWithAccessKeyAndSG(sgId);
  }

  @Test
  public void testProvisionNodeCommandWithAccessKeyCustomSG() {
    String sgId = null;
    runAndTestProvisionWithAccessKeyAndSG(sgId);
  }

  @Test
  public void testProvisionNodeCommandWithInvalidParam() {
    for (TestData t : testData) {
      try {
        NodeTaskParams params = new NodeTaskParams();
        buildValidParams(
            t,
            params,
            Universe.saveDetails(
                createUniverse().getUniverseUUID(), ApiUtils.mockUniverseUpdater(t.cloudType)));
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Provision, params);
        fail();
      } catch (RuntimeException re) {
        assertThat(re.getMessage(), is("NodeTaskParams is not AnsibleSetupServer.Params"));
      }
    }
  }

  @Test
  public void testCreateNodeCommand() {
    int idx = 0;
    for (TestData t : testData) {
      AnsibleCreateServer.Params params = new AnsibleCreateServer.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().getUniverseUUID(), ApiUtils.mockUniverseUpdater(t.cloudType)));
      addValidDeviceInfo(t, params);
      params.subnetId = t.zone.getSubnet();

      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(
          nodeCommand(NodeManager.NodeCommandType.Create, params, t, NODE_IPS[idx]));
      reset(shellProcessHandler);
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Create, params);
      verify(shellProcessHandler, times(1))
          .run(eq(expectedCommand), any(ShellProcessContext.class));
      idx++;
    }
  }

  @Test
  public void testCreateNodeCommandWithoutAssignPublicIP() {
    int idx = 0;
    for (TestData t : testData) {
      AnsibleCreateServer.Params params = new AnsibleCreateServer.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().getUniverseUUID(), ApiUtils.mockUniverseUpdater(t.cloudType)));
      addValidDeviceInfo(t, params);
      params.subnetId = t.zone.getSubnet();
      if (t.cloudType.equals(Common.CloudType.aws)) {
        params.assignPublicIP = false;
      }

      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(
          nodeCommand(NodeManager.NodeCommandType.Create, params, t, NODE_IPS[idx]));
      if (t.cloudType.equals(Common.CloudType.aws)) {
        Predicate<String> stringPredicate = p -> p.equals("--assign_public_ip");
        expectedCommand.removeIf(stringPredicate);
      }
      reset(shellProcessHandler);
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Create, params);
      verify(shellProcessHandler, times(1))
          .run(eq(expectedCommand), any(ShellProcessContext.class));
      idx++;
    }
  }

  @Test
  public void testCreateNodeCommandWithLocalPackage() {
    String packagePath = "/tmp/third-party";
    new File(packagePath).mkdir();
    when(mockConfig.getString("yb.thirdparty.packagePath")).thenReturn(packagePath);
    int idx = 0;
    for (TestData t : testData) {
      AnsibleCreateServer.Params params = new AnsibleCreateServer.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().getUniverseUUID(), ApiUtils.mockUniverseUpdater(t.cloudType)));
      addValidDeviceInfo(t, params);
      params.subnetId = t.zone.getSubnet();

      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(
          nodeCommand(NodeManager.NodeCommandType.Create, params, t, NODE_IPS[idx]));
      reset(shellProcessHandler);
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Create, params);
      verify(shellProcessHandler, times(1))
          .run(eq(expectedCommand), any(ShellProcessContext.class));
      idx++;
    }

    File file = new File(packagePath);
    file.delete();
  }

  @Test
  public void testCreateWithAWSTags() {
    int idx = 0;
    for (TestData t : testData) {
      AnsibleCreateServer.Params params = new AnsibleCreateServer.Params();
      UUID univUUID = createUniverse().getUniverseUUID();
      Universe universe = Universe.saveDetails(univUUID, ApiUtils.mockUniverseUpdater(t.cloudType));
      buildValidParams(t, params, universe);
      addValidDeviceInfo(t, params);
      if (t.cloudType.equals(Common.CloudType.aws)) {
        ApiUtils.insertInstanceTags(univUUID);
        setInstanceTags(params);
      }

      ArrayList<String> expectedCommandArrayList = new ArrayList<>();
      expectedCommandArrayList.addAll(t.baseCommand);
      expectedCommandArrayList.addAll(
          nodeCommand(NodeManager.NodeCommandType.Create, params, t, NODE_IPS[idx]));
      reset(shellProcessHandler);
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Create, params);
      verify(shellProcessHandler, times(1))
          .run(eq(expectedCommandArrayList), any(ShellProcessContext.class));
      idx++;
    }
  }

  private void runAndTestCreateWithAccessKeyAndSG(String sgId) {
    int idx = 0;
    for (TestData t : testData) {
      t.region.setSecurityGroupId(sgId);
      t.region.update();
      // Create AccessKey
      AccessKey.KeyInfo keyInfo = new AccessKey.KeyInfo();
      keyInfo.privateKey = "/path/to/private.key";
      keyInfo.publicKey = "/path/to/public.key";
      keyInfo.vaultFile = "/path/to/vault_file";
      keyInfo.vaultPasswordFile = "/path/to/vault_password";
      getOrCreate(t.provider.getUuid(), "demo-access", keyInfo);

      t.provider.getDetails().sshPort = 3333;
      t.provider.getDetails().airGapInstall = true;
      t.provider.save();

      // Set up task params
      UniverseDefinitionTaskParams.UserIntent userIntent =
          new UniverseDefinitionTaskParams.UserIntent();
      userIntent.numNodes = 3;
      userIntent.accessKeyCode = "demo-access";
      userIntent.regionList = new ArrayList<>();
      userIntent.regionList.add(t.region.getUuid());
      userIntent.providerType = t.cloudType;
      AnsibleCreateServer.Params params = new AnsibleCreateServer.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().getUniverseUUID(), ApiUtils.mockUniverseUpdater(userIntent)));
      addValidDeviceInfo(t, params);

      // Set up expected command
      int accessKeyIndexOffset = 8;
      if (t.cloudType.equals(Common.CloudType.aws)) {
        accessKeyIndexOffset += 2;
        if (params.deviceInfo.storageType.equals(PublicCloudConstants.StorageType.IO1)) {
          accessKeyIndexOffset += 2;
        }
      }

      List<String> expectedCommand = new ArrayList<>(t.baseCommand);
      expectedCommand.addAll(
          nodeCommand(NodeManager.NodeCommandType.Create, params, t, NODE_IPS[idx]));
      List<String> accessKeyCommands =
          new ArrayList<>(
              ImmutableList.of(
                  "--vars_file",
                  "/path/to/vault_file",
                  "--vault_password_file",
                  "/path/to/vault_password",
                  "--private_key_file",
                  "/path/to/private.key"));
      if (t.cloudType.equals(Common.CloudType.aws)) {
        accessKeyCommands.add("--key_pair_name");
        accessKeyCommands.add(userIntent.accessKeyCode);
        String customSecurityGroupId = t.region.getSecurityGroupId();
        if (customSecurityGroupId != null) {
          accessKeyCommands.add("--security_group_id");
          accessKeyCommands.add(customSecurityGroupId);
        }
      }
      accessKeyCommands.add("--custom_ssh_port");
      accessKeyCommands.add("3333");
      expectedCommand.addAll(expectedCommand.size() - accessKeyIndexOffset, accessKeyCommands);
      reset(shellProcessHandler);
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Create, params);
      verify(shellProcessHandler, times(1))
          .run(eq(expectedCommand), any(ShellProcessContext.class));
      idx++;
    }
  }

  @Test
  public void testCreateNodeCommandWithAccessKeyNoSG() {
    String sgId = null;
    runAndTestCreateWithAccessKeyAndSG(sgId);
  }

  @Test
  public void testCreateNodeCommandWithAccessKeyCustomSG() {
    String sgId = "custom_sg_id";
    runAndTestCreateWithAccessKeyAndSG(sgId);
  }

  @Test
  public void testCreateNodeCommandWithCMK() {
    int idx = 0;
    for (TestData t : testData) {
      AnsibleCreateServer.Params params = new AnsibleCreateServer.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().getUniverseUUID(), ApiUtils.mockUniverseUpdater(t.cloudType)));
      addValidDeviceInfo(t, params);
      params.subnetId = t.zone.getSubnet();
      if (t.cloudType.equals(Common.CloudType.aws)) {
        params.setCmkArn("cmkArn");
      }

      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(
          nodeCommand(NodeManager.NodeCommandType.Create, params, t, NODE_IPS[idx]));
      reset(shellProcessHandler);
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Create, params);
      verify(shellProcessHandler, times(1))
          .run(eq(expectedCommand), any(ShellProcessContext.class));
      idx++;
    }
  }

  @Test
  public void testCreateNodeCommandWithIAM() {
    int idx = 0;
    for (TestData t : testData) {
      AnsibleCreateServer.Params params = new AnsibleCreateServer.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().getUniverseUUID(), ApiUtils.mockUniverseUpdater(t.cloudType)));
      addValidDeviceInfo(t, params);
      params.subnetId = t.zone.getSubnet();
      if (t.cloudType.equals(Common.CloudType.aws)) {
        params.ipArnString = "ipArnString";
      }

      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(
          nodeCommand(NodeManager.NodeCommandType.Create, params, t, NODE_IPS[idx]));
      reset(shellProcessHandler);
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Create, params);
      verify(shellProcessHandler, times(1))
          .run(eq(expectedCommand), any(ShellProcessContext.class));
      idx++;
    }
  }

  @Test
  public void testCreateNodeCommandWithInvalidParam() {
    for (TestData t : testData) {
      try {
        NodeTaskParams params = new NodeTaskParams();
        buildValidParams(
            t,
            params,
            Universe.saveDetails(
                createUniverse().getUniverseUUID(), ApiUtils.mockUniverseUpdater(t.cloudType)));
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Create, params);
        fail();
      } catch (RuntimeException re) {
        assertThat(re.getMessage(), is("NodeTaskParams is not AnsibleCreateServer.Params"));
      }
    }
  }

  @Test
  public void testConfigureNodeCommandWithInvalidParam() {
    for (TestData t : testData) {
      try {
        NodeTaskParams params = new NodeTaskParams();
        buildValidParams(
            t,
            params,
            Universe.saveDetails(
                createUniverse().getUniverseUUID(), ApiUtils.mockUniverseUpdater(t.cloudType)));
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
        fail();
      } catch (RuntimeException re) {
        assertThat(re.getMessage(), is("NodeTaskParams is not AnsibleConfigureServers.Params"));
      }
    }
  }

  @Test
  public void testConfigureNodeCommand() {
    int idx = 0;
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().getUniverseUUID(), ApiUtils.mockUniverseUpdater(t.cloudType)));
      addValidDeviceInfo(t, params);
      params.isMasterInShellMode = true;
      params.ybSoftwareVersion = "0.0.1";
      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(
          nodeCommand(NodeManager.NodeCommandType.Configure, params, t, NODE_IPS[idx]));
      reset(shellProcessHandler);
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      verify(shellProcessHandler, times(1))
          .run(eq(expectedCommand), any(ShellProcessContext.class));
      idx++;
    }
  }

  @Test
  public void testConfigureNodeCommandWithRf1() {
    int idx = 0;
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().getUniverseUUID(), ApiUtils.mockUniverseUpdater(t.cloudType)));
      addValidDeviceInfo(t, params);
      params.isMasterInShellMode = true;
      params.ybSoftwareVersion = "0.0.1";
      List<String> expectedCommand = t.baseCommand;
      UserIntent userIntent = new UserIntent();
      userIntent.replicationFactor = 1;
      expectedCommand.addAll(
          nodeCommand(NodeManager.NodeCommandType.Configure, params, t, userIntent, NODE_IPS[idx]));
      reset(shellProcessHandler);
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      verify(shellProcessHandler, times(1))
          .run(eq(expectedCommand), any(ShellProcessContext.class));
      idx++;
    }
  }

  @Test
  public void testConfigureNodeCommandWithoutReleasePackage() {
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().getUniverseUUID(), ApiUtils.mockUniverseUpdater(t.cloudType)));
      params.isMasterInShellMode = false;
      params.ybSoftwareVersion = "0.0.2";
      try {
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
        fail();
      } catch (RuntimeException re) {
        assertThat(
            re.getMessage(),
            allOf(notNullValue(), is("Unable to fetch yugabyte release for version: 0.0.2")));
      }
    }
  }

  @Test
  public void testConfigureNodeCommandWithAccessKey() {
    int idx = 0;
    for (TestData t : testData) {
      // Create AccessKey
      AccessKey.KeyInfo keyInfo = new AccessKey.KeyInfo();
      keyInfo.privateKey = "/path/to/private.key";
      keyInfo.publicKey = "/path/to/public.key";
      keyInfo.vaultFile = "/path/to/vault_file";
      keyInfo.vaultPasswordFile = "/path/to/vault_password";
      getOrCreate(t.provider.getUuid(), "demo-access", keyInfo);

      t.provider.getDetails().sshPort = 3333;
      t.provider.save();

      // Set up task params
      UniverseDefinitionTaskParams.UserIntent userIntent =
          new UniverseDefinitionTaskParams.UserIntent();
      userIntent.numNodes = 3;
      userIntent.accessKeyCode = "demo-access";
      userIntent.regionList = new ArrayList<>();
      userIntent.regionList.add(t.region.getUuid());
      userIntent.providerType = t.cloudType;
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().getUniverseUUID(),
              ApiUtils.mockUniverseUpdater(userIntent, true /* setMasters */)));
      addValidDeviceInfo(t, params);
      params.isMasterInShellMode = true;
      params.ybSoftwareVersion = "0.0.1";

      // Set up expected command
      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(
          nodeCommand(NodeManager.NodeCommandType.Configure, params, t, NODE_IPS[idx]));
      List<String> accessKeyCommand =
          ImmutableList.of(
              "--vars_file",
              "/path/to/vault_file",
              "--vault_password_file",
              "/path/to/vault_password",
              "--private_key_file",
              "/path/to/private.key",
              "--custom_ssh_port",
              "3333");
      expectedCommand.addAll(expectedCommand.size() - 7, accessKeyCommand);
      reset(shellProcessHandler);
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      verify(shellProcessHandler, times(1))
          .run(eq(expectedCommand), any(ShellProcessContext.class));
      idx++;
    }
  }

  @Test
  public void testConfigureNodeCommandWithSetTxnTableWaitCountFlag() {
    int idx = 0;
    for (TestData t : testData) {
      // Set up TaskParams
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().getUniverseUUID(), ApiUtils.mockUniverseUpdater(t.cloudType)));
      addValidDeviceInfo(t, params);
      params.ybSoftwareVersion = "0.0.1";
      params.setTxnTableWaitCountFlag = true;

      // Set up UserIntent
      UserIntent userIntent = new UserIntent();
      userIntent.numNodes = 3;

      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(
          nodeCommand(NodeManager.NodeCommandType.Configure, params, t, userIntent, NODE_IPS[idx]));
      reset(shellProcessHandler);
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      verify(shellProcessHandler, times(1))
          .run(eq(expectedCommand), any(ShellProcessContext.class));
      idx++;
    }
  }

  @Test
  public void testSoftwareUpgradeWithoutProcessType() {
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().getUniverseUUID(), ApiUtils.mockUniverseUpdater(t.cloudType)));
      params.type = Software;
      params.ybSoftwareVersion = "0.0.1";

      try {
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
        fail();
      } catch (RuntimeException re) {
        assertThat(re.getMessage(), allOf(notNullValue(), is("Invalid processType: null")));
      }
    }
  }

  @Test
  public void testSoftwareUpgradeWithInvalidProcessType() {
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().getUniverseUUID(), ApiUtils.mockUniverseUpdater(t.cloudType)));
      params.type = Software;
      params.ybSoftwareVersion = "0.0.1";

      for (UniverseTaskBase.ServerType type : UniverseTaskBase.ServerType.values()) {
        try {
          // master and tserver are valid process types.
          if (ImmutableList.of(MASTER, TSERVER, CONTROLLER).contains(type)) {
            continue;
          }
          params.setProperty("processType", type.toString());
          nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
          fail();
        } catch (RuntimeException re) {
          assertThat(
              re.getMessage(), allOf(notNullValue(), is("Invalid processType: " + type.name())));
        }
      }
    }
  }

  @Test
  public void testSoftwareUpgradeWithoutTaskType() {
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().getUniverseUUID(), ApiUtils.mockUniverseUpdater(t.cloudType)));
      params.type = Software;
      params.ybSoftwareVersion = "0.0.1";
      params.setProperty("processType", MASTER.toString());

      try {
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
        fail();
      } catch (RuntimeException re) {
        assertThat(
            re.getMessage(), allOf(notNullValue(), is("Invalid taskSubType property: null")));
      }
    }
  }

  @Test
  public void testSoftwareUpgradeWithDownloadNodeCommand() {
    int idx = 0;
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().getUniverseUUID(), ApiUtils.mockUniverseUpdater(t.cloudType)));
      params.type = Software;
      params.ybSoftwareVersion = "0.0.1";
      params.isMasterInShellMode = true;
      params.setProperty("taskSubType", Download.toString());
      params.setProperty("processType", MASTER.toString());
      params.deviceInfo = new DeviceInfo();
      params.deviceInfo.numVolumes = 1;
      if (t.cloudType == CloudType.onprem) {
        params.deviceInfo.mountPoints = fakeMountPaths;
      }
      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(
          nodeCommand(NodeManager.NodeCommandType.Configure, params, t, NODE_IPS[idx]));
      reset(shellProcessHandler);
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      verify(shellProcessHandler, times(1))
          .run(eq(expectedCommand), any(ShellProcessContext.class));
      idx++;
    }
  }

  @Test
  public void testSoftwareUpgradeWithInstallNodeCommand() {
    int idx = 0;
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().getUniverseUUID(), ApiUtils.mockUniverseUpdater(t.cloudType)));
      params.type = Software;
      params.ybSoftwareVersion = "0.0.1";
      params.isMasterInShellMode = true;
      params.setProperty("taskSubType", Install.toString());
      params.setProperty("processType", MASTER.toString());
      params.deviceInfo = new DeviceInfo();
      params.deviceInfo.numVolumes = 1;
      if (t.cloudType == CloudType.onprem) {
        params.deviceInfo.mountPoints = fakeMountPaths;
      }
      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(
          nodeCommand(NodeManager.NodeCommandType.Configure, params, t, NODE_IPS[idx]));
      reset(shellProcessHandler);
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      verify(shellProcessHandler, times(1))
          .run(eq(expectedCommand), any(ShellProcessContext.class));
      idx++;
    }
  }

  @Test
  public void testSoftwareUpgradeWithoutReleasePackage() {
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().getUniverseUUID(), ApiUtils.mockUniverseUpdater(t.cloudType)));
      params.type = Software;
      params.ybSoftwareVersion = "0.0.2";
      params.isMasterInShellMode = true;
      params.setProperty("taskSubType", Install.toString());
      params.setProperty("processType", MASTER.toString());
      try {
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
        fail();
      } catch (RuntimeException re) {
        assertThat(
            re.getMessage(),
            allOf(notNullValue(), is("Unable to fetch yugabyte release for version: 0.0.2")));
      }
    }
  }

  @Test
  public void testGFlagRemove() {
    int idx = 0;
    for (TestData t : testData) {
      for (String serverType : ImmutableList.of(MASTER.toString(), TSERVER.toString())) {
        AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
        buildValidParams(
            t,
            params,
            Universe.saveDetails(
                createUniverse().getUniverseUUID(), ApiUtils.mockUniverseUpdater(t.cloudType)));
        Set<String> gflagsToRemove = new HashSet<>();
        gflagsToRemove.add("flag1");
        gflagsToRemove.add("flag2");
        params.type = GFlags;
        params.isMasterInShellMode = true;
        params.gflagsToRemove = gflagsToRemove;
        params.setProperty("processType", serverType);
        params.deviceInfo = new DeviceInfo();
        params.deviceInfo.numVolumes = 1;
        if (t.cloudType == CloudType.onprem) {
          params.deviceInfo.mountPoints = fakeMountPaths;
        }

        List<String> expectedCommand = new ArrayList<>(t.baseCommand);
        expectedCommand.addAll(
            nodeCommand(NodeManager.NodeCommandType.Configure, params, t, NODE_IPS[idx]));
        reset(shellProcessHandler);
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
        verify(shellProcessHandler, times(1))
            .run(eq(expectedCommand), any(ShellProcessContext.class));
      }
      idx++;
    }
  }

  @Test
  public void testGFlagPreprocess() {
    when(runtimeConfigFactory.forUniverse(any())).thenReturn(mockConfig);
    for (TestData t : testData) {
      for (String serverType : ImmutableList.of(MASTER.toString(), TSERVER.toString())) {
        AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
        buildValidParams(
            t,
            params,
            Universe.saveDetails(
                createUniverse().getUniverseUUID(), ApiUtils.mockUniverseUpdater(t.cloudType)));
        params.type = GFlags;
        params.enableYSQL = true;
        params.enableYSQLAuth = true;
        params.isMasterInShellMode = true;
        params.setProperty("processType", serverType);
        params.gflags.put(GFlagsUtil.FS_DATA_DIRS, "/some/other"); // will be removed on conflict
        params.gflags.put(
            GFlagsUtil.YSQL_HBA_CONF_CSV,
            "host all all ::1/128 trust,"
                + "\"local adb=\"\"cc,bb,aa\"\" bda=\"\"bb,aa,cc\"\" \","
                + "\"host all all jwt jwks={\"a\": \"b\"} jwt_audiences=\"yb,yb2\""
                + " jwt_issuers=\"\"issuers\"\"\","
                + "host all all jwt jwks={\"c\": \"d\"} jwt_audiences=\"yb1\""
                + " jwt_issuers=\"issuers1,issuers2\","
                + "hostgssenc all all jwt jwks={\"c\": \"d\"} jwt_audiences=\"yb1\""
                + " jwt_issuers=\"\"issuers1\"\","
                + "host all all all trust,"
                + "hostssl all all jwt jwks={\"c\": \"d\"} jwt_audiences=\"yb1,yb\""
                + " jwt_issuers=\"issuers1,issuers2,issuers3\""); // will be merged
        params.gflags.put(GFlagsUtil.UNDEFOK, "use_private_ip"); // will be merged
        params.gflags.put(GFlagsUtil.CSQL_PROXY_BIND_ADDRESS, "0.0.0.0:1990"); // port replace
        params.gflags.put(GFlagsUtil.PSQL_PROXY_BIND_ADDRESS, "0.1.2.3"); // port append
        params.gflags.put(GFlagsUtil.MAX_LOG_SIZE, "65000"); // left as it is

        params.deviceInfo = new DeviceInfo();
        params.deviceInfo.numVolumes = 1;
        params.deviceInfo.mountPoints = fakeMountPaths;

        when(mockConfig.getBoolean("yb.cloud.enabled")).thenReturn(true);
        when(mockConfGetter.getConfForScope(any(Customer.class), eq(CustomerConfKeys.cloudEnabled)))
            .thenReturn(true);
        reset(shellProcessHandler);
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);

        when(mockConfig.getBoolean("yb.cloud.enabled")).thenReturn(false);
        when(mockConfGetter.getConfForScope(any(Customer.class), eq(CustomerConfKeys.cloudEnabled)))
            .thenReturn(false);
        when(mockConfig.getBoolean("yb.gflags.allow_user_override")).thenReturn(false);
        when(mockConfGetter.getConfForScope(
                any(Universe.class), eq(UniverseConfKeys.gflagsAllowUserOverride)))
            .thenReturn(false);
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);

        when(mockConfig.getBoolean("yb.gflags.allow_user_override")).thenReturn(true);
        when(mockConfGetter.getConfForScope(
                any(Universe.class), eq(UniverseConfKeys.gflagsAllowUserOverride)))
            .thenReturn(true);
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);

        verify(shellProcessHandler, times(3)).run(captor.capture(), any(ShellProcessContext.class));

        Map<String, String> gflagsProcessed = extractGFlags(captor.getAllValues().get(1));
        Map<String, String> copy = new TreeMap<>(params.gflags);
        copy.put(GFlagsUtil.UNDEFOK, "use_private_ip,master_join_existing_universe,enable_ysql");
        String jwksFileName1 = "";
        String jwksFileName2 = "";
        try {
          jwksFileName1 = FileUtils.computeHashForAFile("{\"a\": \"b\"}", 10);
          jwksFileName2 = FileUtils.computeHashForAFile("{\"c\": \"d\"}", 10);
          jwksFileName1 = "/home/yugabyte" + GFlagsUtil.GFLAG_REMOTE_FILES_PATH + jwksFileName1;
          jwksFileName2 = "/home/yugabyte" + GFlagsUtil.GFLAG_REMOTE_FILES_PATH + jwksFileName2;
        } catch (NoSuchAlgorithmException e) {
          throw new RuntimeException("Error generating fileName " + e.getMessage());
        }
        copy.put(
            GFlagsUtil.YSQL_HBA_CONF_CSV,
            String.format(
                "host all all ::1/128 trust,"
                    + "\"local adb=\"\"cc,bb,aa\"\" bda=\"\"bb,aa,cc\"\" \","
                    + "\"host all all jwt jwt_jwks_path=\"\"%s\"\" jwt_audiences=\"\"yb,yb2\"\""
                    + " jwt_issuers=\"\"issuers\"\"\","
                    + "\"host all all jwt jwt_jwks_path=\"\"%s\"\" jwt_audiences=\"\"yb1\"\""
                    + " jwt_issuers=\"\"issuers1,issuers2\"\"\","
                    + "\"hostgssenc all all jwt jwt_jwks_path=\"\"%s\"\" jwt_audiences=\"\"yb1\"\""
                    + " jwt_issuers=\"\"issuers1\"\"\","
                    + "host all all all trust,"
                    + "\"hostssl all all jwt jwt_jwks_path=\"\"%s\"\" jwt_audiences=\"\"yb1,yb\"\""
                    + " jwt_issuers=\"\"issuers1,issuers2,issuers3\"\"\","
                    + "local all yugabyte trust",
                jwksFileName1, jwksFileName2, jwksFileName2, jwksFileName2));
        copy.remove(GFlagsUtil.FS_DATA_DIRS);
        copy.put(GFlagsUtil.CSQL_PROXY_BIND_ADDRESS, "0.0.0.0:9042");
        copy.put(GFlagsUtil.PSQL_PROXY_BIND_ADDRESS, "0.1.2.3:5433");
        assertEquals(copy, new TreeMap<>(gflagsProcessed));

        Map<String, String> gflagsNotFiltered = extractGFlags(captor.getAllValues().get(2));
        Map<String, String> copy2 = new TreeMap<>(params.gflags);
        copy2.put(GFlagsUtil.UNDEFOK, "use_private_ip,master_join_existing_universe,enable_ysql");
        copy2.put(
            GFlagsUtil.YSQL_HBA_CONF_CSV,
            String.format(
                "host all all ::1/128 trust,"
                    + "\"local adb=\"\"cc,bb,aa\"\" bda=\"\"bb,aa,cc\"\" \","
                    + "\"host all all jwt jwt_jwks_path=\"\"%s\"\" jwt_audiences=\"\"yb,yb2\"\""
                    + " jwt_issuers=\"\"issuers\"\"\","
                    + "\"host all all jwt jwt_jwks_path=\"\"%s\"\" jwt_audiences=\"\"yb1\"\""
                    + " jwt_issuers=\"\"issuers1,issuers2\"\"\","
                    + "\"hostgssenc all all jwt jwt_jwks_path=\"\"%s\"\" jwt_audiences=\"\"yb1\"\""
                    + " jwt_issuers=\"\"issuers1\"\"\","
                    + "host all all all trust,"
                    + "\"hostssl all all jwt jwt_jwks_path=\"\"%s\"\" jwt_audiences=\"\"yb1,yb\"\""
                    + " jwt_issuers=\"\"issuers1,issuers2,issuers3\"\"\","
                    + "local all yugabyte trust",
                jwksFileName1, jwksFileName2, jwksFileName2, jwksFileName2));
        copy2.put(GFlagsUtil.CSQL_PROXY_BIND_ADDRESS, "0.0.0.0:9042");
        copy2.put(GFlagsUtil.PSQL_PROXY_BIND_ADDRESS, "0.1.2.3:5433");
        assertEquals(copy2, new TreeMap<>(gflagsNotFiltered));

        Map<String, String> cloudGflags = extractGFlags(captor.getAllValues().get(0));
        assertEquals(copy2, new TreeMap<>(cloudGflags)); // Non filtered as well
      }
    }
  }

  private Map<String, String> extractGFlags(List<String> command) {
    String gflagsJson = LocalNodeManager.convertCommandArgListToMap(command).get("--gflags");
    if (gflagsJson == null) {
      throw new IllegalStateException("Empty gflags for " + command);
    }
    return Json.fromJson(Json.parse(gflagsJson), Map.class);
  }

  @Test
  public void testGFlagsUpgradeNullProcessType() {
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().getUniverseUUID(), ApiUtils.mockUniverseUpdater(t.cloudType)));
      HashMap<String, String> gflags = new HashMap<>();
      gflags.put("gflagName", "gflagValue");
      params.gflags = gflags;
      params.type = GFlags;
      params.isMasterInShellMode = true;

      try {
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
        fail();
      } catch (RuntimeException re) {
        assertThat(re.getMessage(), allOf(notNullValue(), is("Invalid processType: null")));
      }
    }
  }

  @Test
  public void testGFlagsUpgradeInvalidProcessType() {
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().getUniverseUUID(), ApiUtils.mockUniverseUpdater(t.cloudType)));
      HashMap<String, String> gflags = new HashMap<>();
      gflags.put("gflagName", "gflagValue");
      params.gflags = gflags;
      params.type = GFlags;
      params.isMasterInShellMode = true;

      for (UniverseTaskBase.ServerType type : UniverseTaskBase.ServerType.values()) {
        try {
          // master and tserver are valid process types.
          if (ImmutableList.of(MASTER, TSERVER, CONTROLLER).contains(type)) {
            continue;
          }
          params.setProperty("processType", type.toString());
          nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
          fail();
        } catch (RuntimeException re) {
          assertThat(
              re.getMessage(), allOf(notNullValue(), is("Invalid processType: " + type.name())));
        }
      }
    }
  }

  @Test
  public void testGFlagsUpgradeForMasterNodeCommand() {
    int idx = 0;
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().getUniverseUUID(), ApiUtils.mockUniverseUpdater(t.cloudType)));
      HashMap<String, String> gflags = new HashMap<>();
      gflags.put("gflagName", "gflagValue");
      params.gflags = gflags;
      params.type = GFlags;
      params.isMasterInShellMode = true;
      params.setProperty("processType", MASTER.toString());
      params.deviceInfo = new DeviceInfo();
      params.deviceInfo.numVolumes = 1;
      if (t.cloudType == CloudType.onprem) {
        params.deviceInfo.mountPoints = fakeMountPaths;
      }
      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(
          nodeCommand(NodeManager.NodeCommandType.Configure, params, t, NODE_IPS[idx]));
      reset(shellProcessHandler);
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      verify(shellProcessHandler, times(1))
          .run(eq(expectedCommand), any(ShellProcessContext.class));
      idx++;
    }
  }

  @Test
  @Parameters({"true, false", "false, true", "true, true"})
  @TestCaseName("{method}(enabledYSQL:{0},enabledYCQL:{1}) [{index}]")
  public void testEnableYSQLNodeCommand(boolean ysqlEnabled, boolean ycqlEnabled) {
    int idx = 0;
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().getUniverseUUID(), ApiUtils.mockUniverseUpdater(t.cloudType)));
      params.type = Everything;
      params.ybSoftwareVersion = "0.0.1";
      params.enableYSQL = ysqlEnabled;
      params.enableYCQL = ycqlEnabled;
      params.deviceInfo = new DeviceInfo();
      params.deviceInfo.numVolumes = 1;
      if (t.cloudType == CloudType.onprem) {
        params.deviceInfo.mountPoints = fakeMountPaths;
      }
      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(
          nodeCommand(NodeManager.NodeCommandType.Configure, params, t, NODE_IPS[idx]));
      reset(shellProcessHandler);
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      verify(shellProcessHandler, times(1))
          .run(eq(expectedCommand), any(ShellProcessContext.class));
      idx++;
    }
  }

  @Test
  public void testEnableNodeToNodeTLSNodeCommand() throws IOException, NoSuchAlgorithmException {
    int idx = 0;
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      params.nodeName = t.node.getNodeName();
      params.type = Everything;
      params.ybSoftwareVersion = "0.0.1";
      params.enableNodeToNodeEncrypt = true;
      params.allowInsecure = false;
      params.rootCA = createUniverseWithCert(t, params);
      params.deviceInfo = new DeviceInfo();
      params.deviceInfo.numVolumes = 1;
      if (t.cloudType == CloudType.onprem) {
        params.deviceInfo.mountPoints = fakeMountPaths;
      }
      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(
          nodeCommand(NodeManager.NodeCommandType.Configure, params, t, NODE_IPS[idx]));
      reset(shellProcessHandler);
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      verify(shellProcessHandler, times(1)).run(captor.capture(), contextCaptor.capture());
      contextCaptor
          .getValue()
          .getDescription()
          .equals(
              String.format(
                  "bin/ybcloud.sh %s --region %s instance configure %s",
                  t.region.getProvider().getName(), t.region.getCode(), t.node.getNodeName()));
      List<String> actualCommand = captor.getValue();
      int serverCertPathIndex = expectedCommand.indexOf("--server_cert_path") + 1;
      int serverKeyPathIndex = expectedCommand.indexOf("--server_key_path") + 1;
      expectedCommand.set(serverCertPathIndex, actualCommand.get(serverCertPathIndex));
      expectedCommand.set(serverKeyPathIndex, actualCommand.get(serverKeyPathIndex));
      assertEquals(expectedCommand, actualCommand);
      idx++;
    }
  }

  @Test
  public void testCustomCertNodeCommand() throws IOException, NoSuchAlgorithmException {
    Customer customer = ModelFactory.testCustomer();
    ModelFactory.newProvider(customer, Common.CloudType.onprem);
    int idx = 0;
    for (TestData t : testData) {
      if (t.cloudType == Common.CloudType.onprem) {
        t.privateKey = null;
      }
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      params.nodeName = t.node.getNodeName();
      params.type = Everything;
      params.ybSoftwareVersion = "0.0.1";
      params.enableNodeToNodeEncrypt = true;
      params.allowInsecure = false;
      params.rootCA = createUniverseWithCert(t, params);
      params.deviceInfo = new DeviceInfo();
      params.deviceInfo.numVolumes = 1;
      if (t.cloudType == CloudType.onprem) {
        params.deviceInfo.mountPoints = fakeMountPaths;
      }
      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(
          nodeCommand(NodeManager.NodeCommandType.Configure, params, t, NODE_IPS[idx]));
      reset(shellProcessHandler);
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      verify(shellProcessHandler, times(1)).run(captor.capture(), contextCaptor.capture());
      contextCaptor
          .getValue()
          .getDescription()
          .equals(
              String.format(
                  "bin/ybcloud.sh %s --region %s instance configure %s",
                  t.region.getProvider().getName(), t.region.getCode(), t.node.getNodeName()));
      List<String> actualCommand = captor.getValue();
      int serverCertPathIndex = expectedCommand.indexOf("--server_cert_path") + 1;
      int serverKeyPathIndex = expectedCommand.indexOf("--server_key_path") + 1;
      expectedCommand.set(serverCertPathIndex, actualCommand.get(serverCertPathIndex));
      expectedCommand.set(serverKeyPathIndex, actualCommand.get(serverKeyPathIndex));
      assertEquals(expectedCommand, actualCommand);
      idx++;
    }
  }

  @Test
  public void testEnableClientToNodeTLSNodeCommand() throws IOException, NoSuchAlgorithmException {
    int idx = 0;
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().getUniverseUUID(), ApiUtils.mockUniverseUpdater(t.cloudType)));
      params.type = Everything;
      params.ybSoftwareVersion = "0.0.1";
      params.enableClientToNodeEncrypt = true;
      params.allowInsecure = false;
      params.rootCA = createUniverseWithCert(t, params);
      params.setClientRootCA(params.rootCA);
      params.deviceInfo = new DeviceInfo();
      params.deviceInfo.numVolumes = 1;
      if (t.cloudType == CloudType.onprem) {
        params.deviceInfo.mountPoints = fakeMountPaths;
      }
      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(
          nodeCommand(NodeManager.NodeCommandType.Configure, params, t, NODE_IPS[idx]));
      reset(shellProcessHandler);
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      verify(shellProcessHandler, times(1)).run(captor.capture(), contextCaptor.capture());
      contextCaptor
          .getValue()
          .getDescription()
          .equals(
              String.format(
                  "bin/ybcloud.sh %s --region %s instance configure %s",
                  t.region.getProvider().getName(), t.region.getCode(), t.node.getNodeName()));
      List<String> actualCommand = captor.getValue();
      int serverCertPathIndex = expectedCommand.indexOf("--server_cert_path") + 1;
      int serverKeyPathIndex = expectedCommand.indexOf("--server_key_path") + 1;
      expectedCommand.set(serverCertPathIndex, actualCommand.get(serverCertPathIndex));
      expectedCommand.set(serverKeyPathIndex, actualCommand.get(serverKeyPathIndex));
      checkEquality(expectedCommand, actualCommand);
      idx++;
    }
  }

  // /temp/root.crt

  @Captor private ArgumentCaptor<List<String>> captor;
  @Captor private ArgumentCaptor<ShellProcessContext> contextCaptor;

  @Test
  public void testEnableAllTLSNodeCommand() throws IOException, NoSuchAlgorithmException {
    int idx = 0;
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().getUniverseUUID(), ApiUtils.mockUniverseUpdater(t.cloudType)));
      params.type = Everything;
      params.ybSoftwareVersion = "0.0.1";
      params.enableNodeToNodeEncrypt = true;
      params.enableClientToNodeEncrypt = true;
      params.allowInsecure = false;
      params.rootCA = createUniverseWithCert(t, params);
      params.setClientRootCA(params.rootCA);
      params.deviceInfo = new DeviceInfo();
      params.deviceInfo.numVolumes = 1;
      if (t.cloudType == CloudType.onprem) {
        params.deviceInfo.mountPoints = fakeMountPaths;
      }
      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(
          nodeCommand(NodeManager.NodeCommandType.Configure, params, t, NODE_IPS[idx]));
      reset(shellProcessHandler);
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      verify(shellProcessHandler, times(1)).run(captor.capture(), contextCaptor.capture());
      contextCaptor
          .getValue()
          .getDescription()
          .equals(
              String.format(
                  "bin/ybcloud.sh %s --region %s instance configure %s",
                  t.region.getProvider().getName(), t.region.getCode(), t.node.getNodeName()));
      List<String> actualCommand = captor.getValue();
      int serverCertPathIndex = expectedCommand.indexOf("--server_cert_path") + 1;
      int serverKeyPathIndex = expectedCommand.indexOf("--server_key_path") + 1;
      expectedCommand.set(serverCertPathIndex, actualCommand.get(serverCertPathIndex));
      expectedCommand.set(serverKeyPathIndex, actualCommand.get(serverKeyPathIndex));
      checkEquality(expectedCommand, actualCommand);
      idx++;
    }
  }

  // checks for presence of same options (starting with --) in the commands and
  // makes sure values are not empty. Cert names are random temporary files,
  // expecting them to be same is impractical
  private void checkEquality(List<String> expectedCommand, List<String> actualCommand) {
    if (expectedCommand != null
        && actualCommand != null
        && expectedCommand.size() == actualCommand.size()) {
      for (int i = 0; i < expectedCommand.size(); i++) {
        String expected = expectedCommand.get(i);
        String actual = actualCommand.get(i);
        if (expected != null
            && ((expected.startsWith("--") && expected.equals(actual))
                || !actual.trim().isEmpty())) {
          continue;
        }
        String message =
            String.format(
                "ybcloud.sh command mismatch. Expected => %s, Actual => %s",
                expectedCommand, actualCommand);
        Assert.fail(message);
      }
    }
  }

  @Test
  public void testGlobalDefaultCallhome() {
    int idx = 0;
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().getUniverseUUID(), ApiUtils.mockUniverseUpdater(t.cloudType)));
      params.type = Everything;
      params.ybSoftwareVersion = "0.0.1";
      params.callhomeLevel = CallHomeManager.CollectionLevel.valueOf("NONE");
      params.deviceInfo = new DeviceInfo();
      params.deviceInfo.numVolumes = 1;
      if (t.cloudType == CloudType.onprem) {
        params.deviceInfo.mountPoints = fakeMountPaths;
      }
      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(
          nodeCommand(NodeManager.NodeCommandType.Configure, params, t, NODE_IPS[idx]));
      reset(shellProcessHandler);
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      verify(shellProcessHandler, times(1))
          .run(eq(expectedCommand), any(ShellProcessContext.class));
      idx++;
    }
  }

  @Test
  public void testDestroyNodeCommandWithInvalidParam() {
    for (TestData t : testData) {
      try {
        NodeTaskParams params = new NodeTaskParams();
        buildValidParams(
            t,
            params,
            Universe.saveDetails(
                createUniverse().getUniverseUUID(), ApiUtils.mockUniverseUpdater(t.cloudType)));
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Destroy, params);
        fail();
      } catch (RuntimeException re) {
        assertThat(re.getMessage(), is("NodeTaskParams is not AnsibleDestroyServer.Params"));
      }
    }
  }

  @Test
  public void testDestroyNodeCommand() {
    int idx = 0;
    for (TestData t : testData) {
      AnsibleDestroyServer.Params params = new AnsibleDestroyServer.Params();
      params.nodeIP = "1.1.1.1";
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().getUniverseUUID(), ApiUtils.mockUniverseUpdater(t.cloudType)));
      assertNotNull(params.nodeUuid);
      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(
          nodeCommand(NodeManager.NodeCommandType.Destroy, params, t, NODE_IPS[idx]));
      reset(shellProcessHandler);
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Destroy, params);
      verify(shellProcessHandler, times(1))
          .run(eq(expectedCommand), any(ShellProcessContext.class));
      idx++;
    }
  }

  @Test
  public void testListNodeCommand() {
    int idx = 0;
    for (TestData t : testData) {
      NodeTaskParams params = new NodeTaskParams();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().getUniverseUUID(), ApiUtils.mockUniverseUpdater(t.cloudType)));

      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(
          nodeCommand(NodeManager.NodeCommandType.List, params, t, NODE_IPS[idx]));
      reset(shellProcessHandler);
      nodeManager.nodeCommand(NodeManager.NodeCommandType.List, params);
      verify(shellProcessHandler, times(1))
          .run(eq(expectedCommand), any(ShellProcessContext.class));
      idx++;
    }
  }

  @Test
  public void testControlNodeCommandWithInvalidParam() {
    for (TestData t : testData) {
      try {
        NodeTaskParams params = new NodeTaskParams();
        buildValidParams(
            t,
            params,
            Universe.saveDetails(
                createUniverse().getUniverseUUID(), ApiUtils.mockUniverseUpdater(t.cloudType)));
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Control, params);
        fail();
      } catch (RuntimeException re) {
        assertThat(re.getMessage(), is("NodeTaskParams is not AnsibleClusterServerCtl.Params"));
      }
    }
  }

  @Test
  public void testControlNodeCommand() {
    int idx = 0;
    for (TestData t : testData) {
      AnsibleClusterServerCtl.Params params = new AnsibleClusterServerCtl.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().getUniverseUUID(), ApiUtils.mockUniverseUpdater(t.cloudType)));
      params.process = "master";
      params.command = "create";

      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(
          nodeCommand(NodeManager.NodeCommandType.Control, params, t, NODE_IPS[idx]));
      reset(shellProcessHandler);
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Control, params);
      verify(shellProcessHandler, times(1))
          .run(eq(expectedCommand), any(ShellProcessContext.class));
      idx++;
    }
  }

  @Test
  public void testDockerNodeCommandWithoutDockerNetwork() {
    for (TestData t : testData) {
      NodeTaskParams params = new NodeTaskParams();
      buildValidParams(t, params, createUniverse());

      try {
        nodeManager.nodeCommand(NodeManager.NodeCommandType.List, params);
      } catch (RuntimeException re) {
        if (t.cloudType == Common.CloudType.docker) {
          assertThat(
              re.getMessage(),
              allOf(notNullValue(), is("yb.docker.network is not set in application.conf")));
        }
      }
    }
  }

  @Test
  public void testDockerNodeCommandWithDockerNetwork() {
    int idx = 0;
    for (TestData t : testData) {
      NodeTaskParams params = new NodeTaskParams();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().getUniverseUUID(), ApiUtils.mockUniverseUpdater(t.cloudType)));

      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(
          nodeCommand(NodeManager.NodeCommandType.List, params, t, NODE_IPS[idx]));
      reset(shellProcessHandler);
      nodeManager.nodeCommand(NodeManager.NodeCommandType.List, params);
      verify(shellProcessHandler, times(1))
          .run(eq(expectedCommand), any(ShellProcessContext.class));
      idx++;
    }
  }

  @Test
  public void testSetInstanceTags() {
    int idx = 0;
    for (TestData t : testData) {
      InstanceActions.Params params = new InstanceActions.Params();
      UUID univUUID = createUniverse().getUniverseUUID();
      Universe universe = Universe.saveDetails(univUUID, ApiUtils.mockUniverseUpdater(t.cloudType));
      buildValidParams(t, params, universe);
      ApiUtils.insertInstanceTags(univUUID);
      setInstanceTags(params);
      if (Provider.InstanceTagsEnabledProviders.contains(t.cloudType)) {
        List<String> expectedCommand = t.baseCommand;
        expectedCommand.addAll(
            nodeCommand(NodeManager.NodeCommandType.Tags, params, t, NODE_IPS[idx]));
        reset(shellProcessHandler);
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Tags, params);
        verify(shellProcessHandler, times(1))
            .run(eq(expectedCommand), any(ShellProcessContext.class));
      } else {
        assertFails(
            () -> nodeManager.nodeCommand(NodeManager.NodeCommandType.Tags, params),
            "Tags are unsupported for " + t.cloudType.name());
      }

      idx++;
    }
  }

  @Test
  public void testRemoveInstanceTags() {
    int idx = 0;
    for (TestData t : testData) {
      InstanceActions.Params params = new InstanceActions.Params();
      UUID univUUID = createUniverse().getUniverseUUID();
      Universe universe = Universe.saveDetails(univUUID, ApiUtils.mockUniverseUpdater(t.cloudType));
      buildValidParams(t, params, universe);
      ApiUtils.insertInstanceTags(univUUID);
      setInstanceTags(params);
      params.deleteTags = "Remove,Also";
      if (Provider.InstanceTagsEnabledProviders.contains(t.cloudType)) {
        List<String> expectedCommand = t.baseCommand;
        expectedCommand.addAll(
            nodeCommand(NodeManager.NodeCommandType.Tags, params, t, NODE_IPS[idx]));
        reset(shellProcessHandler);
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Tags, params);
        verify(shellProcessHandler, times(1))
            .run(eq(expectedCommand), any(ShellProcessContext.class));
      } else {
        assertFails(
            () -> nodeManager.nodeCommand(NodeManager.NodeCommandType.Tags, params),
            "Tags are unsupported for " + t.cloudType.name());
      }
      idx++;
    }
  }

  @Test
  public void testEmptyInstanceTags() {
    int idx = 0;
    for (TestData t : testData) {
      InstanceActions.Params params = new InstanceActions.Params();
      UUID univUUID = createUniverse().getUniverseUUID();
      Universe universe = Universe.saveDetails(univUUID, ApiUtils.mockUniverseUpdater(t.cloudType));
      buildValidParams(t, params, universe);
      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(
          nodeCommand(NodeManager.NodeCommandType.Tags, params, t, NODE_IPS[idx]));
      try {
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Tags, params);
        assertNotEquals(t.cloudType, Common.CloudType.aws);
      } catch (RuntimeException re) {
        if (t.cloudType == Common.CloudType.aws) {
          assertThat(
              re.getMessage(),
              allOf(notNullValue(), is("Invalid params: no tags to add or remove")));
        }
      }
      idx++;
    }
  }

  @Test
  public void testDiskUpdateCommand() {
    int idx = 0;
    for (TestData t : testData) {
      InstanceActions.Params params = new InstanceActions.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().getUniverseUUID(), ApiUtils.mockUniverseUpdater(t.cloudType)));
      addValidDeviceInfo(t, params);
      params.deviceInfo = new DeviceInfo();
      params.deviceInfo.numVolumes = 1;
      if (t.cloudType == CloudType.onprem) {
        params.deviceInfo.mountPoints = fakeMountPaths;
      }
      if (t.cloudType == CloudType.aws) {
        params.deviceInfo.storageType = PublicCloudConstants.StorageType.GP3;
        params.deviceInfo.diskIops = 5000;
        params.deviceInfo.throughput = 500;
      }
      params.deviceInfo.volumeSize = 500;
      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(
          nodeCommand(NodeManager.NodeCommandType.Disk_Update, params, t, NODE_IPS[idx]));
      reset(shellProcessHandler);
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Disk_Update, params);
      verify(shellProcessHandler, times(1))
          .run(eq(expectedCommand), any(ShellProcessContext.class));
      idx++;
    }
  }

  @Test
  public void testConfigureNodeCommandInShellMode() {
    int idx = 0;
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().getUniverseUUID(), ApiUtils.mockUniverseUpdater(t.cloudType)));
      params.isMasterInShellMode = false;
      params.ybSoftwareVersion = "0.0.1";
      params.deviceInfo = new DeviceInfo();
      params.deviceInfo.numVolumes = 1;
      if (t.cloudType == CloudType.onprem) {
        params.deviceInfo.mountPoints = fakeMountPaths;
      }

      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(
          nodeCommand(NodeManager.NodeCommandType.Configure, params, t, NODE_IPS[idx]));
      reset(shellProcessHandler);
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      verify(shellProcessHandler, times(1))
          .run(eq(expectedCommand), any(ShellProcessContext.class));
      idx++;
    }
  }

  @Test
  @Parameters({"true", "false"})
  public void testYEDISNodeCommand(boolean enableYEDIS) {
    int idx = 0;
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().getUniverseUUID(), ApiUtils.mockUniverseUpdater(t.cloudType)));
      params.type = Everything;
      params.ybSoftwareVersion = "0.0.1";
      params.enableYEDIS = enableYEDIS;
      params.deviceInfo = new DeviceInfo();
      params.deviceInfo.numVolumes = 1;
      if (t.cloudType == CloudType.onprem) {
        params.deviceInfo.mountPoints = fakeMountPaths;
      }
      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(
          nodeCommand(NodeManager.NodeCommandType.Configure, params, t, NODE_IPS[idx]));
      reset(shellProcessHandler);
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      verify(shellProcessHandler, times(1))
          .run(eq(expectedCommand), any(ShellProcessContext.class));
      idx++;
    }
  }

  private static NodeDetails createNode(boolean isMaster) {
    NodeDetails node = new NodeDetails();
    node.nodeName = "testNode";
    node.azUuid = UUID.randomUUID();
    node.isMaster = isMaster;
    node.cloudInfo = new CloudSpecificInfo();
    node.cloudInfo.private_ip = "192.168.0.1";
    return node;
  }

  @Test
  @Parameters({"MASTER, true", "MASTER, false", "TSERVER, true", "TSERVER, false"})
  @TestCaseName("testGFlagsEraseMastersWhenInShell_Type:{0}_InShell:{1}")
  public void testGFlagsEraseMastersWhenInShell(String serverType, boolean isMasterInShellMode) {
    int idx = 0;
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();

      Universe universe = createUniverse();
      universe.getUniverseDetails().nodeDetailsSet.add(createNode(true));
      assertFalse(StringUtils.isEmpty(universe.getMasterAddresses()));

      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              universe.getUniverseUUID(), ApiUtils.mockUniverseUpdater(t.cloudType)));
      addValidDeviceInfo(t, params);
      params.type = GFlags;
      params.isMasterInShellMode = isMasterInShellMode;
      params.updateMasterAddrsOnly = true;
      params.isMaster = serverType.equals(MASTER.toString());
      params.setProperty("processType", serverType);

      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(
          nodeCommand(NodeManager.NodeCommandType.Configure, params, t, NODE_IPS[idx]));
      reset(shellProcessHandler);
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      verify(shellProcessHandler, times(1))
          .run(eq(expectedCommand), any(ShellProcessContext.class));
      idx++;
    }
  }

  @Test
  public void testToggleTlsWithoutProcessType() {
    for (TestData data : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(
          data,
          params,
          Universe.saveDetails(
              createUniverse().getUniverseUUID(), ApiUtils.mockUniverseUpdater(data.cloudType)));
      params.type = ToggleTls;

      try {
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
        fail();
      } catch (RuntimeException re) {
        assertThat(re.getMessage(), allOf(notNullValue(), is("Invalid processType: null")));
      }
    }
  }

  @Test
  public void testToggleTlsWithInvalidProcessType() {
    for (TestData data : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(
          data,
          params,
          Universe.saveDetails(
              createUniverse().getUniverseUUID(), ApiUtils.mockUniverseUpdater(data.cloudType)));
      params.type = ToggleTls;

      for (UniverseTaskBase.ServerType type : UniverseTaskBase.ServerType.values()) {
        try {
          // Master and TServer are valid process types
          if (ImmutableList.of(MASTER, TSERVER, CONTROLLER).contains(type)) {
            continue;
          }
          params.setProperty("processType", type.toString());
          nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
          fail();
        } catch (RuntimeException re) {
          assertThat(
              re.getMessage(), allOf(notNullValue(), is("Invalid processType: " + type.name())));
        }
      }
    }
  }

  @Test
  public void testToggleTlsWithoutTaskType() {
    for (TestData data : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(
          data,
          params,
          Universe.saveDetails(
              createUniverse().getUniverseUUID(), ApiUtils.mockUniverseUpdater(data.cloudType)));
      params.type = ToggleTls;
      params.setProperty("processType", MASTER.toString());

      try {
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
        fail();
      } catch (RuntimeException re) {
        assertThat(
            re.getMessage(), allOf(notNullValue(), is("Invalid taskSubType property: null")));
      }
    }
  }

  @Test
  public void testToggleTlsCopyCertsWithInvalidRootCa() {
    for (TestData data : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(
          data,
          params,
          Universe.saveDetails(
              createUniverse().getUniverseUUID(), ApiUtils.mockUniverseUpdater(data.cloudType)));
      params.type = ToggleTls;
      params.setProperty("processType", MASTER.toString());
      params.setProperty("taskSubType", UpgradeTaskParams.UpgradeTaskSubType.CopyCerts.name());
      params.enableNodeToNodeEncrypt = true;
      params.enableClientToNodeEncrypt = true;
      params.rootCA = UUID.randomUUID();

      try {
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
        fail();
      } catch (RuntimeException re) {
        assertThat(re.getMessage(), allOf(notNullValue(), containsString("No valid rootCA")));
      }
    }
  }

  @Test
  @Parameters({"true, true", "true, false", "false, true", "false, false"})
  @TestCaseName("testToggleTlsCopyCertsWhenNodeToNode:{0}_ClientToNode:{1}")
  public void testToggleTlsCopyCertsWithParams(
      boolean enableNodeToNodeEncrypt, boolean enableClientToNodeEncrypt)
      throws IOException, NoSuchAlgorithmException {
    int idx = 0;
    for (TestData data : testData) {
      if (data.cloudType == Common.CloudType.gcp) {
        data.privateKey = null;
      }

      UUID universeUuid = createUniverse().getUniverseUUID();
      UserIntent userIntent =
          Universe.getOrBadRequest(universeUuid)
              .getUniverseDetails()
              .getPrimaryCluster()
              .userIntent;

      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(
          data,
          params,
          Universe.saveDetails(universeUuid, ApiUtils.mockUniverseUpdater(data.cloudType)));
      params.type = ToggleTls;
      params.setProperty("processType", MASTER.toString());
      params.setProperty("taskSubType", UpgradeTaskParams.UpgradeTaskSubType.CopyCerts.name());
      params.enableNodeToNodeEncrypt = enableNodeToNodeEncrypt;
      params.enableClientToNodeEncrypt = enableClientToNodeEncrypt;
      params.rootCA = createUniverseWithCert(data, params);
      params.deviceInfo = new DeviceInfo();
      params.deviceInfo.mountPoints = "/fake/path/d0,/fake/path/d1";
      params.setClientRootCA(params.rootCA);
      reset(shellProcessHandler);
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      List<String> expectedCommand = data.baseCommand;
      expectedCommand.addAll(
          nodeCommand(
              NodeManager.NodeCommandType.Configure, params, data, userIntent, NODE_IPS[idx]));
      verify(shellProcessHandler, times(1)).run(captor.capture(), contextCaptor.capture());
      contextCaptor
          .getValue()
          .getDescription()
          .equals(
              String.format(
                  "bin/ybcloud.sh %s --region %s instance configure %s",
                  data.region.getProvider().getName(),
                  data.region.getCode(),
                  data.node.getNodeName()));
      List<String> actualCommand = captor.getValue();
      int serverCertPathIndex = expectedCommand.indexOf("--server_cert_path") + 1;
      int serverKeyPathIndex = expectedCommand.indexOf("--server_key_path") + 1;
      expectedCommand.set(serverCertPathIndex, actualCommand.get(serverCertPathIndex));
      expectedCommand.set(serverKeyPathIndex, actualCommand.get(serverKeyPathIndex));
      int clientCertPathIndex = expectedCommand.indexOf("--server_cert_path_client_to_server") + 1;
      int clientKeyPathIndex = expectedCommand.indexOf("--server_key_path_client_to_server") + 1;
      expectedCommand.set(clientCertPathIndex, actualCommand.get(clientCertPathIndex));
      expectedCommand.set(clientKeyPathIndex, actualCommand.get(clientKeyPathIndex));
      assertEquals(expectedCommand, actualCommand);
      idx++;
    }
  }

  @Test
  @Parameters({"0", "-1", "1"})
  @TestCaseName("testToggleTlsRound1GFlagsUpdateWhenNodeToNodeChange:{0}")
  public void testToggleTlsRound1GFlagsUpdate(int nodeToNodeChange) {
    int idx = 0;
    for (TestData data : testData) {
      UUID universeUuid = createUniverse().getUniverseUUID();
      UserIntent userIntent =
          Universe.getOrBadRequest(universeUuid)
              .getUniverseDetails()
              .getPrimaryCluster()
              .userIntent;

      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(
          data,
          params,
          Universe.saveDetails(universeUuid, ApiUtils.mockUniverseUpdater(data.cloudType)));
      params.type = ToggleTls;
      params.setProperty("processType", MASTER.toString());
      params.setProperty(
          "taskSubType", UpgradeTaskParams.UpgradeTaskSubType.Round1GFlagsUpdate.name());
      params.nodeToNodeChange = nodeToNodeChange;
      params.deviceInfo = new DeviceInfo();
      params.deviceInfo.numVolumes = 1;
      if (data.cloudType == CloudType.onprem) {
        params.deviceInfo.mountPoints = fakeMountPaths;
      }
      reset(shellProcessHandler);
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      List<String> expectedCommand = data.baseCommand;
      expectedCommand.addAll(
          nodeCommand(
              NodeManager.NodeCommandType.Configure, params, data, userIntent, NODE_IPS[idx]));
      verify(shellProcessHandler, times(1))
          .run(eq(expectedCommand), any(ShellProcessContext.class));
      idx++;
    }
  }

  @Test
  @Parameters({"0", "-1", "1"})
  @TestCaseName("testToggleTlsRound2GFlagsUpdateWhenNodeToNodeChange:{0}")
  public void testToggleTlsRound2GFlagsUpdate(int nodeToNodeChange) {
    int idx = 0;
    for (TestData data : testData) {
      UUID universeUuid = createUniverse().getUniverseUUID();
      UserIntent userIntent =
          Universe.getOrBadRequest(universeUuid)
              .getUniverseDetails()
              .getPrimaryCluster()
              .userIntent;

      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(
          data,
          params,
          Universe.saveDetails(universeUuid, ApiUtils.mockUniverseUpdater(data.cloudType)));
      params.type = ToggleTls;
      params.setProperty("processType", MASTER.toString());
      params.setProperty(
          "taskSubType", UpgradeTaskParams.UpgradeTaskSubType.Round2GFlagsUpdate.name());
      params.nodeToNodeChange = nodeToNodeChange;
      params.deviceInfo = new DeviceInfo();
      params.deviceInfo.numVolumes = 1;
      if (data.cloudType == CloudType.onprem) {
        params.deviceInfo.mountPoints = fakeMountPaths;
      }
      reset(shellProcessHandler);
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      List<String> expectedCommand = data.baseCommand;
      expectedCommand.addAll(
          nodeCommand(
              NodeManager.NodeCommandType.Configure, params, data, userIntent, NODE_IPS[idx]));
      verify(shellProcessHandler, times(1))
          .run(eq(expectedCommand), any(ShellProcessContext.class));
      idx++;
    }
  }

  private UUID createCertificateConfig(CertConfigType certType, UUID customerUUID, String label)
      throws IOException, NoSuchAlgorithmException {
    return createCertificate(certType, customerUUID, label, "", false).getUuid();
  }

  private CertificateInfo createCertificate(
      CertConfigType certType,
      UUID customerUUID,
      String label,
      String suffix,
      boolean createClientPaths)
      throws IOException, NoSuchAlgorithmException {
    UUID certUUID = UUID.randomUUID();
    Calendar cal = Calendar.getInstance();
    Date today = cal.getTime();
    cal.add(Calendar.YEAR, 1);
    Date nextYear = cal.getTime();

    createTempFile("node_manager_test_ca.crt", "test data");

    if (certType == CertConfigType.SelfSigned) {
      when(mockConfig.getString("yb.storage.path")).thenReturn(TestHelper.TMP_PATH);
      certUUID = certificateHelper.createRootCA(mockConfig, "foobar", customerUUID);
      return CertificateInfo.get(certUUID);
    } else if (certType == CertConfigType.CustomCertHostPath) {
      CertificateParams.CustomCertInfo customCertInfo = new CertificateParams.CustomCertInfo();
      customCertInfo.rootCertPath = String.format("/path/to/cert%s.crt", suffix);
      customCertInfo.nodeCertPath = String.format("/path/to/rootcert%s.crt", suffix);
      customCertInfo.nodeKeyPath = String.format("/path/to/nodecert%s.key", suffix);
      if (createClientPaths) {
        customCertInfo.clientCertPath = String.format("/path/to/clientcert%s.crt", suffix);
        customCertInfo.clientKeyPath = String.format("/path/to/nodecert%s.crt", suffix);
      }
      return CertificateInfo.create(
          certUUID,
          customerUUID,
          label,
          today,
          nextYear,
          TestHelper.TMP_PATH + "/node_manager_test_ca.crt",
          customCertInfo);
    } else if (certType == CertConfigType.CustomServerCert) {
      return CertificateInfo.create(
          certUUID,
          customerUUID,
          label,
          today,
          nextYear,
          "privateKey",
          TestHelper.TMP_PATH + "/node_manager_test_ca.crt",
          CertConfigType.CustomServerCert);
    } else if (certType == CertConfigType.HashicorpVault) {
      return CertificateInfo.create(
          certUUID,
          customerUUID,
          label,
          today,
          nextYear,
          "privateKey",
          TestHelper.TMP_PATH + "/node_manager_test_ca.crt",
          CertConfigType.HashicorpVault);
    } else {
      throw new IllegalArgumentException("Unknown type " + certType);
    }
  }

  @Test
  public void testCertRotateWithoutCertRotateAction() {
    for (TestData data : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(
          data,
          params,
          Universe.saveDetails(
              createUniverse().getUniverseUUID(), ApiUtils.mockUniverseUpdater(data.cloudType)));
      params.type = Certs;
      params.certRotateAction = null;
      params.setProperty("processType", MASTER.toString());
      params.deviceInfo = new DeviceInfo();
      if (data.cloudType == CloudType.onprem) {
        params.deviceInfo.mountPoints = fakeMountPaths;
      }
      try {
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
        fail();
      } catch (RuntimeException re) {
        assertThat(re.getMessage(), containsString("Cert Rotation Action is null."));
      }
    }
  }

  @Test
  @Parameters({"APPEND_NEW_ROOT_CERT", "REMOVE_OLD_ROOT_CERT"})
  @TestCaseName("testCertsRotateWithNoRootCertRotation:{0}")
  public void testCertsRotateWithNoRootCertRotation(String action) {
    for (TestData data : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(
          data,
          params,
          Universe.saveDetails(
              createUniverse().getUniverseUUID(), ApiUtils.mockUniverseUpdater(data.cloudType)));
      params.type = Certs;
      params.certRotateAction = CertRotateAction.valueOf(action);
      params.rootCARotationType = CertRotationType.None;
      params.setProperty("processType", MASTER.toString());

      try {
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
        fail();
      } catch (RuntimeException re) {
        assertThat(
            re.getMessage(),
            containsString(action + " is needed only when there is rootCA rotation."));
      }
    }
  }

  @Test
  @Parameters({"APPEND_NEW_ROOT_CERT", "REMOVE_OLD_ROOT_CERT"})
  @TestCaseName("testCertsRotateWithNoRootCert:{0}")
  public void testCertsRotateWithNoRootCert(String action) {
    for (TestData data : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(
          data,
          params,
          Universe.saveDetails(
              createUniverse().getUniverseUUID(), ApiUtils.mockUniverseUpdater(data.cloudType)));
      params.type = Certs;
      params.certRotateAction = CertRotateAction.valueOf(action);
      params.rootCARotationType = CertRotationType.RootCert;
      params.setProperty("processType", MASTER.toString());

      try {
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
        fail();
      } catch (RuntimeException re) {
        assertThat(re.getMessage(), containsString("Certificate is null: null"));
      }
    }
  }

  @Test
  @Parameters({"APPEND_NEW_ROOT_CERT", "REMOVE_OLD_ROOT_CERT"})
  @TestCaseName("testCertsRotateWithCustomServerCert:{0}")
  public void testCertsRotateWithCustomServerCert(String action)
      throws IOException, NoSuchAlgorithmException {
    for (TestData data : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(
          data,
          params,
          Universe.saveDetails(
              createUniverse().getUniverseUUID(), ApiUtils.mockUniverseUpdater(data.cloudType)));

      params.type = Certs;
      params.certRotateAction = CertRotateAction.valueOf(action);
      params.rootCARotationType = CertRotationType.RootCert;
      params.rootCA =
          createCertificateConfig(
              CertConfigType.CustomServerCert, data.provider.getCustomerUUID(), params.nodePrefix);
      params.setProperty("processType", MASTER.toString());

      try {
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
        fail();
      } catch (RuntimeException re) {
        assertThat(
            re.getMessage(),
            containsString("Root certificate cannot be of type CustomServerCert."));
      }
    }
  }

  @Test
  @Parameters({
    "APPEND_NEW_ROOT_CERT, SelfSigned",
    "APPEND_NEW_ROOT_CERT, CustomCertHostPath",
    "REMOVE_OLD_ROOT_CERT, SelfSigned",
    "REMOVE_OLD_ROOT_CERT, CustomCertHostPath",
    "APPEND_NEW_ROOT_CERT, HashicorpVault",
    "REMOVE_OLD_ROOT_CERT, HashicorpVault"
  })
  @TestCaseName("testCertsRotateWithValidParams_Action:{0}_CertType:{1}")
  public void testCertsRotateWithValidParams(String action, String certType)
      throws IOException, NoSuchAlgorithmException {
    int idx = 0;
    for (TestData data : testData) {
      UUID universeUuid = createUniverse().getUniverseUUID();
      UserIntent userIntent =
          Universe.getOrBadRequest(universeUuid)
              .getUniverseDetails()
              .getPrimaryCluster()
              .userIntent;

      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(
          data,
          params,
          Universe.saveDetails(universeUuid, ApiUtils.mockUniverseUpdater(data.cloudType)));

      params.type = Certs;
      params.certRotateAction = CertRotateAction.valueOf(action);
      params.rootCARotationType = CertRotationType.RootCert;
      params.rootCA =
          createCertificateConfig(
              CertConfigType.valueOf(certType), data.provider.getCustomerUUID(), params.nodePrefix);
      params.setProperty("processType", MASTER.toString());
      params.deviceInfo = new DeviceInfo();
      params.deviceInfo.numVolumes = 1;
      if (data.cloudType == CloudType.onprem) {
        params.deviceInfo.mountPoints = fakeMountPaths;
      }
      reset(shellProcessHandler);
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      List<String> expectedCommand = data.baseCommand;
      expectedCommand.addAll(
          nodeCommand(
              NodeManager.NodeCommandType.Configure, params, data, userIntent, NODE_IPS[idx]));
      verify(shellProcessHandler, times(1))
          .run(eq(expectedCommand), any(ShellProcessContext.class));
      idx++;
    }
  }

  @Test
  @Parameters({"true, true", "true, false", "false, true", "false, false"})
  @TestCaseName("testCertsRotateCopyCerts_rootCA:{0}_clientRootCA:{1}")
  public void testCertsRotateCopyCerts(boolean isRootCA, boolean isClientRootCA)
      throws IOException, NoSuchAlgorithmException {
    int idx = 0;
    for (TestData data : testData) {
      UUID universeUuid = createUniverse().getUniverseUUID();
      UserIntent userIntent =
          Universe.getOrBadRequest(universeUuid)
              .getUniverseDetails()
              .getPrimaryCluster()
              .userIntent;

      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(
          data,
          params,
          Universe.saveDetails(universeUuid, ApiUtils.mockUniverseUpdater(data.cloudType)));

      params.type = Certs;
      params.certRotateAction = CertRotateAction.ROTATE_CERTS;
      if (isRootCA) {
        params.rootCARotationType = CertRotationType.RootCert;
        params.rootCA =
            createCertificateConfig(
                CertConfigType.SelfSigned, data.provider.getCustomerUUID(), params.nodePrefix);
      }
      if (isClientRootCA) {
        params.clientRootCARotationType = CertRotationType.RootCert;
        params.setClientRootCA(
            createCertificateConfig(
                CertConfigType.SelfSigned, data.provider.getCustomerUUID(), params.nodePrefix));
      }
      params.setProperty("processType", MASTER.toString());
      params.deviceInfo = new DeviceInfo();
      params.deviceInfo.numVolumes = 1;
      params.deviceInfo.mountPoints = fakeMountPaths;

      try {
        reset(shellProcessHandler);
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
        List<String> expectedCommand = data.baseCommand;
        expectedCommand.addAll(
            nodeCommand(
                NodeManager.NodeCommandType.Configure, params, data, userIntent, NODE_IPS[idx]));
        verify(shellProcessHandler, times(1)).run(captor.capture(), contextCaptor.capture());
        contextCaptor
            .getValue()
            .getDescription()
            .equals(
                String.format(
                    "bin/ybcloud.sh %s --region %s instance configure %s",
                    data.region.getProvider().getName(),
                    data.region.getCode(),
                    data.node.getNodeName()));
        List<String> actualCommand = captor.getValue();
        if (isRootCA) {
          int serverCertPathIndex = expectedCommand.indexOf("--server_cert_path") + 1;
          int serverKeyPathIndex = expectedCommand.indexOf("--server_key_path") + 1;
          expectedCommand.set(serverCertPathIndex, actualCommand.get(serverCertPathIndex));
          expectedCommand.set(serverKeyPathIndex, actualCommand.get(serverKeyPathIndex));
        }
        if (isClientRootCA) {
          int serverCertPathIndex =
              expectedCommand.indexOf("--server_cert_path_client_to_server") + 1;
          int serverKeyPathIndex =
              expectedCommand.indexOf("--server_key_path_client_to_server") + 1;
          expectedCommand.set(serverCertPathIndex, actualCommand.get(serverCertPathIndex));
          expectedCommand.set(serverKeyPathIndex, actualCommand.get(serverKeyPathIndex));
        }
        assertEquals(expectedCommand, actualCommand);
      } catch (RuntimeException re) {
        assertNull(re.getMessage());
      }
      idx++;
    }
  }

  @Test
  @Parameters({
    ", false, null, null, NONE",
    ", false, null, true, NONE",
    ", false, null, false, HOSTNAME",
    ", false, false, null, HOSTNAME",
    ", false, false, true, HOSTNAME",
    ", true, false, null, NONE",
    ", false, null, false, HOSTNAME",
    ", true, false, null, NONE",
    "ALL, true, false, null, ALL",
    "ALL, false, null, null, ALL",
    "HOSTNAME, true, false, null, HOSTNAME",
    "HOSTNAME, false, null, null, HOSTNAME"
  })
  public void testGetSkipCertValidation(
      String configValue,
      boolean inRemove,
      @Nullable Boolean inAddition,
      @Nullable Boolean inGflags,
      String expected) {
    Config config = Mockito.mock(Config.class);
    Mockito.when(config.getString(any())).thenReturn(configValue);
    final String flagName = GFlagsUtil.VERIFY_SERVER_ENDPOINT_GFLAG;
    AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
    if (inRemove) {
      params.gflagsToRemove.add(flagName);
    }
    if (inAddition != null) {
      params.gflags.put(flagName, String.valueOf(inAddition));
    }
    UserIntent userIntent = new UserIntent();
    if (inGflags != null) {
      userIntent.tserverGFlags.put(flagName, String.valueOf(inGflags));
    }

    assertEquals(
        NodeManager.SkipCertValidationType.valueOf(expected),
        NodeManager.getSkipCertValidationType(config, userIntent, params));
  }

  @Test
  public void testCronCheckWithAccessKey() {
    int idx = 0;
    for (TestData t : testData) {
      // Create AccessKey
      AccessKey.KeyInfo keyInfo = new AccessKey.KeyInfo();
      keyInfo.privateKey = "/path/to/private.key";
      keyInfo.publicKey = "/path/to/public.key";
      keyInfo.vaultFile = "/path/to/vault_file";
      keyInfo.vaultPasswordFile = "/path/to/vault_password";
      getOrCreate(t.provider.getUuid(), "demo-access", keyInfo);

      t.provider.getDetails().sshPort = 3333;
      t.provider.getDetails().installNodeExporter = false;
      t.provider.save();

      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().getUniverseUUID(), ApiUtils.mockUniverseUpdater(t.cloudType)));

      Universe.saveDetails(
          params.getUniverseUUID(),
          universe -> {
            NodeDetails nodeDetails = universe.getNode(params.nodeName);
            UserIntent userIntent =
                universe
                    .getUniverseDetails()
                    .getClusterByUuid(nodeDetails.placementUuid)
                    .userIntent;
            userIntent.accessKeyCode = "demo-access";
          });

      List<String> expectedCommand = new ArrayList<>(t.baseCommand);
      expectedCommand.addAll(
          nodeCommand(NodeManager.NodeCommandType.CronCheck, params, t, NODE_IPS[idx]));
      List<String> accessKeyCommands =
          new ArrayList<>(
              ImmutableList.of(
                  "--vars_file",
                  "/path/to/vault_file",
                  "--vault_password_file",
                  "/path/to/vault_password",
                  "--private_key_file",
                  "/path/to/private.key"));
      accessKeyCommands.add("--custom_ssh_port");
      accessKeyCommands.add("3333");
      expectedCommand.addAll(expectedCommand.size() - 3, accessKeyCommands);
      reset(shellProcessHandler);
      nodeManager.nodeCommand(NodeManager.NodeCommandType.CronCheck, params);
      verify(shellProcessHandler, times(1))
          .run(eq(expectedCommand), any(ShellProcessContext.class));
      idx++;
    }
  }

  @Test
  public void testPrecheckNotCheckingSelfSignedCertificates()
      throws IOException, NoSuchAlgorithmException {
    UUID customerUUID = testData.get(0).provider.getCustomerUUID();
    UUID certificateUUID =
        createCertificateConfig(
            CertConfigType.SelfSigned,
            customerUUID,
            "SS" + RandomStringUtils.randomAlphanumeric(8));
    List<String> cmds = createPrecheckCommandForCerts(certificateUUID, null);
    checkArguments(cmds, PRECHECK_CERT_PATHS); // Check no args
  }

  @Test
  public void testPrecheckNotCheckingTwoSelfSignedCertificates()
      throws IOException, NoSuchAlgorithmException {
    UUID customerUUID = testData.get(0).provider.getCustomerUUID();
    UUID certificateUUID1 =
        createCertificateConfig(
            CertConfigType.SelfSigned,
            customerUUID,
            "SS" + RandomStringUtils.randomAlphanumeric(8));
    UUID certificateUUID2 =
        createCertificateConfig(
            CertConfigType.SelfSigned,
            customerUUID,
            "SS" + RandomStringUtils.randomAlphanumeric(8));
    List<String> cmds = createPrecheckCommandForCerts(certificateUUID1, certificateUUID2);
    checkArguments(cmds, PRECHECK_CERT_PATHS); // Check no args
  }

  @Test
  public void testPrecheckCheckCertificates() throws IOException, NoSuchAlgorithmException {
    UUID customerUUID = testData.get(0).provider.getCustomerUUID();
    CertificateInfo certificateInfo =
        createCertificate(CertConfigType.CustomCertHostPath, customerUUID, "CS", "", false);
    List<String> cmds = createPrecheckCommandForCerts(certificateInfo.getUuid(), null);

    checkArguments(
        cmds,
        PRECHECK_CERT_PATHS,
        "--root_cert_path",
        certificateInfo.getCustomCertPathParams().rootCertPath,
        "--server_cert_path",
        certificateInfo.getCustomCertPathParams().nodeCertPath,
        "--server_key_path",
        certificateInfo.getCustomCertPathParams().nodeKeyPath);
  }

  @Test
  public void testPrecheckCheckEqualCertificates() throws IOException, NoSuchAlgorithmException {
    UUID customerUUID = testData.get(0).provider.getCustomerUUID();
    CertificateInfo certificateInfo =
        createCertificate(CertConfigType.CustomCertHostPath, customerUUID, "CS", "", true);
    List<String> cmds =
        createPrecheckCommandForCerts(certificateInfo.getUuid(), certificateInfo.getUuid());

    checkArguments(
        cmds,
        PRECHECK_CERT_PATHS,
        "--root_cert_path",
        certificateInfo.getCustomCertPathParams().rootCertPath,
        "--server_cert_path",
        certificateInfo.getCustomCertPathParams().nodeCertPath,
        "--server_key_path",
        certificateInfo.getCustomCertPathParams().nodeKeyPath,
        "--client_cert_path",
        certificateInfo.getCustomCertPathParams().clientCertPath,
        "--client_key_path",
        certificateInfo.getCustomCertPathParams().clientKeyPath);
  }

  @Test
  public void testPrecheckCheckCertificatesWithClientPaths()
      throws IOException, NoSuchAlgorithmException {
    UUID customerUUID = testData.get(0).provider.getCustomerUUID();
    CertificateInfo certificateInfo =
        createCertificate(
            CertConfigType.CustomCertHostPath,
            customerUUID,
            "CS" + RandomStringUtils.randomAlphanumeric(8),
            "",
            true);
    List<String> cmds = createPrecheckCommandForCerts(certificateInfo.getUuid(), null);

    checkArguments(
        cmds,
        PRECHECK_CERT_PATHS,
        "--root_cert_path",
        certificateInfo.getCustomCertPathParams().rootCertPath,
        "--server_cert_path",
        certificateInfo.getCustomCertPathParams().nodeCertPath,
        "--server_key_path",
        certificateInfo.getCustomCertPathParams().nodeKeyPath,
        "--client_cert_path",
        certificateInfo.getCustomCertPathParams().clientCertPath,
        "--client_key_path",
        certificateInfo.getCustomCertPathParams().clientKeyPath);
  }

  @Test
  public void testPrecheckCheckBothCertificatesWithClientPaths()
      throws IOException, NoSuchAlgorithmException {
    UUID customerUUID = testData.get(0).provider.getCustomerUUID();
    CertificateInfo cert =
        createCertificate(
            CertConfigType.CustomCertHostPath,
            customerUUID,
            "CS" + RandomStringUtils.randomAlphanumeric(8),
            "",
            true);
    CertificateInfo cert2 =
        createCertificate(
            CertConfigType.CustomCertHostPath,
            customerUUID,
            "CS" + RandomStringUtils.randomAlphanumeric(8),
            "",
            true);
    List<String> cmds = createPrecheckCommandForCerts(cert.getUuid(), cert2.getUuid());

    checkArguments(
        cmds,
        PRECHECK_CERT_PATHS,
        "--root_cert_path",
        cert.getCustomCertPathParams().rootCertPath,
        "--server_cert_path",
        cert.getCustomCertPathParams().nodeCertPath,
        "--server_key_path",
        cert.getCustomCertPathParams().nodeKeyPath,
        "--root_cert_path_client_to_server",
        cert2.getCustomCertPathParams().rootCertPath,
        "--server_cert_path_client_to_server",
        cert2.getCustomCertPathParams().nodeCertPath,
        "--server_key_path_client_to_server",
        cert2.getCustomCertPathParams().nodeKeyPath);
  }

  @Test
  public void testPrecheckCheckBothCertificates() throws IOException, NoSuchAlgorithmException {
    UUID customerUUID = testData.get(0).provider.getCustomerUUID();
    CertificateInfo cert =
        createCertificate(
            CertConfigType.CustomCertHostPath,
            customerUUID,
            "CS" + RandomStringUtils.randomAlphanumeric(8),
            "",
            false);
    CertificateInfo cert2 =
        createCertificate(
            CertConfigType.CustomCertHostPath,
            customerUUID,
            "CS" + RandomStringUtils.randomAlphanumeric(8),
            "",
            false);
    List<String> cmds = createPrecheckCommandForCerts(cert.getUuid(), cert2.getUuid());

    checkArguments(
        cmds,
        PRECHECK_CERT_PATHS,
        "--root_cert_path",
        cert.getCustomCertPathParams().rootCertPath,
        "--server_cert_path",
        cert.getCustomCertPathParams().nodeCertPath,
        "--server_key_path",
        cert.getCustomCertPathParams().nodeKeyPath,
        "--root_cert_path_client_to_server",
        cert2.getCustomCertPathParams().rootCertPath,
        "--server_cert_path_client_to_server",
        cert2.getCustomCertPathParams().nodeCertPath,
        "--server_key_path_client_to_server",
        cert2.getCustomCertPathParams().nodeKeyPath);
  }

  @Test
  public void testPrecheckCheckCertificatesSkipHostnameValidation()
      throws IOException, NoSuchAlgorithmException {
    UUID customerUUID = testData.get(0).provider.getCustomerUUID();
    CertificateInfo cert =
        createCertificate(CertConfigType.CustomCertHostPath, customerUUID, "CS", "", false);
    List<String> cmds =
        createPrecheckCommandForCerts(
            cert.getUuid(),
            null,
            params -> {
              Universe.saveDetails(
                  params.getUniverseUUID(),
                  universe -> {
                    NodeDetails nodeDetails = universe.getNode(params.nodeName);
                    UserIntent userIntent =
                        universe
                            .getUniverseDetails()
                            .getClusterByUuid(nodeDetails.placementUuid)
                            .userIntent;
                    userIntent.tserverGFlags.put(GFlagsUtil.VERIFY_SERVER_ENDPOINT_GFLAG, "false");
                  });
            });

    checkArguments(
        cmds,
        PRECHECK_CERT_PATHS,
        "--root_cert_path",
        cert.getCustomCertPathParams().rootCertPath,
        "--server_cert_path",
        cert.getCustomCertPathParams().nodeCertPath,
        "--server_key_path",
        cert.getCustomCertPathParams().nodeKeyPath,
        "--skip_cert_validation",
        "HOSTNAME");
  }

  @Test
  public void testDeleteRootVolumes() {
    int idx = 0;
    for (TestData t : testData) {
      NodeTaskParams params = new NodeTaskParams();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().getUniverseUUID(), ApiUtils.mockUniverseUpdater(t.cloudType)));
      List<String> expectedCommand = new ArrayList<>(t.baseCommand);
      expectedCommand.addAll(
          nodeCommand(NodeManager.NodeCommandType.Delete_Root_Volumes, params, t, NODE_IPS[idx]));
      reset(shellProcessHandler);
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Delete_Root_Volumes, params);
      verify(shellProcessHandler, times(1))
          .run(eq(expectedCommand), any(ShellProcessContext.class));
      idx++;
    }
  }

  @Test
  public void testCGroupsSize() {
    TestData td = this.testData.get(0);
    Universe universe = createUniverse();
    PlacementInfo pi = new PlacementInfo();
    UserIntent userIntent = new UserIntent();
    userIntent.provider = td.provider.getUuid().toString();
    userIntent.providerType = td.provider.getCloudCode();
    userIntent.numNodes = 1;
    userIntent.replicationFactor = 1;
    PlacementInfoUtil.addPlacementZone(td.zone.getUuid(), pi, 1, 1, false);
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            ApiUtils.mockUniverseUpdaterWithReadReplica(userIntent, pi));
    when(mockConfGetter.getConfForScope(eq(universe), eq(UniverseConfKeys.dbMemPostgresMaxMemMb)))
        .thenReturn(100);
    when(mockConfGetter.getConfForScope(
            eq(universe), eq(UniverseConfKeys.dbMemPostgresReadReplicaMaxMemMb)))
        .thenReturn(-1);

    NodeDetails primaryNode = new NodeDetails();
    primaryNode.azUuid = td.zone.getUuid();
    primaryNode.placementUuid = universe.getUniverseDetails().getPrimaryCluster().uuid;
    universe.getUniverseDetails().nodeDetailsSet.add(primaryNode);
    NodeDetails rrNode =
        universe
            .getNodesInCluster(universe.getUniverseDetails().getReadOnlyClusters().get(0).uuid)
            .iterator()
            .next();

    UserIntent primaryIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
    UserIntent rrIntent = universe.getUniverseDetails().getReadOnlyClusters().get(0).userIntent;

    verifyCGroupSize(universe, primaryNode, 100);
    verifyCGroupSize(universe, rrNode, 100); // inherited

    primaryIntent.setCgroupSize(37);
    verifyCGroupSize(universe, primaryNode, 37);
    verifyCGroupSize(universe, rrNode, 37); // inherited
    rrIntent.setCgroupSize(-1);
    verifyCGroupSize(universe, rrNode, 37); // inherited
    rrIntent.setCgroupSize(21);
    verifyCGroupSize(universe, rrNode, 21);

    primaryIntent.setUserIntentOverrides(TestUtils.composeAZOverrides(UUID.randomUUID(), null, 10));
    verifyCGroupSize(universe, primaryNode, 37); // still the same as wrong azUUID
    primaryIntent.setUserIntentOverrides(TestUtils.composeAZOverrides(td.zone.getUuid(), null, 10));
    verifyCGroupSize(universe, primaryNode, 10);
    verifyCGroupSize(universe, rrNode, 21);

    rrIntent.setUserIntentOverrides(TestUtils.composeAZOverrides(td.zone.getUuid(), null, 5));
    verifyCGroupSize(universe, rrNode, 5);
  }

  private void verifyCGroupSize(Universe universe, NodeDetails node, int value) {
    assertEquals(
        value,
        UniverseDefinitionTaskBase.getCGroupSize(
            mockConfGetter,
            universe,
            universe.getUniverseDetails().getPrimaryCluster(),
            universe.getUniverseDetails().getClusterByUuid(node.placementUuid),
            node));
  }

  private void checkArguments(
      List<String> currentArgs, List<String> allPossibleArgs, String... argsExpected) {
    Map<String, String> k2v = LocalNodeManager.convertCommandArgListToMap(currentArgs);
    List<String> allArgsCpy = new ArrayList<>(allPossibleArgs);
    for (int i = 0; i < argsExpected.length; i += 2) {
      String key = argsExpected[i];
      String value = argsExpected[i + 1];
      assertEquals(key + " should have value", value, k2v.get(key));
      allArgsCpy.remove(key);
    }
    for (String absent : allArgsCpy) {
      assertFalse(
          currentArgs.toString() + " should not contain " + absent, k2v.containsKey(absent));
    }
  }

  private List<String> createPrecheckCommandForCerts(UUID certificateUUID1, UUID certificateUUID2) {
    return createPrecheckCommandForCerts(certificateUUID1, certificateUUID2, x -> {});
  }

  private List<String> createPrecheckCommandForCerts(
      UUID certificateUUID1, UUID certificateUUID2, Consumer<NodeTaskParams> paramsUpdate) {
    TestData onpremTD =
        testData.stream().filter(t -> t.cloudType == Common.CloudType.onprem).findFirst().get();
    AccessKey.KeyInfo keyInfo = new AccessKey.KeyInfo();
    keyInfo.privateKey = "/path/to/private.key";
    keyInfo.publicKey = "/path/to/public.key";
    keyInfo.vaultFile = "/path/to/vault_file";
    keyInfo.vaultPasswordFile = "/path/to/vault_password";
    getOrCreate(onpremTD.provider.getUuid(), "mock-access-code-key", keyInfo);

    onpremTD.provider.getDetails().sshPort = 3333;
    onpremTD.provider.getDetails().airGapInstall = true;
    onpremTD.provider.save();

    NodeTaskParams nodeTaskParams =
        buildValidParams(
            onpremTD,
            new NodeTaskParams(),
            Universe.saveDetails(
                createUniverse().getUniverseUUID(),
                ApiUtils.mockUniverseUpdater(onpremTD.cloudType)));
    nodeTaskParams.rootCA = certificateUUID1;
    nodeTaskParams.setClientRootCA(certificateUUID2);
    nodeTaskParams.rootAndClientRootCASame =
        certificateUUID2 == null || Objects.equals(certificateUUID1, certificateUUID2);

    paramsUpdate.accept(nodeTaskParams);
    Universe.saveDetails(
        nodeTaskParams.getUniverseUUID(),
        universe -> {
          NodeDetails nodeDetails = universe.getNode(nodeTaskParams.nodeName);
          UserIntent userIntent =
              universe.getUniverseDetails().getClusterByUuid(nodeDetails.placementUuid).userIntent;
          userIntent.enableNodeToNodeEncrypt = true;
        });
    nodeManager.nodeCommand(NodeManager.NodeCommandType.Precheck, nodeTaskParams);
    ArgumentCaptor<List> arg = ArgumentCaptor.forClass(List.class);
    verify(shellProcessHandler).run(arg.capture(), any(ShellProcessContext.class));
    return new ArrayList<>(arg.getValue());
  }

  private void assertFails(Runnable r, String expectedError) {
    try {
      r.run();
      fail();
    } catch (RuntimeException re) {
      assertThat(re.getMessage(), is(expectedError));
    }
  }
}
