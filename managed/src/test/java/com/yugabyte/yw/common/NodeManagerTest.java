// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.common;

import static com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.ServerType.MASTER;
import static com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.ServerType.TSERVER;
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
import static org.mockito.ArgumentMatchers.anyMap;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.spy;
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
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.ServerType;
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
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
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
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.io.File;
import java.io.IOException;
import java.security.NoSuchAlgorithmException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Calendar;
import java.util.Date;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.TreeMap;
import java.util.UUID;
import java.util.function.Consumer;
import java.util.function.Predicate;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import junitparams.converters.Nullable;
import junitparams.naming.TestCaseName;
import org.apache.commons.lang3.StringUtils;
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

  @Mock play.Configuration mockAppConfig;

  @Mock ShellProcessHandler shellProcessHandler;

  @Mock ReleaseManager releaseManager;

  @Mock RuntimeConfigFactory runtimeConfigFactory;

  @Mock ConfigHelper mockConfigHelper;

  @InjectMocks NodeManager nodeManager;

  @Mock Config mockConfig;

  private final String DOCKER_NETWORK = "yugaware_bridge";
  private final String MASTER_ADDRESSES = "10.0.0.1:7100,10.0.0.2:7100,10.0.0.3:7100";
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
    public final String NewInstanceType = "test-c5.2xlarge";

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

      NodeInstanceFormData.NodeInstanceData nodeData = new NodeInstanceFormData.NodeInstanceData();
      nodeData.ip = "fake_ip";
      nodeData.region = region.code;
      nodeData.zone = zone.code;
      nodeData.instanceType = instanceTypeCode;
      node = NodeInstance.create(zone.uuid, nodeData);
      // Update name.
      node.setNodeName("host-n" + idx);
      node.save();

      baseCommand.add("bin/ybcloud.sh");
      baseCommand.add(provider.code);
      baseCommand.add("--region");
      baseCommand.add(region.code);
      baseCommand.add("--zone");
      baseCommand.add(zone.code);

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
    return ModelFactory.createUniverse("Test Universe - " + universeUUID, universeUUID);
  }

  private NodeTaskParams buildValidParams(
      TestData testData, NodeTaskParams params, Universe universe) {
    params.azUuid = testData.zone.uuid;
    params.instanceType = testData.node.getInstanceTypeCode();
    params.nodeName = testData.node.getNodeName();
    Iterator<NodeDetails> iter = universe.getNodes().iterator();
    if (iter.hasNext()) {
      params.nodeUuid = iter.next().nodeUuid;
    }
    params.universeUUID = universe.universeUUID;
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
        configureParams.enableNodeToNodeEncrypt
            || (configureParams.rootAndClientRootCASame
                && configureParams.enableClientToNodeEncrypt),
        !configureParams.rootAndClientRootCASame && configureParams.enableClientToNodeEncrypt,
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
        throw new RuntimeException("No valid rootCA found for " + configureParams.universeUUID);
      }

      String rootCertPath = null, serverCertPath = null, serverKeyPath = null, certsLocation = null;

      switch (rootCert.certType) {
        case SelfSigned:
          {
            rootCertPath = rootCert.certificate;
            serverCertPath = "cert.crt";
            serverKeyPath = "key.crt";
            certsLocation = NodeManager.CERT_LOCATION_PLATFORM;

            if (configureParams.rootAndClientRootCASame
                && configureParams.enableClientToNodeEncrypt) {
              expectedCommand.add("--client_cert_path");
              expectedCommand.add(CertificateHelper.getClientCertFile(configureParams.rootCA));
              expectedCommand.add("--client_key_path");
              expectedCommand.add(CertificateHelper.getClientKeyFile(configureParams.rootCA));
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
            rootCertPath = rootCert.certificate;
            serverCertPath = "cert.crt";
            serverKeyPath = "key.crt";
            certsLocation = NodeManager.CERT_LOCATION_PLATFORM;

            if (configureParams.rootAndClientRootCASame
                && configureParams.enableClientToNodeEncrypt) {
              expectedCommand.add("--client_cert_path");
              expectedCommand.add(CertificateHelper.getClientCertFile(configureParams.rootCA));
              expectedCommand.add("--client_key_path");
              expectedCommand.add(CertificateHelper.getClientKeyFile(configureParams.rootCA));
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

      CertificateInfo clientRootCert = CertificateInfo.get(configureParams.clientRootCA);
      if (clientRootCert == null) {
        throw new RuntimeException(
            "No valid clientRootCA found for " + configureParams.universeUUID);
      }

      String rootCertPath = null, serverCertPath = null, serverKeyPath = null, certsLocation = null;

      switch (clientRootCert.certType) {
        case SelfSigned:
          {
            rootCertPath = clientRootCert.certificate;
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
            rootCertPath = clientRootCert.certificate;
            serverCertPath = customServerCertInfo.serverCert;
            serverKeyPath = customServerCertInfo.serverKey;
            certsLocation = NodeManager.CERT_LOCATION_PLATFORM;
          }
        case HashicorpVault:
          {
            rootCertPath = clientRootCert.certificate;
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
                Universe.getOrBadRequest(configureParams.universeUUID)),
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
              t.provider.customerUUID,
              params.nodePrefix,
              today,
              nextYear,
              TestHelper.TMP_PATH + "/node_manager_test_ca.crt",
              customCertInfo);
    } else {
      UUID certUUID =
          CertificateHelper.createRootCA("foobar", t.provider.customerUUID, TestHelper.TMP_PATH);
      cert = CertificateInfo.get(certUUID);
    }

    Universe u = createUniverse();
    u.getUniverseDetails().rootCA = cert.uuid;
    buildValidParams(
        t, params, Universe.saveDetails(u.universeUUID, ApiUtils.mockUniverseUpdater(t.cloudType)));
    return cert.uuid;
  }

  @Before
  public void setUp() {
    Customer customer = ModelFactory.testCustomer();
    testData = new ArrayList<TestData>();
    testData.addAll(getTestData(customer, Common.CloudType.aws));
    testData.addAll(getTestData(customer, Common.CloudType.gcp));
    testData.addAll(getTestData(customer, Common.CloudType.onprem));
    Config appConfig = spy(app.config());
    ReleaseManager.ReleaseMetadata releaseMetadata = new ReleaseManager.ReleaseMetadata();
    releaseMetadata.filePath = "/yb/release.tar.gz";
    when(releaseManager.getReleaseByVersion("0.0.1")).thenReturn(releaseMetadata);
    when(mockConfig.getString(NodeManager.BOOT_SCRIPT_PATH)).thenReturn("");
    when(mockAppConfig.getString(eq("yb.security.default.access.key")))
        .thenReturn(ApiUtils.DEFAULT_ACCESS_KEY_CODE);
    when(runtimeConfigFactory.forProvider(any())).thenReturn(mockConfig);
    when(runtimeConfigFactory.forUniverse(any())).thenReturn(appConfig);
    when(runtimeConfigFactory.globalRuntimeConf()).thenReturn(mockConfig);
    doReturn(true).when(appConfig).getBoolean(eq("yb.gflags.notify_peer_of_removal_from_cluster"));
    createTempFile("node_manager_test_ca.crt", "test-cert");
  }

  private String getMountPoints(AnsibleConfigureServers.Params taskParam) {
    if (taskParam.deviceInfo.mountPoints != null) {
      return taskParam.deviceInfo.mountPoints;
    } else if (taskParam.deviceInfo.numVolumes != null
        && !taskParam.getProvider().code.equals("onprem")) {
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
    Map<String, String> gflags = new HashMap<>();
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

    gflags.put("cluster_uuid", String.valueOf(configureParams.universeUUID));
    if (configureParams.enableYSQL) {
      gflags.put("enable_ysql", "true");
      gflags.put("pgsql_proxy_webserver_port", "13000");
      gflags.put(
          "pgsql_proxy_bind_address",
          String.format(
              "%s:%s",
              pgsqlProxyBindAddress,
              Universe.getOrBadRequest(configureParams.universeUUID)
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
              Universe.getOrBadRequest(configureParams.universeUUID)
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

    if (configureParams.isMaster) {
      gflags.put("replication_factor", String.valueOf(userIntent.replicationFactor));
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
          Universe.getOrBadRequest(configureParams.universeUUID)
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
                .forUniverse(Universe.getOrBadRequest(configureParams.universeUUID))
                .getInt(NodeManager.POSTGRES_MAX_MEM_MB)
            > 0) {
      gflags.put("postmaster_cgroup", NodeManager.YSQL_CGROUP_PATH);
    }
    return gflags;
  }

  private List<String> nodeCommand(
      NodeManager.NodeCommandType type, NodeTaskParams params, TestData testData) {
    return nodeCommand(type, params, testData, new UserIntent());
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
    tags.put("customer-uuid", testData.customer.uuid.toString());
    tags.put("universe-uuid", nodeTaskParam.universeUUID.toString());
    tags.put("node-uuid", nodeTaskParam.nodeUuid.toString());
  }

  private List<String> nodeCommand(
      NodeManager.NodeCommandType type,
      NodeTaskParams params,
      TestData testData,
      UserIntent userIntent) {
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
        break;
      case Create_Root_Volumes:
        CreateRootVolumes.Params crvParams = (CreateRootVolumes.Params) params;
        expectedCommand.add("--num_disks");
        expectedCommand.add(String.valueOf(crvParams.numVolumes));
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
          String ybImage = testData.region.ybImage;
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
          if (createParams.cmkArn != null) {
            expectedCommand.add("--cmk_res_name");
            expectedCommand.add(createParams.cmkArn);
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

        if (cloud.equals(Common.CloudType.gcp)) {
          String ybImage = testData.region.ybImage;
          if (ybImage != null && !ybImage.isEmpty()) {
            expectedCommand.add("--machine_image");
            expectedCommand.add(ybImage);
          }
        }

        if ((cloud.equals(Common.CloudType.aws) || cloud.equals(Common.CloudType.gcp))
            && setupParams.useTimeSync) {
          expectedCommand.add("--use_chrony");
        }
        break;
      case Configure:
        AnsibleConfigureServers.Params configureParams = (AnsibleConfigureServers.Params) params;

        expectedCommand.add("--master_addresses_for_tserver");
        expectedCommand.add(MASTER_ADDRESSES);
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
              break;
            case None:
              break;
          }
        }

        if (configureParams.ybSoftwareVersion != null) {
          expectedCommand.add("--num_releases_to_keep");
          expectedCommand.add("3");
        }
        Map<String, String> gflags = new HashMap<>(configureParams.gflags);

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
            }
            if (!configureParams.rootAndClientRootCASame
                && configureParams.enableClientToNodeEncrypt) {
              gflags.put("certs_for_client_dir", certsForClientDir);
            }
            expectedCommand.addAll(getCertificatePaths(userIntent, configureParams, ybHomeDir));
          } else if (UpgradeTaskParams.UpgradeTaskSubType.Round1GFlagsUpdate.name()
              .equals(subType)) {
            gflags = new HashMap<>(configureParams.gflags);
            if (configureParams.nodeToNodeChange > 0) {
              gflags.put("use_node_to_node_encryption", nodeToNodeString);
              gflags.put("use_client_to_server_encryption", clientToNodeString);
              gflags.put("allow_insecure_connections", "true");
              if (EncryptionInTransitUtil.isRootCARequired(configureParams)) {
                gflags.put("certs_dir", certsDir);
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
              }
              if (EncryptionInTransitUtil.isClientRootCARequired(configureParams)) {
                gflags.put("certs_for_client_dir", certsForClientDir);
              }
            }
          } else if (UpgradeTaskParams.UpgradeTaskSubType.Round2GFlagsUpdate.name()
              .equals(subType)) {
            gflags = new HashMap<>(configureParams.gflags);
            if (configureParams.nodeToNodeChange > 0) {
              gflags.put("allow_insecure_connections", allowInsecureString);
            } else if (configureParams.nodeToNodeChange < 0) {
              gflags.put("use_node_to_node_encryption", nodeToNodeString);
              gflags.put("use_client_to_server_encryption", clientToNodeString);
              gflags.put("allow_insecure_connections", allowInsecureString);
              if (EncryptionInTransitUtil.isRootCARequired(configureParams)) {
                gflags.put("certs_dir", certsDir);
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
                if (rootCert.certType == CertConfigType.SelfSigned) {
                  rootCertPath = rootCert.certificate;
                  certsLocation = NodeManager.CERT_LOCATION_PLATFORM;
                } else if (rootCert.certType == CertConfigType.CustomCertHostPath) {
                  rootCertPath = rootCert.getCustomCertPathParams().rootCertPath;
                  certsLocation = NodeManager.CERT_LOCATION_NODE;
                } else if (rootCert.certType == CertConfigType.HashicorpVault) {
                  rootCertPath = rootCert.certificate;
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
                Map<String, String> gflagMap = new HashMap<>();
                if (EncryptionInTransitUtil.isRootCARequired(configureParams)) {
                  gflagMap.put("certs_dir", certsNodeDir);
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
            && configureParams.type != Certs) {
          expectedCommand.add("--gflags");
          expectedCommand.add(Json.stringify(Json.toJson(gflags)));

          expectedCommand.add("--tags");
          expectedCommand.add("override_gflags");
        }

        expectedCommand.add("--extra_gflags");
        expectedCommand.add(
            Json.stringify(
                Json.toJson(
                    getExtraGflags(configureParams, params, userIntent, testData, useHostname))));

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
      }

      if (type == NodeManager.NodeCommandType.Provision
          || type == NodeManager.NodeCommandType.Create_Root_Volumes
          || type == NodeManager.NodeCommandType.Create) {
        if (deviceInfo.storageType != null) {
          if (type == NodeManager.NodeCommandType.Create
              || type == NodeManager.NodeCommandType.Create_Root_Volumes) {
            expectedCommand.add("--volume_type");
            expectedCommand.add(deviceInfo.storageType.toString().toLowerCase());
            if (deviceInfo.storageType.isIopsProvisioning() && deviceInfo.diskIops != null) {
              expectedCommand.add("--disk_iops");
              expectedCommand.add(Integer.toString(deviceInfo.diskIops));
            }
            if (deviceInfo.storageType.isThroughputProvisioning()
                && deviceInfo.throughput != null) {
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
          String packagePath = mockAppConfig.getString("yb.thirdparty.packagePath");
          if (packagePath != null) {
            expectedCommand.add("--local_package_path");
            expectedCommand.add(packagePath);
          }
          expectedCommand.add("--pg_max_mem_mb");
          expectedCommand.add(
              Integer.toString(
                  runtimeConfigFactory
                      .forUniverse(Universe.getOrBadRequest(params.universeUUID))
                      .getInt(NodeManager.POSTGRES_MAX_MEM_MB)));
        }
      }
    }
    if (type == NodeManager.NodeCommandType.Create) {
      expectedCommand.add("--as_json");
    }
    expectedCommand.add(params.nodeName);
    return expectedCommand;
  }

  @Test
  public void testChangeInstanceTypeCommand() {
    for (TestData t : testData) {
      ChangeInstanceType.Params params = new ChangeInstanceType.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().universeUUID, ApiUtils.mockUniverseUpdater(t.cloudType)));
      List<String> expectedCommand = t.baseCommand;
      params.instanceType = t.NewInstanceType;
      expectedCommand.addAll(
          nodeCommand(NodeManager.NodeCommandType.Change_Instance_Type, params, t));

      nodeManager.nodeCommand(NodeManager.NodeCommandType.Change_Instance_Type, params);
      verify(shellProcessHandler, times(1)).run(eq(expectedCommand), anyMap(), anyString());
    }
  }

  @Test
  @Parameters({"true, false", "false, false", "true, true", "false, true"})
  @TestCaseName("{method}(isCopy:{0},isCustomProducerCertsDir:{1}) [{index}]")
  public void testTransferXClusterCertsCommand(boolean isCopy, boolean isCustomProducerCertsDir) {
    for (TestData t : testData) {
      TransferXClusterCerts.Params params = new TransferXClusterCerts.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().universeUUID, ApiUtils.mockUniverseUpdater(t.cloudType)));
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
          nodeCommand(NodeManager.NodeCommandType.Transfer_XCluster_Certs, params, t));

      nodeManager.nodeCommand(NodeManager.NodeCommandType.Transfer_XCluster_Certs, params);
      verify(shellProcessHandler, times(1)).run(eq(expectedCommand), anyMap(), anyString());
    }
  }

  private void runAndVerifyNodeCommand(
      TestData t, NodeTaskParams params, NodeManager.NodeCommandType cmdType) {
    List<String> expectedCommand = t.baseCommand;
    expectedCommand.addAll(nodeCommand(cmdType, params, t));

    nodeManager.nodeCommand(cmdType, params);
    verify(shellProcessHandler, times(1)).run(eq(expectedCommand), anyMap(), anyString());
  }

  @Test
  public void testCreateRootVolumesCommand() {
    for (TestData t : testData) {
      CreateRootVolumes.Params params = new CreateRootVolumes.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().universeUUID, ApiUtils.mockUniverseUpdater(t.cloudType)));
      addValidDeviceInfo(t, params);
      params.subnetId = t.zone.subnet;
      params.numVolumes = 1;

      runAndVerifyNodeCommand(t, params, NodeManager.NodeCommandType.Create_Root_Volumes);
    }
  }

  @Test
  public void testReplaceRootVolumeCommand() {
    for (TestData t : testData) {
      ReplaceRootVolume.Params params = new ReplaceRootVolume.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().universeUUID, ApiUtils.mockUniverseUpdater(t.cloudType)));
      addValidDeviceInfo(t, params);
      params.replacementDisk = t.replacementVolume;

      runAndVerifyNodeCommand(t, params, NodeManager.NodeCommandType.Replace_Root_Volume);
    }
  }

  @Test
  public void testProvisionNodeCommand() {
    for (TestData t : testData) {
      AnsibleSetupServer.Params params = new AnsibleSetupServer.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().universeUUID, ApiUtils.mockUniverseUpdater(t.cloudType)));
      addValidDeviceInfo(t, params);
      params.subnetId = t.zone.subnet;

      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Provision, params, t));

      nodeManager.nodeCommand(NodeManager.NodeCommandType.Provision, params);
      verify(shellProcessHandler, times(1)).run(eq(expectedCommand), anyMap(), anyString());
    }
  }

  @Test
  public void testCreateNodeCommandSecondarySubnet() {
    for (TestData t : testData) {
      when(mockConfig.getBoolean("yb.cloud.enabled")).thenReturn(true);
      AnsibleCreateServer.Params params = new AnsibleCreateServer.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().universeUUID, ApiUtils.mockUniverseUpdater(t.cloudType)));
      addValidDeviceInfo(t, params);
      params.subnetId = t.zone.subnet;
      params.secondarySubnetId = "testSubnet";

      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Create, params, t));

      nodeManager.nodeCommand(NodeManager.NodeCommandType.Create, params);
      verify(shellProcessHandler, times(1)).run(eq(expectedCommand), anyMap(), anyString());
    }
  }

  @Test
  public void testProvisionNodeCommandWithLocalPackage() {
    String packagePath = "/tmp/third-party";
    new File(packagePath).mkdir();
    when(mockAppConfig.getString("yb.thirdparty.packagePath")).thenReturn(packagePath);

    for (TestData t : testData) {
      AnsibleSetupServer.Params params = new AnsibleSetupServer.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().universeUUID, ApiUtils.mockUniverseUpdater(t.cloudType)));
      addValidDeviceInfo(t, params);
      params.subnetId = t.zone.subnet;

      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Provision, params, t));

      nodeManager.nodeCommand(NodeManager.NodeCommandType.Provision, params);
      verify(shellProcessHandler, times(1)).run(eq(expectedCommand), anyMap(), anyString());
    }

    File file = new File(packagePath);
    file.delete();
  }

  @Test
  public void testProvisionUseTimeSync() {
    int iteration = 0;
    for (TestData t : testData) {
      for (boolean useTimeSync : ImmutableList.of(true, false)) {
        // Bump up the iteration, for use in the verify call and getting the correct
        // capture.
        ++iteration;
        AnsibleSetupServer.Params params = new AnsibleSetupServer.Params();
        buildValidParams(
            t,
            params,
            Universe.saveDetails(
                createUniverse().universeUUID, ApiUtils.mockUniverseUpdater(t.cloudType)));
        addValidDeviceInfo(t, params);
        params.useTimeSync = useTimeSync;

        ArgumentCaptor<List> arg = ArgumentCaptor.forClass(List.class);
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Provision, params);
        verify(shellProcessHandler, times(iteration)).run(arg.capture(), any(), anyString());
        // For AWS and useTimeSync knob set to true, we want to find the flag.
        List<String> cmdArgs = arg.getAllValues().get(iteration - 1);
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
    for (TestData t : testData) {
      AnsibleSetupServer.Params params = new AnsibleSetupServer.Params();
      UUID univUUID = createUniverse().universeUUID;
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
          nodeCommand(NodeManager.NodeCommandType.Provision, params, t));

      nodeManager.nodeCommand(NodeManager.NodeCommandType.Provision, params);
      verify(shellProcessHandler, times(1))
          .run(eq(expectedCommandArrayList), anyMap(), anyString());
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
    for (TestData t : testData) {
      t.region.setSecurityGroupId(sgId);
      t.region.update();
      // Create AccessKey
      AccessKey.KeyInfo keyInfo = new AccessKey.KeyInfo();
      keyInfo.privateKey = "/path/to/private.key";
      keyInfo.publicKey = "/path/to/public.key";
      keyInfo.vaultFile = "/path/to/vault_file";
      keyInfo.vaultPasswordFile = "/path/to/vault_password";
      keyInfo.sshPort = 3333;
      keyInfo.airGapInstall = true;
      getOrCreate(t.provider.uuid, "demo-access", keyInfo);

      // Set up task params
      UniverseDefinitionTaskParams.UserIntent userIntent =
          new UniverseDefinitionTaskParams.UserIntent();
      userIntent.numNodes = 3;
      userIntent.accessKeyCode = "demo-access";
      userIntent.regionList = new ArrayList<UUID>();
      userIntent.regionList.add(t.region.uuid);
      userIntent.providerType = t.cloudType;
      AnsibleSetupServer.Params params = new AnsibleSetupServer.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().universeUUID, ApiUtils.mockUniverseUpdater(userIntent)));
      addValidDeviceInfo(t, params);

      // Set up expected command
      int accessKeyIndexOffset = 7;
      if (t.cloudType.equals(Common.CloudType.aws)
          && params.deviceInfo.storageType.equals(PublicCloudConstants.StorageType.IO1)) {
        accessKeyIndexOffset += 2;
      }

      List<String> expectedCommand = new ArrayList<>(t.baseCommand);
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Provision, params, t));
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

      nodeManager.nodeCommand(NodeManager.NodeCommandType.Provision, params);
      verify(shellProcessHandler, times(1)).run(eq(expectedCommand), anyMap(), anyString());
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
                createUniverse().universeUUID, ApiUtils.mockUniverseUpdater(t.cloudType)));
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Provision, params);
        fail();
      } catch (RuntimeException re) {
        assertThat(re.getMessage(), is("NodeTaskParams is not AnsibleSetupServer.Params"));
      }
    }
  }

  @Test
  public void testCreateNodeCommand() {
    for (TestData t : testData) {
      AnsibleCreateServer.Params params = new AnsibleCreateServer.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().universeUUID, ApiUtils.mockUniverseUpdater(t.cloudType)));
      addValidDeviceInfo(t, params);
      params.subnetId = t.zone.subnet;

      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Create, params, t));

      nodeManager.nodeCommand(NodeManager.NodeCommandType.Create, params);
      verify(shellProcessHandler, times(1)).run(eq(expectedCommand), anyMap(), anyString());
    }
  }

  @Test
  public void testCreateNodeCommandWithoutAssignPublicIP() {
    for (TestData t : testData) {
      AnsibleCreateServer.Params params = new AnsibleCreateServer.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().universeUUID, ApiUtils.mockUniverseUpdater(t.cloudType)));
      addValidDeviceInfo(t, params);
      params.subnetId = t.zone.subnet;
      if (t.cloudType.equals(Common.CloudType.aws)) {
        params.assignPublicIP = false;
      }

      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Create, params, t));
      if (t.cloudType.equals(Common.CloudType.aws)) {
        Predicate<String> stringPredicate = p -> p.equals("--assign_public_ip");
        expectedCommand.removeIf(stringPredicate);
      }
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Create, params);
      verify(shellProcessHandler, times(1)).run(eq(expectedCommand), anyMap(), anyString());
    }
  }

  @Test
  public void testCreateNodeCommandWithLocalPackage() {
    String packagePath = "/tmp/third-party";
    new File(packagePath).mkdir();
    when(mockAppConfig.getString("yb.thirdparty.packagePath")).thenReturn(packagePath);

    for (TestData t : testData) {
      AnsibleCreateServer.Params params = new AnsibleCreateServer.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().universeUUID, ApiUtils.mockUniverseUpdater(t.cloudType)));
      addValidDeviceInfo(t, params);
      params.subnetId = t.zone.subnet;

      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Create, params, t));

      nodeManager.nodeCommand(NodeManager.NodeCommandType.Create, params);
      verify(shellProcessHandler, times(1)).run(eq(expectedCommand), anyMap(), anyString());
    }

    File file = new File(packagePath);
    file.delete();
  }

  @Test
  public void testCreateWithAWSTags() {
    for (TestData t : testData) {
      AnsibleCreateServer.Params params = new AnsibleCreateServer.Params();
      UUID univUUID = createUniverse().universeUUID;
      Universe universe = Universe.saveDetails(univUUID, ApiUtils.mockUniverseUpdater(t.cloudType));
      buildValidParams(t, params, universe);
      addValidDeviceInfo(t, params);
      if (t.cloudType.equals(Common.CloudType.aws)) {
        ApiUtils.insertInstanceTags(univUUID);
        setInstanceTags(params);
      }

      ArrayList<String> expectedCommandArrayList = new ArrayList<>();
      expectedCommandArrayList.addAll(t.baseCommand);
      expectedCommandArrayList.addAll(nodeCommand(NodeManager.NodeCommandType.Create, params, t));

      nodeManager.nodeCommand(NodeManager.NodeCommandType.Create, params);
      verify(shellProcessHandler, times(1))
          .run(eq(expectedCommandArrayList), anyMap(), anyString());
    }
  }

  private void runAndTestCreateWithAccessKeyAndSG(String sgId) {
    for (TestData t : testData) {
      t.region.setSecurityGroupId(sgId);
      t.region.update();
      // Create AccessKey
      AccessKey.KeyInfo keyInfo = new AccessKey.KeyInfo();
      keyInfo.privateKey = "/path/to/private.key";
      keyInfo.publicKey = "/path/to/public.key";
      keyInfo.vaultFile = "/path/to/vault_file";
      keyInfo.vaultPasswordFile = "/path/to/vault_password";
      keyInfo.sshPort = 3333;
      keyInfo.airGapInstall = true;
      getOrCreate(t.provider.uuid, "demo-access", keyInfo);

      // Set up task params
      UniverseDefinitionTaskParams.UserIntent userIntent =
          new UniverseDefinitionTaskParams.UserIntent();
      userIntent.numNodes = 3;
      userIntent.accessKeyCode = "demo-access";
      userIntent.regionList = new ArrayList<>();
      userIntent.regionList.add(t.region.uuid);
      userIntent.providerType = t.cloudType;
      AnsibleCreateServer.Params params = new AnsibleCreateServer.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().universeUUID, ApiUtils.mockUniverseUpdater(userIntent)));
      addValidDeviceInfo(t, params);

      // Set up expected command
      int accessKeyIndexOffset = 6;
      if (t.cloudType.equals(Common.CloudType.aws)) {
        accessKeyIndexOffset += 2;
        if (params.deviceInfo.storageType.equals(PublicCloudConstants.StorageType.IO1)) {
          accessKeyIndexOffset += 2;
        }
      }

      List<String> expectedCommand = new ArrayList<>(t.baseCommand);
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Create, params, t));
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

      nodeManager.nodeCommand(NodeManager.NodeCommandType.Create, params);
      verify(shellProcessHandler, times(1)).run(eq(expectedCommand), anyMap(), anyString());
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
    for (TestData t : testData) {
      AnsibleCreateServer.Params params = new AnsibleCreateServer.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().universeUUID, ApiUtils.mockUniverseUpdater(t.cloudType)));
      addValidDeviceInfo(t, params);
      params.subnetId = t.zone.subnet;
      if (t.cloudType.equals(Common.CloudType.aws)) {
        params.cmkArn = "cmkArn";
      }

      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Create, params, t));
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Create, params);
      verify(shellProcessHandler, times(1)).run(eq(expectedCommand), anyMap(), anyString());
    }
  }

  @Test
  public void testCreateNodeCommandWithIAM() {
    for (TestData t : testData) {
      AnsibleCreateServer.Params params = new AnsibleCreateServer.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().universeUUID, ApiUtils.mockUniverseUpdater(t.cloudType)));
      addValidDeviceInfo(t, params);
      params.subnetId = t.zone.subnet;
      if (t.cloudType.equals(Common.CloudType.aws)) {
        params.ipArnString = "ipArnString";
      }

      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Create, params, t));
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Create, params);
      verify(shellProcessHandler, times(1)).run(eq(expectedCommand), anyMap(), anyString());
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
                createUniverse().universeUUID, ApiUtils.mockUniverseUpdater(t.cloudType)));
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
                createUniverse().universeUUID, ApiUtils.mockUniverseUpdater(t.cloudType)));
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
        fail();
      } catch (RuntimeException re) {
        assertThat(re.getMessage(), is("NodeTaskParams is not AnsibleConfigureServers.Params"));
      }
    }
  }

  @Test
  public void testConfigureNodeCommand() {
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().universeUUID, ApiUtils.mockUniverseUpdater(t.cloudType)));
      addValidDeviceInfo(t, params);
      params.isMasterInShellMode = true;
      params.ybSoftwareVersion = "0.0.1";
      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Configure, params, t));

      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      verify(shellProcessHandler, times(1)).run(eq(expectedCommand), anyMap(), anyString());
    }
  }

  @Test
  public void testConfigureNodeCommandWithRf1() {
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().universeUUID, ApiUtils.mockUniverseUpdater(t.cloudType)));
      addValidDeviceInfo(t, params);
      params.isMasterInShellMode = true;
      params.ybSoftwareVersion = "0.0.1";
      List<String> expectedCommand = t.baseCommand;
      UserIntent userIntent = new UserIntent();
      userIntent.replicationFactor = 1;
      expectedCommand.addAll(
          nodeCommand(NodeManager.NodeCommandType.Configure, params, t, userIntent));

      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      verify(shellProcessHandler, times(1)).run(eq(expectedCommand), anyMap(), anyString());
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
              createUniverse().universeUUID, ApiUtils.mockUniverseUpdater(t.cloudType)));
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
    for (TestData t : testData) {
      // Create AccessKey
      AccessKey.KeyInfo keyInfo = new AccessKey.KeyInfo();
      keyInfo.privateKey = "/path/to/private.key";
      keyInfo.publicKey = "/path/to/public.key";
      keyInfo.vaultFile = "/path/to/vault_file";
      keyInfo.vaultPasswordFile = "/path/to/vault_password";
      keyInfo.sshPort = 3333;
      getOrCreate(t.provider.uuid, "demo-access", keyInfo);

      // Set up task params
      UniverseDefinitionTaskParams.UserIntent userIntent =
          new UniverseDefinitionTaskParams.UserIntent();
      userIntent.numNodes = 3;
      userIntent.accessKeyCode = "demo-access";
      userIntent.regionList = new ArrayList<>();
      userIntent.regionList.add(t.region.uuid);
      userIntent.providerType = t.cloudType;
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().universeUUID,
              ApiUtils.mockUniverseUpdater(userIntent, true /* setMasters */)));
      addValidDeviceInfo(t, params);
      params.isMasterInShellMode = true;
      params.ybSoftwareVersion = "0.0.1";

      // Set up expected command
      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Configure, params, t));
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
      expectedCommand.addAll(expectedCommand.size() - 5, accessKeyCommand);

      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      verify(shellProcessHandler, times(1)).run(eq(expectedCommand), anyMap(), anyString());
    }
  }

  @Test
  public void testConfigureNodeCommandWithSetTxnTableWaitCountFlag() {
    for (TestData t : testData) {
      // Set up TaskParams
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().universeUUID, ApiUtils.mockUniverseUpdater(t.cloudType)));
      addValidDeviceInfo(t, params);
      params.ybSoftwareVersion = "0.0.1";
      params.setTxnTableWaitCountFlag = true;

      // Set up UserIntent
      UserIntent userIntent = new UserIntent();
      userIntent.numNodes = 3;

      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(
          nodeCommand(NodeManager.NodeCommandType.Configure, params, t, userIntent));

      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      verify(shellProcessHandler, times(1)).run(eq(expectedCommand), anyMap(), anyString());
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
              createUniverse().universeUUID, ApiUtils.mockUniverseUpdater(t.cloudType)));
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
              createUniverse().universeUUID, ApiUtils.mockUniverseUpdater(t.cloudType)));
      params.type = Software;
      params.ybSoftwareVersion = "0.0.1";

      for (UniverseDefinitionTaskBase.ServerType type :
          UniverseDefinitionTaskBase.ServerType.values()) {
        try {
          // master and tserver are valid process types.
          if (ImmutableList.of(MASTER, TSERVER).contains(type)) {
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
              createUniverse().universeUUID, ApiUtils.mockUniverseUpdater(t.cloudType)));
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
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().universeUUID, ApiUtils.mockUniverseUpdater(t.cloudType)));
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
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Configure, params, t));
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      verify(shellProcessHandler, times(1)).run(eq(expectedCommand), anyMap(), anyString());
    }
  }

  @Test
  public void testSoftwareUpgradeWithInstallNodeCommand() {
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().universeUUID, ApiUtils.mockUniverseUpdater(t.cloudType)));
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
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Configure, params, t));
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      verify(shellProcessHandler, times(1)).run(eq(expectedCommand), anyMap(), anyString());
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
              createUniverse().universeUUID, ApiUtils.mockUniverseUpdater(t.cloudType)));
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
    for (TestData t : testData) {
      for (String serverType : ImmutableList.of(MASTER.toString(), TSERVER.toString())) {
        AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
        buildValidParams(
            t,
            params,
            Universe.saveDetails(
                createUniverse().universeUUID, ApiUtils.mockUniverseUpdater(t.cloudType)));
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
        expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Configure, params, t));
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
        verify(shellProcessHandler, times(1)).run(eq(expectedCommand), anyMap(), anyString());
      }
    }
  }

  @Test
  public void testGFlagsUpgradeNullProcessType() {
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().universeUUID, ApiUtils.mockUniverseUpdater(t.cloudType)));
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
              createUniverse().universeUUID, ApiUtils.mockUniverseUpdater(t.cloudType)));
      HashMap<String, String> gflags = new HashMap<>();
      gflags.put("gflagName", "gflagValue");
      params.gflags = gflags;
      params.type = GFlags;
      params.isMasterInShellMode = true;

      for (UniverseDefinitionTaskBase.ServerType type :
          UniverseDefinitionTaskBase.ServerType.values()) {
        try {
          // master and tserver are valid process types.
          if (ImmutableList.of(MASTER, TSERVER).contains(type)) {
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
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().universeUUID, ApiUtils.mockUniverseUpdater(t.cloudType)));
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
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Configure, params, t));
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      verify(shellProcessHandler, times(1)).run(eq(expectedCommand), anyMap(), anyString());
    }
  }

  @Test
  @Parameters({"true, false", "false, true", "true, true"})
  @TestCaseName("{method}(enabledYSQL:{0},enabledYCQL:{1}) [{index}]")
  public void testEnableYSQLNodeCommand(boolean ysqlEnabled, boolean ycqlEnabled) {
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().universeUUID, ApiUtils.mockUniverseUpdater(t.cloudType)));
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
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Configure, params, t));
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      verify(shellProcessHandler, times(1)).run(eq(expectedCommand), anyMap(), anyString());
    }
  }

  @Test
  public void testEnableNodeToNodeTLSNodeCommand() throws IOException, NoSuchAlgorithmException {
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
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Configure, params, t));
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      verify(shellProcessHandler, times(1))
          .run(
              captor.capture(),
              anyMap(),
              eq(
                  String.format(
                      "bin/ybcloud.sh %s --region %s instance configure %s",
                      t.region.provider.name, t.region.code, t.node.getNodeName())));
      List<String> actualCommand = captor.getValue();
      int serverCertPathIndex = expectedCommand.indexOf("--server_cert_path") + 1;
      int serverKeyPathIndex = expectedCommand.indexOf("--server_key_path") + 1;
      expectedCommand.set(serverCertPathIndex, actualCommand.get(serverCertPathIndex));
      expectedCommand.set(serverKeyPathIndex, actualCommand.get(serverKeyPathIndex));
      assertEquals(expectedCommand, actualCommand);
    }
  }

  @Test
  public void testCustomCertNodeCommand() throws IOException, NoSuchAlgorithmException {
    Customer customer = ModelFactory.testCustomer();
    ModelFactory.newProvider(customer, Common.CloudType.onprem);
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
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Configure, params, t));
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      verify(shellProcessHandler, times(1))
          .run(
              captor.capture(),
              anyMap(),
              eq(
                  String.format(
                      "bin/ybcloud.sh %s --region %s instance configure %s",
                      t.region.provider.name, t.region.code, t.node.getNodeName())));
      List<String> actualCommand = captor.getValue();
      int serverCertPathIndex = expectedCommand.indexOf("--server_cert_path") + 1;
      int serverKeyPathIndex = expectedCommand.indexOf("--server_key_path") + 1;
      expectedCommand.set(serverCertPathIndex, actualCommand.get(serverCertPathIndex));
      expectedCommand.set(serverKeyPathIndex, actualCommand.get(serverKeyPathIndex));
      assertEquals(expectedCommand, actualCommand);
    }
  }

  @Test
  public void testEnableClientToNodeTLSNodeCommand() throws IOException, NoSuchAlgorithmException {
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().universeUUID, ApiUtils.mockUniverseUpdater(t.cloudType)));
      params.type = Everything;
      params.ybSoftwareVersion = "0.0.1";
      params.enableClientToNodeEncrypt = true;
      params.allowInsecure = false;
      params.rootCA = createUniverseWithCert(t, params);
      params.deviceInfo = new DeviceInfo();
      params.deviceInfo.numVolumes = 1;
      if (t.cloudType == CloudType.onprem) {
        params.deviceInfo.mountPoints = fakeMountPaths;
      }
      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Configure, params, t));
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      verify(shellProcessHandler, times(1))
          .run(
              captor.capture(),
              anyMap(),
              eq(
                  String.format(
                      "bin/ybcloud.sh %s --region %s instance configure %s",
                      t.region.provider.name, t.region.code, t.node.getNodeName())));
      List<String> actualCommand = captor.getValue();
      int serverCertPathIndex = expectedCommand.indexOf("--server_cert_path") + 1;
      int serverKeyPathIndex = expectedCommand.indexOf("--server_key_path") + 1;
      expectedCommand.set(serverCertPathIndex, actualCommand.get(serverCertPathIndex));
      expectedCommand.set(serverKeyPathIndex, actualCommand.get(serverKeyPathIndex));
      assertEquals(expectedCommand, actualCommand);
    }
  }

  // /temp/root.crt

  @Captor private ArgumentCaptor<List<String>> captor;

  @Test
  public void testEnableAllTLSNodeCommand() throws IOException, NoSuchAlgorithmException {
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().universeUUID, ApiUtils.mockUniverseUpdater(t.cloudType)));
      params.type = Everything;
      params.ybSoftwareVersion = "0.0.1";
      params.enableNodeToNodeEncrypt = true;
      params.enableClientToNodeEncrypt = true;
      params.allowInsecure = false;
      params.rootCA = createUniverseWithCert(t, params);
      params.deviceInfo = new DeviceInfo();
      params.deviceInfo.numVolumes = 1;
      if (t.cloudType == CloudType.onprem) {
        params.deviceInfo.mountPoints = fakeMountPaths;
      }
      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Configure, params, t));
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      verify(shellProcessHandler, times(1))
          .run(
              captor.capture(),
              anyMap(),
              eq(
                  String.format(
                      "bin/ybcloud.sh %s --region %s instance configure %s",
                      t.region.provider.name, t.region.code, t.node.getNodeName())));
      List<String> actualCommand = captor.getValue();
      int serverCertPathIndex = expectedCommand.indexOf("--server_cert_path") + 1;
      int serverKeyPathIndex = expectedCommand.indexOf("--server_key_path") + 1;
      expectedCommand.set(serverCertPathIndex, actualCommand.get(serverCertPathIndex));
      expectedCommand.set(serverKeyPathIndex, actualCommand.get(serverKeyPathIndex));
      assertEquals(expectedCommand, actualCommand);
    }
  }

  @Test
  public void testGlobalDefaultCallhome() {
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().universeUUID, ApiUtils.mockUniverseUpdater(t.cloudType)));
      params.type = Everything;
      params.ybSoftwareVersion = "0.0.1";
      params.callhomeLevel = CallHomeManager.CollectionLevel.valueOf("NONE");
      params.deviceInfo = new DeviceInfo();
      params.deviceInfo.numVolumes = 1;
      if (t.cloudType == CloudType.onprem) {
        params.deviceInfo.mountPoints = fakeMountPaths;
      }
      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Configure, params, t));
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      verify(shellProcessHandler, times(1)).run(eq(expectedCommand), anyMap(), anyString());
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
                createUniverse().universeUUID, ApiUtils.mockUniverseUpdater(t.cloudType)));
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Destroy, params);
        fail();
      } catch (RuntimeException re) {
        assertThat(re.getMessage(), is("NodeTaskParams is not AnsibleDestroyServer.Params"));
      }
    }
  }

  @Test
  public void testDestroyNodeCommand() {
    for (TestData t : testData) {
      AnsibleDestroyServer.Params params = new AnsibleDestroyServer.Params();
      params.nodeIP = "1.1.1.1";
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().universeUUID, ApiUtils.mockUniverseUpdater(t.cloudType)));
      assertNotNull(params.nodeUuid);
      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Destroy, params, t));
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Destroy, params);
      verify(shellProcessHandler, times(1)).run(eq(expectedCommand), anyMap(), anyString());
    }
  }

  @Test
  public void testListNodeCommand() {
    for (TestData t : testData) {
      NodeTaskParams params = new NodeTaskParams();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().universeUUID, ApiUtils.mockUniverseUpdater(t.cloudType)));

      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.List, params, t));
      nodeManager.nodeCommand(NodeManager.NodeCommandType.List, params);
      verify(shellProcessHandler, times(1)).run(eq(expectedCommand), anyMap(), anyString());
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
                createUniverse().universeUUID, ApiUtils.mockUniverseUpdater(t.cloudType)));
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Control, params);
        fail();
      } catch (RuntimeException re) {
        assertThat(re.getMessage(), is("NodeTaskParams is not AnsibleClusterServerCtl.Params"));
      }
    }
  }

  @Test
  public void testControlNodeCommand() {
    for (TestData t : testData) {
      AnsibleClusterServerCtl.Params params = new AnsibleClusterServerCtl.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().universeUUID, ApiUtils.mockUniverseUpdater(t.cloudType)));
      params.process = "master";
      params.command = "create";

      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Control, params, t));
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Control, params);
      verify(shellProcessHandler, times(1)).run(eq(expectedCommand), anyMap(), anyString());
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
    for (TestData t : testData) {
      NodeTaskParams params = new NodeTaskParams();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().universeUUID, ApiUtils.mockUniverseUpdater(t.cloudType)));

      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.List, params, t));
      nodeManager.nodeCommand(NodeManager.NodeCommandType.List, params);
      verify(shellProcessHandler, times(1)).run(eq(expectedCommand), anyMap(), anyString());
    }
  }

  @Test
  public void testSetInstanceTags() {
    for (TestData t : testData) {
      InstanceActions.Params params = new InstanceActions.Params();
      UUID univUUID = createUniverse().universeUUID;
      Universe universe = Universe.saveDetails(univUUID, ApiUtils.mockUniverseUpdater(t.cloudType));
      buildValidParams(t, params, universe);
      ApiUtils.insertInstanceTags(univUUID);
      setInstanceTags(params);
      if (Provider.InstanceTagsEnabledProviders.contains(t.cloudType)) {
        List<String> expectedCommand = t.baseCommand;
        expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Tags, params, t));
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Tags, params);
        verify(shellProcessHandler, times(1)).run(eq(expectedCommand), anyMap(), anyString());
      } else {
        assertFails(
            () -> nodeManager.nodeCommand(NodeManager.NodeCommandType.Tags, params),
            "Tags are unsupported for " + t.cloudType.name());
      }
    }
  }

  @Test
  public void testRemoveInstanceTags() {
    for (TestData t : testData) {
      InstanceActions.Params params = new InstanceActions.Params();
      UUID univUUID = createUniverse().universeUUID;
      Universe universe = Universe.saveDetails(univUUID, ApiUtils.mockUniverseUpdater(t.cloudType));
      buildValidParams(t, params, universe);
      ApiUtils.insertInstanceTags(univUUID);
      setInstanceTags(params);
      params.deleteTags = "Remove,Also";
      if (Provider.InstanceTagsEnabledProviders.contains(t.cloudType)) {
        List<String> expectedCommand = t.baseCommand;
        expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Tags, params, t));
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Tags, params);
        verify(shellProcessHandler, times(1)).run(eq(expectedCommand), anyMap(), anyString());
      } else {
        assertFails(
            () -> nodeManager.nodeCommand(NodeManager.NodeCommandType.Tags, params),
            "Tags are unsupported for " + t.cloudType.name());
      }
    }
  }

  @Test
  public void testEmptyInstanceTags() {
    for (TestData t : testData) {
      InstanceActions.Params params = new InstanceActions.Params();
      UUID univUUID = createUniverse().universeUUID;
      Universe universe = Universe.saveDetails(univUUID, ApiUtils.mockUniverseUpdater(t.cloudType));
      buildValidParams(t, params, universe);
      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Tags, params, t));
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
    }
  }

  @Test
  public void testDiskUpdateCommand() {
    for (TestData t : testData) {
      InstanceActions.Params params = new InstanceActions.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().universeUUID, ApiUtils.mockUniverseUpdater(t.cloudType)));
      addValidDeviceInfo(t, params);
      params.deviceInfo = new DeviceInfo();
      params.deviceInfo.numVolumes = 1;
      if (t.cloudType == CloudType.onprem) {
        params.deviceInfo.mountPoints = fakeMountPaths;
      }
      params.deviceInfo.volumeSize = 500;
      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Disk_Update, params, t));
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Disk_Update, params);
      verify(shellProcessHandler, times(1)).run(eq(expectedCommand), anyMap(), anyString());
    }
  }

  @Test
  public void testConfigureNodeCommandInShellMode() {
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().universeUUID, ApiUtils.mockUniverseUpdater(t.cloudType)));
      params.isMasterInShellMode = false;
      params.ybSoftwareVersion = "0.0.1";
      params.deviceInfo = new DeviceInfo();
      params.deviceInfo.numVolumes = 1;
      if (t.cloudType == CloudType.onprem) {
        params.deviceInfo.mountPoints = fakeMountPaths;
      }

      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Configure, params, t));
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      verify(shellProcessHandler, times(1)).run(eq(expectedCommand), anyMap(), anyString());
    }
  }

  @Test
  @Parameters({"true", "false"})
  public void testYEDISNodeCommand(boolean enableYEDIS) {
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().universeUUID, ApiUtils.mockUniverseUpdater(t.cloudType)));
      params.type = Everything;
      params.ybSoftwareVersion = "0.0.1";
      params.enableYEDIS = enableYEDIS;
      params.deviceInfo = new DeviceInfo();
      params.deviceInfo.numVolumes = 1;
      if (t.cloudType == CloudType.onprem) {
        params.deviceInfo.mountPoints = fakeMountPaths;
      }
      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Configure, params, t));
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      verify(shellProcessHandler, times(1)).run(eq(expectedCommand), anyMap(), anyString());
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
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();

      Universe universe = createUniverse();
      universe.getUniverseDetails().nodeDetailsSet.add(createNode(true));
      assertFalse(StringUtils.isEmpty(universe.getMasterAddresses()));

      buildValidParams(
          t,
          params,
          Universe.saveDetails(universe.universeUUID, ApiUtils.mockUniverseUpdater(t.cloudType)));
      addValidDeviceInfo(t, params);
      params.type = GFlags;
      params.isMasterInShellMode = isMasterInShellMode;
      params.updateMasterAddrsOnly = true;
      params.isMaster = serverType.equals(MASTER.toString());
      params.setProperty("processType", serverType);

      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Configure, params, t));
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      verify(shellProcessHandler, times(1)).run(eq(expectedCommand), anyMap(), anyString());
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
              createUniverse().universeUUID, ApiUtils.mockUniverseUpdater(data.cloudType)));
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
              createUniverse().universeUUID, ApiUtils.mockUniverseUpdater(data.cloudType)));
      params.type = ToggleTls;

      for (UniverseDefinitionTaskBase.ServerType type :
          UniverseDefinitionTaskBase.ServerType.values()) {
        try {
          // Master and TServer are valid process types
          if (ImmutableList.of(MASTER, TSERVER).contains(type)) {
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
              createUniverse().universeUUID, ApiUtils.mockUniverseUpdater(data.cloudType)));
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
              createUniverse().universeUUID, ApiUtils.mockUniverseUpdater(data.cloudType)));
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
    for (TestData data : testData) {
      if (data.cloudType == Common.CloudType.gcp) {
        data.privateKey = null;
      }

      UUID universeUuid = createUniverse().universeUUID;
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

      try {
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
        List<String> expectedCommand = data.baseCommand;
        expectedCommand.addAll(
            nodeCommand(NodeManager.NodeCommandType.Configure, params, data, userIntent));
        verify(shellProcessHandler, times(1))
            .run(
                captor.capture(),
                anyMap(),
                eq(
                    String.format(
                        "bin/ybcloud.sh %s --region %s instance configure %s",
                        data.region.provider.name, data.region.code, data.node.getNodeName())));
        List<String> actualCommand = captor.getValue();
        int serverCertPathIndex = expectedCommand.indexOf("--server_cert_path") + 1;
        int serverKeyPathIndex = expectedCommand.indexOf("--server_key_path") + 1;
        expectedCommand.set(serverCertPathIndex, actualCommand.get(serverCertPathIndex));
        expectedCommand.set(serverKeyPathIndex, actualCommand.get(serverKeyPathIndex));
        assertEquals(expectedCommand, actualCommand);
      } catch (RuntimeException re) {
        assertNull(re.getMessage());
      }
    }
  }

  @Test
  @Parameters({"0", "-1", "1"})
  @TestCaseName("testToggleTlsRound1GFlagsUpdateWhenNodeToNodeChange:{0}")
  public void testToggleTlsRound1GFlagsUpdate(int nodeToNodeChange) {
    for (TestData data : testData) {
      UUID universeUuid = createUniverse().universeUUID;
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
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      List<String> expectedCommand = data.baseCommand;
      expectedCommand.addAll(
          nodeCommand(NodeManager.NodeCommandType.Configure, params, data, userIntent));
      verify(shellProcessHandler, times(1)).run(eq(expectedCommand), anyMap(), anyString());
    }
  }

  @Test
  @Parameters({"0", "-1", "1"})
  @TestCaseName("testToggleTlsRound2GFlagsUpdateWhenNodeToNodeChange:{0}")
  public void testToggleTlsRound2GFlagsUpdate(int nodeToNodeChange) {
    for (TestData data : testData) {
      UUID universeUuid = createUniverse().universeUUID;
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
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      List<String> expectedCommand = data.baseCommand;
      expectedCommand.addAll(
          nodeCommand(NodeManager.NodeCommandType.Configure, params, data, userIntent));
      verify(shellProcessHandler, times(1)).run(eq(expectedCommand), anyMap(), anyString());
    }
  }

  private UUID createCertificateConfig(CertConfigType certType, UUID customerUUID, String label)
      throws IOException, NoSuchAlgorithmException {
    return createCertificate(certType, customerUUID, label, "", false).uuid;
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
      certUUID = CertificateHelper.createRootCA("foobar", customerUUID, TestHelper.TMP_PATH);
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
              createUniverse().universeUUID, ApiUtils.mockUniverseUpdater(data.cloudType)));
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
              createUniverse().universeUUID, ApiUtils.mockUniverseUpdater(data.cloudType)));
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
              createUniverse().universeUUID, ApiUtils.mockUniverseUpdater(data.cloudType)));
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
              createUniverse().universeUUID, ApiUtils.mockUniverseUpdater(data.cloudType)));

      params.type = Certs;
      params.certRotateAction = CertRotateAction.valueOf(action);
      params.rootCARotationType = CertRotationType.RootCert;
      params.rootCA =
          createCertificateConfig(
              CertConfigType.CustomServerCert, data.provider.customerUUID, params.nodePrefix);
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
    for (TestData data : testData) {
      UUID universeUuid = createUniverse().universeUUID;
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
              CertConfigType.valueOf(certType), data.provider.customerUUID, params.nodePrefix);
      params.setProperty("processType", MASTER.toString());
      params.deviceInfo = new DeviceInfo();
      params.deviceInfo.numVolumes = 1;
      if (data.cloudType == CloudType.onprem) {
        params.deviceInfo.mountPoints = fakeMountPaths;
      }
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      List<String> expectedCommand = data.baseCommand;
      expectedCommand.addAll(
          nodeCommand(NodeManager.NodeCommandType.Configure, params, data, userIntent));
      verify(shellProcessHandler, times(1)).run(eq(expectedCommand), anyMap(), anyString());
    }
  }

  @Test
  @Parameters({"true, true", "true, false", "false, true", "false, false"})
  @TestCaseName("testCertsRotateCopyCerts_rootCA:{0}_clientRootCA:{1}")
  public void testCertsRotateCopyCerts(boolean isRootCA, boolean isClientRootCA)
      throws IOException, NoSuchAlgorithmException {
    for (TestData data : testData) {
      UUID universeUuid = createUniverse().universeUUID;
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
                CertConfigType.SelfSigned, data.provider.customerUUID, params.nodePrefix);
      }
      if (isClientRootCA) {
        params.clientRootCARotationType = CertRotationType.RootCert;
        params.clientRootCA =
            createCertificateConfig(
                CertConfigType.SelfSigned, data.provider.customerUUID, params.nodePrefix);
      }
      params.setProperty("processType", MASTER.toString());

      try {
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
        List<String> expectedCommand = data.baseCommand;
        expectedCommand.addAll(
            nodeCommand(NodeManager.NodeCommandType.Configure, params, data, userIntent));
        verify(shellProcessHandler, times(1))
            .run(
                captor.capture(),
                anyMap(),
                eq(
                    String.format(
                        "bin/ybcloud.sh %s --region %s instance configure %s",
                        data.region.provider.name, data.region.code, data.node.getNodeName())));
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
    final String flagName = NodeManager.VERIFY_SERVER_ENDPOINT_GFLAG;
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
    for (TestData t : testData) {
      // Create AccessKey
      AccessKey.KeyInfo keyInfo = new AccessKey.KeyInfo();
      keyInfo.privateKey = "/path/to/private.key";
      keyInfo.publicKey = "/path/to/public.key";
      keyInfo.vaultFile = "/path/to/vault_file";
      keyInfo.vaultPasswordFile = "/path/to/vault_password";
      keyInfo.sshPort = 3333;
      keyInfo.installNodeExporter = false;
      getOrCreate(t.provider.uuid, "demo-access", keyInfo);

      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().universeUUID, ApiUtils.mockUniverseUpdater(t.cloudType)));

      Universe.saveDetails(
          params.universeUUID,
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
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.CronCheck, params, t));
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
      expectedCommand.addAll(expectedCommand.size() - 1, accessKeyCommands);

      nodeManager.nodeCommand(NodeManager.NodeCommandType.CronCheck, params);
      verify(shellProcessHandler, times(1)).run(eq(expectedCommand), anyMap(), anyString());
    }
  }

  @Test
  public void testPrecheckNotCheckingSelfSignedCertificates()
      throws IOException, NoSuchAlgorithmException {
    UUID customerUUID = testData.get(0).provider.customerUUID;
    UUID certificateUUID = createCertificateConfig(CertConfigType.SelfSigned, customerUUID, "SS");
    List<String> cmds = createPrecheckCommandForCerts(certificateUUID, null);
    checkArguments(cmds, PRECHECK_CERT_PATHS); // Check no args
  }

  @Test
  public void testPrecheckNotCheckingTwoSelfSignedCertificates()
      throws IOException, NoSuchAlgorithmException {
    UUID customerUUID = testData.get(0).provider.customerUUID;
    UUID certificateUUID1 = createCertificateConfig(CertConfigType.SelfSigned, customerUUID, "SS");
    UUID certificateUUID2 = createCertificateConfig(CertConfigType.SelfSigned, customerUUID, "SS");
    List<String> cmds = createPrecheckCommandForCerts(certificateUUID1, certificateUUID2);
    checkArguments(cmds, PRECHECK_CERT_PATHS); // Check no args
  }

  @Test
  public void testPrecheckCheckCertificates() throws IOException, NoSuchAlgorithmException {
    UUID customerUUID = testData.get(0).provider.customerUUID;
    CertificateInfo certificateInfo =
        createCertificate(CertConfigType.CustomCertHostPath, customerUUID, "CS", "", false);
    List<String> cmds = createPrecheckCommandForCerts(certificateInfo.uuid, null);

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
    UUID customerUUID = testData.get(0).provider.customerUUID;
    CertificateInfo certificateInfo =
        createCertificate(CertConfigType.CustomCertHostPath, customerUUID, "CS", "", true);
    List<String> cmds = createPrecheckCommandForCerts(certificateInfo.uuid, certificateInfo.uuid);

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
    UUID customerUUID = testData.get(0).provider.customerUUID;
    CertificateInfo certificateInfo =
        createCertificate(CertConfigType.CustomCertHostPath, customerUUID, "CS", "", true);
    List<String> cmds = createPrecheckCommandForCerts(certificateInfo.uuid, null);

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
    UUID customerUUID = testData.get(0).provider.customerUUID;
    CertificateInfo cert =
        createCertificate(CertConfigType.CustomCertHostPath, customerUUID, "CS", "", true);
    CertificateInfo cert2 =
        createCertificate(CertConfigType.CustomCertHostPath, customerUUID, "CS", "", true);
    List<String> cmds = createPrecheckCommandForCerts(cert.uuid, cert2.uuid);

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
    UUID customerUUID = testData.get(0).provider.customerUUID;
    CertificateInfo cert =
        createCertificate(CertConfigType.CustomCertHostPath, customerUUID, "CS", "", false);
    CertificateInfo cert2 =
        createCertificate(CertConfigType.CustomCertHostPath, customerUUID, "CS", "", false);
    List<String> cmds = createPrecheckCommandForCerts(cert.uuid, cert2.uuid);

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
    UUID customerUUID = testData.get(0).provider.customerUUID;
    CertificateInfo cert =
        createCertificate(CertConfigType.CustomCertHostPath, customerUUID, "CS", "", false);
    List<String> cmds =
        createPrecheckCommandForCerts(
            cert.uuid,
            null,
            params -> {
              Universe.saveDetails(
                  params.universeUUID,
                  universe -> {
                    NodeDetails nodeDetails = universe.getNode(params.nodeName);
                    UserIntent userIntent =
                        universe
                            .getUniverseDetails()
                            .getClusterByUuid(nodeDetails.placementUuid)
                            .userIntent;
                    userIntent.tserverGFlags.put(NodeManager.VERIFY_SERVER_ENDPOINT_GFLAG, "false");
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
    for (TestData t : testData) {
      NodeTaskParams params = new NodeTaskParams();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().universeUUID, ApiUtils.mockUniverseUpdater(t.cloudType)));
      List<String> expectedCommand = new ArrayList<>(t.baseCommand);
      expectedCommand.addAll(
          nodeCommand(NodeManager.NodeCommandType.Delete_Root_Volumes, params, t));
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Delete_Root_Volumes, params);
      verify(shellProcessHandler, times(1)).run(eq(expectedCommand), anyMap(), anyString());
    }
  }

  private void checkArguments(
      List<String> currentArgs, List<String> allPossibleArgs, String... argsExpected) {
    Map<String, String> k2v = mapKeysToValues(currentArgs);
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
    keyInfo.sshPort = 3333;
    keyInfo.airGapInstall = true;
    getOrCreate(onpremTD.provider.uuid, "mock-access-code-key", keyInfo);

    NodeTaskParams nodeTaskParams =
        buildValidParams(
            onpremTD,
            new NodeTaskParams(),
            Universe.saveDetails(
                createUniverse().universeUUID, ApiUtils.mockUniverseUpdater(onpremTD.cloudType)));
    nodeTaskParams.rootCA = certificateUUID1;
    nodeTaskParams.clientRootCA = certificateUUID2;
    nodeTaskParams.rootAndClientRootCASame =
        certificateUUID2 == null || Objects.equals(certificateUUID1, certificateUUID2);

    paramsUpdate.accept(nodeTaskParams);
    Universe.saveDetails(
        nodeTaskParams.universeUUID,
        universe -> {
          NodeDetails nodeDetails = universe.getNode(nodeTaskParams.nodeName);
          UserIntent userIntent =
              universe.getUniverseDetails().getClusterByUuid(nodeDetails.placementUuid).userIntent;
          userIntent.enableNodeToNodeEncrypt = true;
        });
    nodeManager.nodeCommand(NodeManager.NodeCommandType.Precheck, nodeTaskParams);
    ArgumentCaptor<List> arg = ArgumentCaptor.forClass(List.class);
    verify(shellProcessHandler).run(arg.capture(), any(), anyString());

    return new ArrayList<>(arg.getValue());
  }

  private Map<String, String> mapKeysToValues(List<String> args) {
    Map<String, String> result = new HashMap<>();
    for (int i = 0; i < args.size(); i++) {
      String key = args.get(i);
      if (key.startsWith("--")) {
        String value = "";
        if (i < args.size() - 1 && !args.get(i + 1).startsWith("--")) {
          value = args.get(++i);
        }
        result.put(key, value);
      }
    }
    return result;
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
