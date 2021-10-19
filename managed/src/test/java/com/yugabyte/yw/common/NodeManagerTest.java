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
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;
import static org.mockito.ArgumentMatchers.anyMap;
import static org.mockito.Matchers.any;
import static org.mockito.Matchers.anyString;
import static org.mockito.Matchers.eq;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.typesafe.config.Config;
import com.yugabyte.yw.cloud.PublicCloudConstants;
import com.yugabyte.yw.commissioner.Common;
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
import com.yugabyte.yw.common.NodeManager.CertRotateAction;
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
import com.yugabyte.yw.models.CertificateInfo.Type;
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
import java.util.Calendar;
import java.util.Date;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.function.Predicate;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
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
  private final String MASTER_ADDRESSES = "host-n1:7100,host-n2:7100,host-n3:7100";
  private final String fakeMountPath1 = "/fake/path/d0";
  private final String fakeMountPath2 = "/fake/path/d1";
  private final String fakeMountPaths = fakeMountPath1 + "," + fakeMountPath2;
  private final String instanceTypeCode = "fake_instance_type";
  private final String SERVER_CERT_PATH = "/tmp/cert.crt";
  private final String SERVER_KEY_PATH = "/tmp/key.crt";

  private class TestData {
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
        Provider p, Common.CloudType cloud, PublicCloudConstants.StorageType storageType, int idx) {
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
      testDataList.add(new TestData(provider, cloud, PublicCloudConstants.StorageType.GP2, 1));
    } else if (cloud.equals(Common.CloudType.gcp)) {
      testDataList.add(new TestData(provider, cloud, PublicCloudConstants.StorageType.IO1, 2));
    } else {
      testDataList.add(new TestData(provider, cloud, null, 3));
    }
    return testDataList;
  }

  private Universe createUniverse() {
    UUID universeUUID = UUID.randomUUID();
    return ModelFactory.createUniverse("Test Universe - " + universeUUID, universeUUID);
  }

  private void buildValidParams(TestData testData, NodeTaskParams params, Universe universe) {
    params.azUuid = testData.zone.uuid;
    params.instanceType = testData.node.instanceTypeCode;
    params.nodeName = testData.node.getNodeName();
    params.universeUUID = universe.universeUUID;
    params.placementUuid = universe.getUniverseDetails().getPrimaryCluster().uuid;
    params.currentClusterType = ClusterType.PRIMARY;
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

  private NodeTaskParams createInvalidParams(TestData testData) {
    Universe u = createUniverse();
    NodeTaskParams params = new NodeTaskParams();
    params.azUuid = testData.zone.uuid;
    params.nodeName = testData.node.getNodeName();
    params.universeUUID = u.universeUUID;
    return params;
  }

  private AccessKey getOrCreate(UUID providerUUID, String keyCode, AccessKey.KeyInfo keyInfo) {
    AccessKey accessKey = AccessKey.get(providerUUID, keyCode);
    if (accessKey == null) {
      accessKey = AccessKey.create(providerUUID, keyCode, keyInfo);
    }
    return accessKey;
  }

  private List<String> getCertificatePaths(
      AnsibleConfigureServers.Params configureParams, String yb_home_dir) {
    return getCertificatePaths(
        configureParams,
        configureParams.enableNodeToNodeEncrypt
            || (configureParams.rootAndClientRootCASame
                && configureParams.enableClientToNodeEncrypt),
        !configureParams.rootAndClientRootCASame && configureParams.enableClientToNodeEncrypt,
        yb_home_dir);
  }

  private List<String> getCertificatePaths(
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
            CertificateParams.CustomCertInfo customCertInfo = rootCert.getCustomCertInfo();
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
            CertificateParams.CustomCertInfo customCertInfo = clientRootCert.getCustomCertInfo();
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
              TestHelper.TMP_PATH + "/ca.crt",
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
    ReleaseManager.ReleaseMetadata releaseMetadata = new ReleaseManager.ReleaseMetadata();
    releaseMetadata.filePath = "/yb/release.tar.gz";
    when(releaseManager.getReleaseByVersion("0.0.1")).thenReturn(releaseMetadata);

    when(mockConfig.hasPath(NodeManager.BOOT_SCRIPT_PATH)).thenReturn(false);
    when(mockAppConfig.getString(eq("yb.security.default.access.key")))
        .thenReturn(ApiUtils.DEFAULT_ACCESS_KEY_CODE);
    when(runtimeConfigFactory.forProvider(any())).thenReturn(mockConfig);
    when(runtimeConfigFactory.forUniverse(any())).thenReturn(app.config());
    when(mockConfigHelper.getGravitonInstancePrefixList()).thenReturn(ImmutableList.of("m6g."));
    new File(TestHelper.TMP_PATH).mkdirs();
    createTempFile("ca.crt", "test-cert");
  }

  private List<String> nodeCommand(
      NodeManager.NodeCommandType type, NodeTaskParams params, TestData testData) {
    return nodeCommand(type, params, testData, new UserIntent());
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
          if (!createParams.clusters.isEmpty()
              && createParams.clusters.get(0) != null
              && !createParams.clusters.get(0).userIntent.instanceTags.isEmpty()) {
            expectedCommand.add("--instance_tags");
            expectedCommand.add(
                Json.stringify(Json.toJson(createParams.clusters.get(0).userIntent.instanceTags)));
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

        Map<String, String> gflags = new HashMap<>(configureParams.gflags);

        if (configureParams.type == Everything || configureParams.type == Software) {
          gflags.put("placement_uuid", String.valueOf(params.placementUuid));
        }

        if (configureParams.type == Everything) {
          gflags.put("metric_node_name", params.nodeName);
          if (configureParams.isMaster) {
            gflags.put("enable_ysql", Boolean.valueOf(configureParams.enableYSQL).toString());
          }
        }

        if (configureParams.type == Everything) {
          if (configureParams.isMaster) {
            gflags.put("replication_factor", String.valueOf(userIntent.replicationFactor));
          }
          gflags.put("undefok", "enable_ysql");
          if (configureParams.enableYSQL) {
            gflags.put("enable_ysql", "true");
            gflags.put(
                "pgsql_proxy_bind_address",
                String.format(
                    "%s:%s",
                    configureParams.nodeName,
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
            gflags.put(
                "cql_proxy_bind_address",
                String.format(
                    "%s:%s",
                    configureParams.nodeName,
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
          if (configureParams.callhomeLevel != null) {
            gflags.put(
                "callhome_collection_level",
                configureParams.callhomeLevel.toString().toLowerCase());
            if (configureParams.callhomeLevel.toString() == "NONE") {
              gflags.put("callhome_enabled", "false");
            }
          }
          if (configureParams.currentClusterType == UniverseDefinitionTaskParams.ClusterType.PRIMARY
              && configureParams.setTxnTableWaitCountFlag) {
            gflags.put("txn_table_wait_min_ts_count", Integer.toString(userIntent.numNodes));
          }

          if ((configureParams.enableNodeToNodeEncrypt
              || configureParams.enableClientToNodeEncrypt)) {
            if (configureParams.enableNodeToNodeEncrypt) {
              gflags.put("use_node_to_node_encryption", "true");
            }
            if (configureParams.enableClientToNodeEncrypt) {
              gflags.put("use_client_to_server_encryption", "true");
            }
            gflags.put(
                "allow_insecure_connections", configureParams.allowInsecure ? "true" : "false");
            String yb_home_dir = configureParams.getProvider().getYbHome();

            gflags.put("cert_node_filename", params.nodeName);

            if (configureParams.enableNodeToNodeEncrypt
                || (configureParams.rootAndClientRootCASame
                    && configureParams.enableClientToNodeEncrypt)) {
              gflags.put("certs_dir", yb_home_dir + "/yugabyte-tls-config");
            }
            if (!configureParams.rootAndClientRootCASame
                && configureParams.enableClientToNodeEncrypt) {
              gflags.put("certs_for_client_dir", yb_home_dir + "/yugabyte-client-tls-config");
            }
            expectedCommand.addAll(getCertificatePaths(configureParams, yb_home_dir));
          }
          expectedCommand.add("--extra_gflags");
          expectedCommand.add(Json.stringify(Json.toJson(gflags)));
        } else if (configureParams.type == GFlags) {
          String processType = configureParams.getProperty("processType");
          expectedCommand.add("--yb_process_type");
          expectedCommand.add(processType.toLowerCase());

          if (configureParams.updateMasterAddrsOnly) {
            String masterAddresses =
                Universe.getOrBadRequest(configureParams.universeUUID).getMasterAddresses(false);
            if (configureParams.isMasterInShellMode) {
              masterAddresses = "";
            }
            if (processType.equals(ServerType.MASTER.name())) {
              gflags.put("master_addresses", masterAddresses);
            } else {
              gflags.put("tserver_master_addrs", masterAddresses);
            }
          } else {
            gflags.put("placement_uuid", String.valueOf(configureParams.placementUuid));
            gflags.put("metric_node_name", configureParams.nodeName);
          }

          String gflagsJson = Json.stringify(Json.toJson(gflags));
          expectedCommand.add("--replace_gflags");
          expectedCommand.add("--gflags");
          expectedCommand.add(gflagsJson);
          if (configureParams.gflagsToRemove != null && !configureParams.gflagsToRemove.isEmpty()) {
            expectedCommand.add("--gflags_to_remove");
            expectedCommand.add(Json.stringify(Json.toJson(configureParams.gflagsToRemove)));
          }
          expectedCommand.add("--tags");
          expectedCommand.add("override_gflags");
        } else if (configureParams.type == ToggleTls) {
          String nodeToNodeString = String.valueOf(configureParams.enableNodeToNodeEncrypt);
          String clientToNodeString = String.valueOf(configureParams.enableClientToNodeEncrypt);
          String allowInsecureString = String.valueOf(configureParams.allowInsecure);

          String yb_home_dir =
              Provider.getOrBadRequest(UUID.fromString(userIntent.provider)).getYbHome();
          String certsDir = yb_home_dir + "/yugabyte-tls-config";
          String certsForClientDir = yb_home_dir + "/yugabyte-client-tls-config";

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
            expectedCommand.addAll(getCertificatePaths(configureParams, yb_home_dir));
          } else if (UpgradeTaskParams.UpgradeTaskSubType.Round1GFlagsUpdate.name()
              .equals(subType)) {
            gflags = new HashMap<>();
            if (configureParams.nodeToNodeChange > 0) {
              gflags.put("use_node_to_node_encryption", nodeToNodeString);
              gflags.put("use_client_to_server_encryption", clientToNodeString);
              gflags.put("allow_insecure_connections", "true");
              if (CertificateHelper.isRootCARequired(configureParams)) {
                gflags.put("certs_dir", certsDir);
              }
              if (CertificateHelper.isClientRootCARequired(configureParams)) {
                gflags.put("certs_for_client_dir", certsForClientDir);
              }
            } else if (configureParams.nodeToNodeChange < 0) {
              gflags.put("allow_insecure_connections", "true");
            } else {
              gflags.put("use_node_to_node_encryption", nodeToNodeString);
              gflags.put("use_client_to_server_encryption", clientToNodeString);
              gflags.put("allow_insecure_connections", allowInsecureString);
              if (CertificateHelper.isRootCARequired(configureParams)) {
                gflags.put("certs_dir", certsDir);
              }
              if (CertificateHelper.isClientRootCARequired(configureParams)) {
                gflags.put("certs_for_client_dir", certsForClientDir);
              }
            }

            expectedCommand.add("--replace_gflags");
            expectedCommand.add("--gflags");
            expectedCommand.add(Json.stringify(Json.toJson(gflags)));
          } else if (UpgradeTaskParams.UpgradeTaskSubType.Round2GFlagsUpdate.name()
              .equals(subType)) {
            gflags = new HashMap<>();
            if (configureParams.nodeToNodeChange > 0) {
              gflags.put("allow_insecure_connections", allowInsecureString);
            } else if (configureParams.nodeToNodeChange < 0) {
              gflags.put("use_node_to_node_encryption", nodeToNodeString);
              gflags.put("use_client_to_server_encryption", clientToNodeString);
              gflags.put("allow_insecure_connections", allowInsecureString);
              if (CertificateHelper.isRootCARequired(configureParams)) {
                gflags.put("certs_dir", certsDir);
              }
              if (CertificateHelper.isClientRootCARequired(configureParams)) {
                gflags.put("certs_for_client_dir", certsForClientDir);
              }
            }

            expectedCommand.add("--replace_gflags");
            expectedCommand.add("--gflags");
            expectedCommand.add(Json.stringify(Json.toJson(gflags)));
          }
        } else if (configureParams.type == Certs) {
          String processType = configureParams.getProperty("processType");
          expectedCommand.add("--yb_process_type");
          expectedCommand.add(processType.toLowerCase());

          String yb_home_dir =
              Provider.getOrBadRequest(UUID.fromString(userIntent.provider)).getYbHome();
          String certsNodeDir = yb_home_dir + "/yugabyte-tls-config";

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
                if (rootCert.certType == Type.SelfSigned) {
                  rootCertPath = rootCert.certificate;
                  certsLocation = NodeManager.CERT_LOCATION_PLATFORM;
                } else if (rootCert.certType == Type.CustomCertHostPath) {
                  rootCertPath = rootCert.getCustomCertInfo().rootCertPath;
                  certsLocation = NodeManager.CERT_LOCATION_NODE;
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
                      configureParams,
                      configureParams.rootCARotationType != CertRotationType.None,
                      configureParams.clientRootCARotationType != CertRotationType.None,
                      yb_home_dir));
              break;
          }
        } else {
          expectedCommand.add("--extra_gflags");
          Map<String, String> gflags1 = new HashMap<>(configureParams.gflags);
          gflags1.put("placement_uuid", String.valueOf(params.placementUuid));
          expectedCommand.add(Json.stringify(Json.toJson(gflags1)));
        }
        break;
      case Destroy:
        expectedCommand.add("--instance_type");
        expectedCommand.add(instanceTypeCode);
        expectedCommand.add("--node_ip");
        expectedCommand.add("1.1.1.1");
        break;
      case Tags:
        InstanceActions.Params tagsParams = (InstanceActions.Params) params;
        if (Provider.InstanceTagsEnabledProviders.contains(cloud)) {
          expectedCommand.add("--instance_tags");
          // The quotes in format is needed here, so cannot use instanceTags.toString().
          expectedCommand.add("{\"Cust\":\"Test\"}");
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
        }
      }
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
    UserIntent userIntent = new UserIntent();
    userIntent.instanceTags = ImmutableMap.of("Cust", "Test");
    params.clusters.add(new Cluster(ClusterType.PRIMARY, userIntent));
  }

  private void runAndTestProvisionWithAccessKeyAndSG(String sgId) {
    for (TestData t : testData) {
      t.region.setSecurityGroupId(sgId);
      t.region.save();
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
      int accessKeyIndexOffset = 5;
      if (t.cloudType.equals(Common.CloudType.aws)
          && params.deviceInfo.storageType.equals(PublicCloudConstants.StorageType.IO1)) {
        accessKeyIndexOffset += 2;
      }

      List<String> expectedCommand = new ArrayList<>(t.baseCommand);
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Provision, params, t));
      List<String> accessKeyCommands =
          new ArrayList<String>(
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
      t.region.save();
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
      AnsibleCreateServer.Params params = new AnsibleCreateServer.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().universeUUID, ApiUtils.mockUniverseUpdater(userIntent)));
      addValidDeviceInfo(t, params);

      // Set up expected command
      int accessKeyIndexOffset = 5;
      if (t.cloudType.equals(Common.CloudType.aws)) {
        accessKeyIndexOffset += 2;
        if (params.deviceInfo.storageType.equals(PublicCloudConstants.StorageType.IO1)) {
          accessKeyIndexOffset += 2;
        }
      }

      List<String> expectedCommand = new ArrayList<>(t.baseCommand);
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Create, params, t));
      List<String> accessKeyCommands =
          new ArrayList<String>(
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
      userIntent.regionList = new ArrayList<UUID>();
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
  public void testGFlagsUpgradeWithEmptyGFlagsNodeCommand() {
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().universeUUID, ApiUtils.mockUniverseUpdater(t.cloudType)));
      params.type = GFlags;

      try {
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
        fail();
      } catch (RuntimeException re) {
        assertThat(re.getMessage(), allOf(notNullValue(), containsString("GFlags data provided")));
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

      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Configure, params, t));
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      verify(shellProcessHandler, times(1)).run(eq(expectedCommand), anyMap(), anyString());
    }
  }

  @Test
  public void testEnableYSQLNodeCommand() {
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().universeUUID, ApiUtils.mockUniverseUpdater(t.cloudType)));
      params.type = Everything;
      params.ybSoftwareVersion = "0.0.1";
      params.enableYSQL = true;
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
    Provider provider = ModelFactory.newProvider(customer, Common.CloudType.onprem);
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
      buildValidParams(t, params, createUniverse());
      buildValidParams(
          t,
          params,
          Universe.saveDetails(
              createUniverse().universeUUID, ApiUtils.mockUniverseUpdater(t.cloudType)));
      params.nodeIP = "1.1.1.1";

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
      if (Provider.InstanceTagsEnabledProviders.contains(t.cloudType)) {
        ApiUtils.insertInstanceTags(univUUID);
        setInstanceTags(params);
      }
      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Tags, params, t));
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Tags, params);
      verify(shellProcessHandler, times(1)).run(eq(expectedCommand), anyMap(), anyString());
    }
  }

  @Test
  public void testRemoveInstanceTags() {
    for (TestData t : testData) {
      InstanceActions.Params params = new InstanceActions.Params();
      UUID univUUID = createUniverse().universeUUID;
      Universe universe = Universe.saveDetails(univUUID, ApiUtils.mockUniverseUpdater(t.cloudType));
      buildValidParams(t, params, universe);
      if (Provider.InstanceTagsEnabledProviders.contains(t.cloudType)) {
        ApiUtils.insertInstanceTags(univUUID);
        setInstanceTags(params);
        params.deleteTags = "Remove,Also";
      }
      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Tags, params, t));
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Tags, params);
      verify(shellProcessHandler, times(1)).run(eq(expectedCommand), anyMap(), anyString());
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
          assertThat(re.getMessage(), allOf(notNullValue(), is("Invalid instance tags")));
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
      testGFlagsInCommand(expectedCommand, params.isMaster, isMasterInShellMode);
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      verify(shellProcessHandler, times(1)).run(eq(expectedCommand), anyMap(), anyString());
    }
  }

  private void testGFlagsInCommand(
      List<String> command, boolean isMaster, boolean isMasterInShellMode) {
    int gflagsIndex = command.indexOf("--gflags");
    assertNotEquals(-1, gflagsIndex);

    JsonNode obj = Json.parse(command.get(gflagsIndex + 1));
    assertEquals(
        isMasterInShellMode,
        StringUtils.isEmpty(
            obj.get(isMaster ? "master_addresses" : "tserver_master_addrs").asText()));
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
        assertThat(re.getMessage(), allOf(notNullValue(), containsString("No changes needed")));
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

      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      List<String> expectedCommand = data.baseCommand;
      expectedCommand.addAll(
          nodeCommand(NodeManager.NodeCommandType.Configure, params, data, userIntent));
      verify(shellProcessHandler, times(1)).run(eq(expectedCommand), anyMap(), anyString());
    }
  }

  private UUID createCertificate(Type certType, UUID customerUUID, String label)
      throws IOException, NoSuchAlgorithmException {
    UUID certUUID = UUID.randomUUID();
    Calendar cal = Calendar.getInstance();
    Date today = cal.getTime();
    cal.add(Calendar.YEAR, 1);
    Date nextYear = cal.getTime();
    if (certType == Type.SelfSigned) {
      certUUID = CertificateHelper.createRootCA("foobar", customerUUID, TestHelper.TMP_PATH);
    } else if (certType == Type.CustomCertHostPath) {
      CertificateParams.CustomCertInfo customCertInfo = new CertificateParams.CustomCertInfo();
      customCertInfo.rootCertPath = "/path/to/cert.crt";
      customCertInfo.nodeCertPath = "/path/to/rootcert.crt";
      customCertInfo.nodeKeyPath = "/path/to/nodecert.crt";
      CertificateInfo.create(
          certUUID,
          customerUUID,
          label,
          today,
          nextYear,
          TestHelper.TMP_PATH + "/ca.crt",
          customCertInfo);
    } else if (certType == Type.CustomServerCert) {
      CertificateInfo.create(
          certUUID,
          customerUUID,
          label,
          today,
          nextYear,
          "privateKey",
          TestHelper.TMP_PATH + "/ca.crt",
          Type.CustomServerCert);
    }

    return certUUID;
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
          createCertificate(Type.CustomServerCert, data.provider.customerUUID, params.nodePrefix);
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
    "REMOVE_OLD_ROOT_CERT, CustomCertHostPath"
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
          createCertificate(Type.valueOf(certType), data.provider.customerUUID, params.nodePrefix);
      params.setProperty("processType", MASTER.toString());

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
            createCertificate(Type.SelfSigned, data.provider.customerUUID, params.nodePrefix);
      }
      if (isClientRootCA) {
        params.clientRootCARotationType = CertRotationType.RootCert;
        params.clientRootCA =
            createCertificate(Type.SelfSigned, data.provider.customerUUID, params.nodePrefix);
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
        assertThat(
            re.getMessage(), containsString("No cert rotation can be done with the given params"));
      }
    }
  }
}
