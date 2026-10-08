// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import static com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType.MASTER;
import static com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType.TSERVER;
import static com.yugabyte.yw.forms.UniverseConfigureTaskParams.ClusterOperationType.CREATE;
import static com.yugabyte.yw.models.TaskInfo.State.Aborted;
import static com.yugabyte.yw.models.TaskInfo.State.Failure;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.hamcrest.CoreMatchers.containsString;
import static org.hamcrest.CoreMatchers.not;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.verifyNoMoreInteractions;
import static org.mockito.Mockito.when;

import api.v2.mappers.UniverseResizeNodeParamsMapper;
import api.v2.models.ClusterResizeNodeSpec;
import api.v2.models.ClusterResizeStorageSpec;
import api.v2.models.PerProcessResizeNodeSpec;
import api.v2.models.UniverseResizeNodes;
import api.v2.models.UniverseResizeNodesCluster;
import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.cloud.PublicCloudConstants;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.MockUpgrade;
import com.yugabyte.yw.commissioner.UpgradeTaskBase;
import com.yugabyte.yw.commissioner.tasks.CommissionerBaseTest;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.commissioner.tasks.subtasks.ChangeInstanceType;
import com.yugabyte.yw.commissioner.tasks.subtasks.DoCapacityReservation;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ProviderInitializer;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.ProviderConfKeys;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.ResizeNodeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.StateTransitionDetails;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.Date;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;
import java.util.stream.Collectors;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import junitparams.converters.Nullable;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.time.DateUtils;
import org.junit.Assert;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import org.yb.client.ListMasterRaftPeersResponse;
import play.libs.Json;

@RunWith(JUnitParamsRunner.class)
@Slf4j
public class ResizeNodeTest extends UpgradeTaskTest {

  @Rule public MockitoRule rule = MockitoJUnit.rule();

  private static final String DEFAULT_INSTANCE_TYPE = "c3.medium";
  private static final String NEW_INSTANCE_TYPE = "c4.medium";
  private static final String NEW_READ_ONLY_INSTANCE_TYPE = "c3.small";

  private static final int DEFAULT_VOLUME_SIZE = 100;
  private static final int NEW_VOLUME_SIZE = 200;
  private static final int DEFAULT_DISK_IOPS = 3000;
  private static final int NEW_DISK_IOPS = 5000;
  private static final int DEFAULT_DISK_THROUGHPUT = 125;
  private static final int NEW_DISK_THROUGHPUT = 250;
  private static final int NEW_CGROUP_SIZE = 10;

  @InjectMocks private ResizeNode resizeNode;

  private Provider ociProvider;

  @Override
  @Before
  public void setUp() {
    super.setUp();
    factory.globalRuntimeConf().setValue("yb.checks.change_master_config.enabled", "false");
    resizeNode.setUserTaskUUID(UUID.randomUUID());
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            universe -> {
              UniverseDefinitionTaskParams.UserIntent userIntent =
                  universe.getUniverseDetails().getPrimaryCluster().userIntent;
              DeviceInfo deviceInfo = new DeviceInfo();
              deviceInfo.numVolumes = 1;
              deviceInfo.volumeSize = DEFAULT_VOLUME_SIZE;
              deviceInfo.storageType = PublicCloudConstants.StorageType.GP3;
              TestUtils.getProviderInitializerForTests(userIntent, defaultProvider.getUuid())
                  .setDeviceInfo(deviceInfo)
                  .setInstanceType(DEFAULT_INSTANCE_TYPE);
              universe
                  .getNodes()
                  .forEach(node -> node.cloudInfo.instance_type = DEFAULT_INSTANCE_TYPE);
            });
    try {
      when(mockYBClient.getClientWithConfig(any())).thenReturn(mockClient);
      ListMasterRaftPeersResponse listMastersResponse = mock(ListMasterRaftPeersResponse.class);
      when(listMastersResponse.getPeersList()).thenReturn(Collections.emptyList());
      when(mockClient.listMasterRaftPeers()).thenReturn(listMastersResponse);
      setCheckNodesAreSafeToTakeDown(mockClient);
    } catch (Exception ignored) {
    }

    createInstanceType(defaultProvider.getUuid(), DEFAULT_INSTANCE_TYPE);
    createInstanceType(defaultProvider.getUuid(), NEW_INSTANCE_TYPE);
    createInstanceType(defaultProvider.getUuid(), NEW_READ_ONLY_INSTANCE_TYPE);

    setUnderReplicatedTabletsMock();
    setFollowerLagMock();
    setLeaderlessTabletsMock();
  }

  @Override
  protected PlacementInfo createPlacementInfo() {
    PlacementInfo placementInfo = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(az1.getUuid(), placementInfo, 1, 1, false);
    PlacementInfoUtil.addPlacementZone(az2.getUuid(), placementInfo, 1, 1, true);
    PlacementInfoUtil.addPlacementZone(az3.getUuid(), placementInfo, 1, 2, false);
    return placementInfo;
  }

  @Parameters({
    "aws, 0, 10, m3.medium, m3.medium, true",
    "gcp, 0, 10, m3.medium, m3.medium, true",
    "aws, 0, 10, m3.medium, c4.medium, true",
    "aws, 0, -10, m3.medium, m3.medium, false", // decrease volume
    "aws, 1, 10, m3.medium, m3.medium, false", // change num of volumes
    "aws, 0, 10, m3.medium, fake_type, false", // unknown instance type
    "aws, 0, 10, i3.instance, m3.medium, false", // ephemeral instance type
    "aws, 0, 10, c5d.instance, m3.medium, false", // ephemeral instance type
    "gcp, 0, 10, scratch, m3.medium, false", // ephemeral instance type
    "aws, 0, 10, m3.medium, c5d.instance, true" // changing to ephemeral is OK
  })
  @Test
  public void testResizeNodeAvailable(
      String cloudTypeStr,
      int numOfVolumesDiff,
      int volumeSizeDiff,
      String curInstanceTypeCode,
      String targetInstanceTypeCode,
      boolean expected) {
    testResizeNodeAvailable(
        cloudTypeStr,
        numOfVolumesDiff,
        volumeSizeDiff,
        curInstanceTypeCode,
        targetInstanceTypeCode,
        false /* volumeIopsChange */,
        false /* volumeThroughputChange */,
        null /* storageType */,
        expected);
  }

  @Parameters({
    "aws, GP3, true",
    "aws, GP2, false",
    "gcp, Persistent, false",
    "gcp, Hyperdisk_Balanced, true",
    "azu, null, false"
  })
  @Test
  public void testResizeNodeIopsThroughputAvailable(
      String cloudTypeStr, @Nullable String storageTypeStr, boolean expected) {
    testResizeNodeAvailable(
        cloudTypeStr,
        0,
        10,
        "m3.medium",
        "m3.medium",
        true /* volumeIopsChange */,
        true /* volumeThroughputChange */,
        storageTypeStr != null ? PublicCloudConstants.StorageType.valueOf(storageTypeStr) : null,
        expected);
  }

  @Parameters({
    "10, Standard_DS2_v2, Standard_DS2_v2, UltraSSD_LRS, false",
    "0, Standard_DS2_v2, Standard_DS4_v2, UltraSSD_LRS, true",
    "10, Standard_DS2_v2, Standard_DS2_v2, StandardSSD_LRS, true",
    "10, Standard_DS2_v2, Standard_DS4_v2, StandardSSD_LRS, true",
    "10, Standard_DS2_v2, Standard_E2as_v5, StandardSSD_LRS, false", // local to no local
    "10, Standard_E2as_v5, Standard_D32as_v5, StandardSSD_LRS, true", // no local to no local
    "10, Standard_D32as_v5, Standard_DS2_v5, StandardSSD_LRS, false", // no local to local
    "0, Standard_DS3_v2, Standard_D8ls_v5, StandardSSD_LRS, false", // DS3_v2 to D8ls_v5 not allowed
    "5000, Standard_DS2_v2, Standard_DS2_v2, StandardSSD_LRS, false",
  })
  @Test
  public void testResizeNodeAzu(
      int volumeSizeDiff,
      String curInstanceTypeCode,
      String targetInstanceTypeCode,
      String volumeType,
      boolean expected) {
    testResizeNodeAvailable(
        Common.CloudType.azu.toString(),
        0,
        volumeSizeDiff,
        curInstanceTypeCode,
        targetInstanceTypeCode,
        false /* volumeIopsChange */,
        false /* volumeThroughputChange */,
        PublicCloudConstants.StorageType.valueOf(volumeType),
        expected);
  }

  private void testResizeNodeAvailable(
      String cloudTypeStr,
      int numOfVolumesDiff,
      int volumeSizeDiff,
      String curInstanceTypeCode,
      String targetInstanceTypeCode,
      boolean volumeIopsChange,
      boolean volumeThroughputChange,
      PublicCloudConstants.StorageType storageType,
      boolean expected) {
    Common.CloudType cloudType = Common.CloudType.valueOf(cloudTypeStr);
    if (storageType == null) {
      storageType = chooseStorageType(cloudType, curInstanceTypeCode.equals("scratch"));
    }
    UniverseDefinitionTaskParams.UserIntent currentIntent =
        createIntent(cloudType, curInstanceTypeCode, storageType);

    UniverseDefinitionTaskParams.UserIntent targetIntent =
        createIntent(cloudType, targetInstanceTypeCode, storageType);
    ProviderInitializer pi =
        TestUtils.existingProviderInitializer(targetIntent)
            .updateDeviceInfo(
                di -> {
                  di.volumeSize += volumeSizeDiff;
                  di.numVolumes += numOfVolumesDiff;
                });

    if (volumeIopsChange) {
      pi.updateDeviceInfo(di -> di.diskIops = NEW_DISK_IOPS);
    }
    if (volumeThroughputChange) {
      pi.updateDeviceInfo(di -> di.throughput = NEW_DISK_THROUGHPUT);
    }
    UUID providerUUID = Util.getSingleProviderUUID(currentIntent);

    createInstanceType(providerUUID, curInstanceTypeCode);
    createInstanceType(providerUUID, targetInstanceTypeCode);
    for (UniverseDefinitionTaskParams.Cluster cluster :
        defaultUniverse.getUniverseDetails().clusters) {
      TestUtils.existingProviderInitializer(cluster.userIntent).setProviderUUID(providerUUID);
    }
    assertEquals(
        expected,
        ResizeNodeParams.checkResizeIsPossible(
            defaultUniverse.getUniverseDetails().getPrimaryCluster().uuid,
            currentIntent,
            targetIntent,
            defaultUniverse,
            mockBaseTaskDependencies.getConfGetter()));
  }

  @Parameters({"aws, GP3", "gcp, Hyperdisk_Balanced", "gcp, Hyperdisk_Extreme"})
  @Test
  public void testBackToBackResizeNode(String providerType, String storageType) {
    Common.CloudType cloudType = Common.CloudType.valueOf(providerType);
    String cooldownParam;
    if (cloudType == Common.CloudType.aws) {
      cooldownParam = "yb.aws.disk_resize_cooldown_hours";
    } else {
      cooldownParam = "yb.gcp.hyperdisk_resize_cooldown_hours";
      // changing provider
      defaultUniverse =
          Universe.saveDetails(
              defaultUniverse.getUniverseUUID(),
              universe -> {
                UniverseDefinitionTaskParams.UserIntent userIntent =
                    universe.getUniverseDetails().getPrimaryCluster().userIntent;
                TestUtils.existingProviderInitializer(userIntent)
                    .setProviderUUID(gcpProvider.getUuid())
                    .setProviderType(cloudType)
                    .updateDeviceInfo(
                        di ->
                            di.storageType = PublicCloudConstants.StorageType.valueOf(storageType));
              });
      createInstanceType(
          gcpProvider.getUuid(),
          defaultUniverse
              .getUniverseDetails()
              .getPrimaryCluster()
              .userIntent
              .getBaseInstanceType(gcpProvider.getUuid()));
      createInstanceType(gcpProvider.getUuid(), NEW_INSTANCE_TYPE);
    }
    UniverseDefinitionTaskParams.Cluster primaryCluster =
        defaultUniverse.getUniverseDetails().getPrimaryCluster();
    UniverseDefinitionTaskParams.UserIntent targetIntent = primaryCluster.userIntent.clone();

    TestUtils.existingProviderInitializer(targetIntent)
        .updateDeviceInfo(
            di -> {
              di.volumeSize += 1;
            });

    UniverseDefinitionTaskParams.UserIntent targetIntentJustType =
        primaryCluster.userIntent.clone();
    TestUtils.existingProviderInitializer(targetIntentJustType).setInstanceType(NEW_INSTANCE_TYPE);
    UUID primaryUUID = primaryCluster.uuid;
    assertTrue(
        ResizeNodeParams.checkResizeIsPossible(
            primaryUUID,
            primaryCluster.userIntent,
            targetIntent,
            defaultUniverse,
            mockBaseTaskDependencies.getConfGetter()));
    factory.globalRuntimeConf().setValue(cooldownParam, "3");
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            univ -> {
              Date date = DateUtils.addHours(new Date(), -2);
              univ.getNodes().forEach(node -> node.lastVolumeUpdateTime = date);
            });
    assertFalse(
        ResizeNodeParams.checkResizeIsPossible(
            primaryUUID,
            primaryCluster.userIntent,
            targetIntent,
            defaultUniverse,
            mockBaseTaskDependencies.getConfGetter()));
    // Just changing instance type is available, even within cooldown window.
    assertTrue(
        ResizeNodeParams.checkResizeIsPossible(
            primaryUUID,
            primaryCluster.userIntent,
            targetIntentJustType,
            defaultUniverse,
            mockBaseTaskDependencies.getConfGetter()));
    // Changing window size.
    factory.globalRuntimeConf().setValue(cooldownParam, "1");
    assertTrue(
        ResizeNodeParams.checkResizeIsPossible(
            primaryUUID,
            primaryCluster.userIntent,
            targetIntent,
            defaultUniverse,
            mockBaseTaskDependencies.getConfGetter()));
  }

  /*
   Instance type codes:
   m -> m3.medium
   i -> i3.instance (ephemeral)
   c -> c4.medium
   Device codes:
   10 -> device with volume 10
   20 -> device with volume 20
   10s -> scratch device with volume 10
  */
  @Parameters({
    "aws, m10, m10, c10, c10, true",
    "aws, i10, m10, i10, c10, true", // tserver is ephemeral but not touched.
    "aws, i10, m10, i10, m20, true", // tserver is ephemeral but not touched(2).
    "aws, i10, m10, c10, c10, false", // tserver is ephemeral.
    "aws, i10, m10, i20, c10, false", // tserver is ephemeral(2).
    "aws, m10, i10, c10, i10, true", // master is ephemeral but not touched.
    "aws, m10, i10, m20, i10, true", // master is ephemeral but not touched(2).
    "aws, m10, i10, c10, c10, false", // master is ephemeral.
    "aws, m10, i10, m10, i20, false", // master is ephemeral(2).
    "gcp, m10, c20s, m10, c10s, false", // master has ephemeral storage type.
    "gcp, m10, c20s, m10, m20s, false", // master has ephemeral storage type.
    "aws, m10, c20, m10, c10, false", // decrease volume for master
    "aws, m10, m10, m10, m10, false" // nothing changed
  })
  @Test
  public void testResizeForDedicated(
      String cloudTypeStr,
      String tserverConf,
      String masterConf,
      String targetTserverConf,
      String targetMasterConf,
      boolean expected) {
    modifyToDedicated();
    Common.CloudType cloudType = Common.CloudType.valueOf(cloudTypeStr);
    UniverseDefinitionTaskParams.UserIntent currentIntent = createIntent(cloudType, null, null);
    TestUtils.copyMasterDeviceInfoFromDeviceInfo(currentIntent);
    currentIntent.dedicatedNodes = true;
    applyConfig(tserverConf, currentIntent, false);
    applyConfig(masterConf, currentIntent, true);
    UniverseDefinitionTaskParams.UserIntent targetIntent = createIntent(cloudType, null, null);
    TestUtils.copyMasterDeviceInfoFromDeviceInfo(targetIntent);
    targetIntent.dedicatedNodes = true;
    applyConfig(targetTserverConf, targetIntent, false);
    applyConfig(targetMasterConf, targetIntent, true);
    assertEquals(
        expected,
        ResizeNodeParams.checkResizeIsPossible(
            defaultUniverse.getUniverseDetails().getPrimaryCluster().uuid,
            currentIntent,
            targetIntent,
            defaultUniverse,
            mockBaseTaskDependencies.getConfGetter()));
  }

  @Test
  public void testResizeRejectsClearingMasterDeviceInfoForDedicated() {
    modifyToDedicated();
    UniverseDefinitionTaskParams.UserIntent currentIntent =
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.clone();
    UniverseDefinitionTaskParams.UserIntent targetIntent = currentIntent.clone();
    targetIntent.deviceInfo.volumeSize = currentIntent.deviceInfo.volumeSize + 10;
    targetIntent.masterDeviceInfo = null;

    assertFalse(
        ResizeNodeParams.checkResizeIsPossible(
            defaultUniverse.getUniverseDetails().getPrimaryCluster().uuid,
            currentIntent,
            targetIntent,
            defaultUniverse,
            mockBaseTaskDependencies.getConfGetter()));

    ResizeNodeParams taskParams = createResizeParams();
    UniverseDefinitionTaskParams.Cluster cluster =
        new UniverseDefinitionTaskParams.Cluster(
            UniverseDefinitionTaskParams.ClusterType.PRIMARY, targetIntent);
    cluster.uuid = defaultUniverse.getUniverseDetails().getPrimaryCluster().uuid;
    taskParams.clusters = Collections.singletonList(cluster);
    Exception thrown =
        assertThrows(RuntimeException.class, () -> taskParams.verifyParams(defaultUniverse, true));
    assertTrue(thrown.getMessage().contains("Cannot clear masterDeviceInfo"));
  }

  @Test
  public void testOciInstanceTypeChangeRequiresSingleDataVolume() {
    Provider oci = ociProvider();
    String currentType = "VM.Standard.E2.2";
    String targetType = "VM.Standard.E2.1";
    createInstanceType(oci.getUuid(), currentType);
    createInstanceType(oci.getUuid(), targetType);

    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            universe -> {
              UniverseDefinitionTaskParams.UserIntent userIntent =
                  universe.getUniverseDetails().getPrimaryCluster().userIntent;
              userIntent.provider = oci.getUuid().toString();
              userIntent.providerType = Common.CloudType.oci;
              userIntent.instanceType = currentType;
              userIntent.deviceInfo.numVolumes = 2;
              userIntent.deviceInfo.storageType = PublicCloudConstants.StorageType.OCI_Balanced;
              universe.getNodes().forEach(node -> node.cloudInfo.instance_type = currentType);
            });

    UUID clusterUuid = defaultUniverse.getUniverseDetails().getPrimaryCluster().uuid;
    UniverseDefinitionTaskParams.UserIntent currentIntent =
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.clone();
    UniverseDefinitionTaskParams.UserIntent targetIntent = currentIntent.clone();
    targetIntent.instanceType = targetType;

    // Smart resize remains available; the OCI volume limit is enforced in ResizeNode precheck.
    assertTrue(
        ResizeNodeParams.checkResizeIsPossible(
            clusterUuid,
            currentIntent,
            targetIntent,
            defaultUniverse,
            mockBaseTaskDependencies.getConfGetter()));

    UniverseDefinitionTaskParams.UserIntent diskOnlyIntent = currentIntent.clone();
    diskOnlyIntent.deviceInfo.volumeSize = currentIntent.deviceInfo.volumeSize + 10;
    assertTrue(
        ResizeNodeParams.checkResizeIsPossible(
            clusterUuid,
            currentIntent,
            diskOnlyIntent,
            defaultUniverse,
            mockBaseTaskDependencies.getConfGetter()));

    ResizeNodeParams taskParams = createResizeParams();
    UniverseDefinitionTaskParams.Cluster cluster =
        new UniverseDefinitionTaskParams.Cluster(
            UniverseDefinitionTaskParams.ClusterType.PRIMARY, targetIntent);
    cluster.uuid = clusterUuid;
    taskParams.clusters = Collections.singletonList(cluster);
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Failure, taskInfo.getTaskState());
    assertThat(taskInfo.getErrorMessage(), containsString("more than one data volume"));

    Universe after = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertFalse(after.getUniverseDetails().updateInProgress);
    assertTrue(after.getUniverseDetails().updateSucceeded);
    assertNull(after.getUniverseDetails().placementModificationTaskUuid);
    assertEquals(
        currentType, after.getUniverseDetails().getPrimaryCluster().userIntent.instanceType);

    factory
        .globalRuntimeConf()
        .setValue(GlobalConfKeys.ociFailFastMultiVolumeInstanceTypeChange.getKey(), "false");
    ResizeNodeParams bypassParams = createResizeParams();
    UniverseDefinitionTaskParams.Cluster bypassCluster =
        new UniverseDefinitionTaskParams.Cluster(
            UniverseDefinitionTaskParams.ClusterType.PRIMARY, targetIntent.clone());
    bypassCluster.uuid = clusterUuid;
    bypassParams.clusters = Collections.singletonList(bypassCluster);
    TaskInfo bypassTaskInfo = submitTask(bypassParams);
    assertEquals(Success, bypassTaskInfo.getTaskState());
    assertThat(bypassTaskInfo.getErrorMessage(), not(containsString("more than one data volume")));
    assertEquals(
        targetType,
        Universe.getOrBadRequest(defaultUniverse.getUniverseUUID())
            .getUniverseDetails()
            .getPrimaryCluster()
            .userIntent
            .instanceType);
  }

  private Provider ociProvider() {
    if (ociProvider == null) {
      ociProvider = ModelFactory.ociProvider(defaultCustomer);
    }
    return ociProvider;
  }

  private void applyConfig(
      String conf, UniverseDefinitionTaskParams.UserIntent intent, boolean toMaster) {
    char instType = conf.charAt(0);
    String instanceType;
    switch (instType) {
      case 'm':
        instanceType = "m3.medium";
        break;
      case 'c':
        instanceType = "c4.medium";
        break;
      case 'i':
        instanceType = "i3.medium";
        break;
      default:
        throw new IllegalArgumentException("Unknown type " + instType);
    }
    createInstanceType(Util.getSingleProviderUUID(intent), instanceType);
    if (toMaster) {
      TestUtils.existingProviderInitializer(intent).setMasterInstanceType(instanceType);
    } else {
      TestUtils.existingProviderInitializer(intent).setInstanceType(instanceType);
    }
    String diskConf = conf.substring(1);
    boolean useScratch = false;
    if (diskConf.endsWith("s")) {
      useScratch = true;
      diskConf = diskConf.substring(0, diskConf.length() - 2);
    }
    UUID providerUUID = intent.maybeGetSingleProviderUUID().get();
    PublicCloudConstants.StorageType storageType =
        chooseStorageType(intent.getAllCloudTypes().iterator().next(), useScratch);

    DeviceInfo deviceInfo =
        toMaster
            ? intent.getBaseDeviceInfo(providerUUID, MASTER)
            : intent.getBaseDeviceInfo(providerUUID);
    deviceInfo.storageType = storageType;
    deviceInfo.volumeSize = Integer.parseInt(diskConf);
  }

  private PublicCloudConstants.StorageType chooseStorageType(
      Common.CloudType cloudType, boolean useScratch) {
    return Arrays.stream(PublicCloudConstants.StorageType.values())
        .filter(type -> type.getCloudType() == cloudType)
        .filter(type -> useScratch == (type == PublicCloudConstants.StorageType.Scratch))
        .findFirst()
        .get();
  }

  @Test
  public void testResizeIsPossibleWithNestedTserverStorageSpec() {
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            universe -> {
              UniverseDefinitionTaskParams.UserIntent userIntent =
                  universe.getUniverseDetails().getPrimaryCluster().userIntent;
              userIntent.provider = gcpProvider.getUuid().toString();
              userIntent.providerType = Common.CloudType.gcp;
              userIntent.deviceInfo.storageType = PublicCloudConstants.StorageType.Persistent;
              userIntent.deviceInfo.volumeSize = 375;
              userIntent.deviceInfo.storageClass = "standard";
            });
    UniverseDefinitionTaskParams.Cluster primaryCluster =
        defaultUniverse.getUniverseDetails().getPrimaryCluster();
    createInstanceType(gcpProvider.getUuid(), primaryCluster.userIntent.instanceType);

    // v2 clients send the new volume size both at cluster level and nested under tserver. The
    // nested spec carries only volumeSize, so it must not erase the universe's storage class.
    ClusterResizeNodeSpec nodeSpec = new ClusterResizeNodeSpec();
    nodeSpec.setInstanceType(primaryCluster.userIntent.instanceType);
    nodeSpec.setStorageSpec(new ClusterResizeStorageSpec().volumeSize(380));
    PerProcessResizeNodeSpec tserverSpec = new PerProcessResizeNodeSpec();
    tserverSpec.setInstanceType(primaryCluster.userIntent.instanceType);
    tserverSpec.setStorageSpec(new ClusterResizeStorageSpec().volumeSize(380));
    nodeSpec.setTserver(tserverSpec);

    UniverseResizeNodesCluster resizeCluster = new UniverseResizeNodesCluster();
    resizeCluster.setUuid(primaryCluster.uuid);
    resizeCluster.setNodeSpec(nodeSpec);
    UniverseResizeNodes req = new UniverseResizeNodes();
    req.addClustersItem(resizeCluster);

    UniverseDefinitionTaskParams.Cluster targetCluster =
        new UniverseDefinitionTaskParams.Cluster(
            primaryCluster.clusterType, primaryCluster.userIntent.clone());
    targetCluster.setUuid(primaryCluster.uuid);
    ResizeNodeParams params = new ResizeNodeParams();
    params.clusters.add(targetCluster);
    UniverseResizeNodeParamsMapper.INSTANCE.copyToV1ResizeNodeParams(req, params);

    assertTrue(
        ResizeNodeParams.checkResizeIsPossible(
            primaryCluster.uuid,
            primaryCluster.userIntent,
            targetCluster.userIntent,
            defaultUniverse,
            mockBaseTaskDependencies.getConfGetter()));
  }

  private UniverseDefinitionTaskParams.UserIntent createIntent(
      Common.CloudType cloudType,
      String instanceTypeCode,
      PublicCloudConstants.StorageType storageType) {
    UniverseDefinitionTaskParams.UserIntent currentIntent =
        new UniverseDefinitionTaskParams.UserIntent();

    DeviceInfo deviceInfo = new DeviceInfo();
    deviceInfo.volumeSize = 100;
    deviceInfo.numVolumes = 1;
    deviceInfo.storageType = storageType;

    Provider provider;
    switch (cloudType) {
      case aws:
        provider = defaultProvider;
        break;
      case gcp:
        provider = gcpProvider;
        break;
      case azu:
        provider = azuProvider;
        break;
      case oci:
        provider = ociProvider();
        break;
      case kubernetes:
        provider = kubernetesProvider;
        break;
      default:
        throw new IllegalStateException("Unknown cloud type " + cloudType);
    }
    TestUtils.initUserIntent(
        currentIntent, provider, instanceTypeCode, deviceInfo, "default-access");
    return currentIntent;
  }

  @Test
  public void testNonRollingUpgradeFails() {
    ResizeNodeParams taskParams = createResizeParams();
    taskParams.upgradeOption = UpgradeTaskParams.UpgradeOption.NON_ROLLING_UPGRADE;
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    assertThrows(RuntimeException.class, () -> submitTask(taskParams));
    verifyNoMoreInteractions(mockNodeManager);
  }

  @Test
  public void testNonRestartUpgradeFails() {
    ResizeNodeParams taskParams = createResizeParams();
    taskParams.upgradeOption = UpgradeTaskParams.UpgradeOption.NON_RESTART_UPGRADE;
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    assertThrows(RuntimeException.class, () -> submitTask(taskParams));
    verifyNoMoreInteractions(mockNodeManager);
  }

  @Test
  public void testNoChangesFails() {
    ResizeNodeParams taskParams = createResizeParams();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    assertThrows(RuntimeException.class, () -> submitTask(taskParams));
    verifyNoMoreInteractions(mockNodeManager);
  }

  @Test
  public void testChangeOnlyGFlagsIsOk() {
    ResizeNodeParams taskParams = createResizeParams();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.clusters.get(0).userIntent.specificGFlags =
        SpecificGFlags.construct(Map.of("master-gflag", "1"), Map.of("tserver-gflag", "2"));
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());

    MockUpgrade mockUpgrade = initMockUpgrade();
    mockUpgrade
        .precheckTasks(getPrecheckTasks(true))
        // Persist for Primary although we have no changes but this will help to avoid
        // inconsistency.
        .addTasks(TaskType.PersistResizeNode)
        .upgradeRound(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE)
        .withContext(UpgradeTaskBase.RUN_BEFORE_STOPPING)
        .tserverTask(TaskType.AnsibleConfigureServers)
        .masterTask(TaskType.AnsibleConfigureServers)
        .applyRound()
        .addTasks(TaskType.UpdateAndPersistGFlags)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testNonRollingOnlyGFlagRejectedForResize() {
    ResizeNodeParams taskParams = createResizeParams();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.clusters.get(0).userIntent.specificGFlags =
        SpecificGFlags.construct(Map.of("emergency_repair_mode", "true"), Map.of());

    PlatformServiceException exception =
        assertThrows(PlatformServiceException.class, () -> submitTask(taskParams));

    assertThat(exception.getMessage(), containsString("NON_ROLLING_UPGRADE"));
  }

  @Test
  public void testChangingNumVolumesFails() {
    ResizeNodeParams taskParams = createResizeParams();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    TestUtils.updateDeviceInfo(
        taskParams.clusters.get(0).userIntent,
        TSERVER,
        di -> {
          di.volumeSize += 10;
          di.numVolumes++;
        });
    Exception thrown = assertThrows(RuntimeException.class, () -> submitTask(taskParams));
    assertThat(
        thrown.getMessage(),
        containsString("Smart resize only supports modifying volumeSize, diskIops, throughput"));
    verifyNoMoreInteractions(mockNodeManager);
  }

  @Test
  public void testChangingStorageTypeFails() {
    ResizeNodeParams taskParams = createResizeParams();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    TestUtils.updateDeviceInfo(
        taskParams.clusters.get(0).userIntent,
        TSERVER,
        di -> {
          di.volumeSize += 10;
          di.storageType = PublicCloudConstants.StorageType.GP2;
        });
    Exception thrown = assertThrows(RuntimeException.class, () -> submitTask(taskParams));
    assertThat(
        thrown.getMessage(),
        containsString("Smart resize only supports modifying volumeSize, diskIops, throughput"));
    verifyNoMoreInteractions(mockNodeManager);
  }

  @Test
  public void testChangingVolume() {
    ResizeNodeParams taskParams = createResizeParams();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    TestUtils.updateDeviceInfo(
        taskParams.getPrimaryCluster().userIntent,
        TSERVER,
        di -> {
          di.volumeSize = NEW_VOLUME_SIZE;
        });
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    assertUniverseData(true, false);
    for (TaskInfo subTask : taskInfo.getSubTasks()) {
      if (subTask.getTaskType() == TaskType.CheckNodesAreSafeToTakeDown
          || subTask.getTaskType() == TaskType.CheckForClusterServers
          || subTask.getTaskType() == TaskType.CheckUnderReplicatedTablets) {
        Assert.fail();
      }
    }

    initMockUpgrade()
        .precheckTasks(new TaskType[0])
        .addTask(TaskType.MarkRollbackUnsafe, null)
        .upgradeRound(UpgradeTaskParams.UpgradeOption.NON_RESTART_UPGRADE)
        .tserverTask(TaskType.InstanceActions, Json.newObject().put("type", "Disk_Update"))
        .applyToTservers()
        .addTask(TaskType.PersistResizeNode, null)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testChangingOnlyCgroup() {
    ResizeNodeParams taskParams = createResizeParamsForCloud();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    TestUtils.existingProviderInitializer(taskParams.getPrimaryCluster().userIntent)
        .setCGroupSize(NEW_CGROUP_SIZE);
    TaskInfo taskInfo = submitTask(taskParams);

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    subTasks.stream()
        .filter(s -> s.getTaskType() == TaskType.ChangeInstanceType)
        .forEach(
            t ->
                assertEquals(
                    String.valueOf(NEW_CGROUP_SIZE), t.getTaskParams().get("cgroupSize").asText()));
    assertEquals(Success, taskInfo.getTaskState());
    assertUniverseData(false, false, false, false);
    Universe universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    UniverseDefinitionTaskParams.UserIntent userIntent =
        universe.getUniverseDetails().getPrimaryCluster().userIntent;
    UUID providerUUID = userIntent.maybeGetSingleProviderUUID().get();
    assertEquals(NEW_CGROUP_SIZE, (int) userIntent.getCGroupSizeForProvider(providerUUID));

    MockUpgrade mockUpgrade = initMockUpgrade();
    mockUpgrade
        .precheckTasks(getPrecheckTasks(true))
        .upgradeRound(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, true)
        .withContext(instanceChangeContext(mockUpgrade))
        .tserverTask(
            TaskType.ChangeInstanceType,
            Json.newObject().put("cgroupSize", String.valueOf(NEW_CGROUP_SIZE)))
        .applyRound()
        .addTask(TaskType.PersistResizeNode, null)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testChangingBoth() {
    ResizeNodeParams taskParams = createResizeParams();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    TestUtils.existingProviderInitializer(taskParams.getPrimaryCluster().userIntent)
        .updateDeviceInfo(
            deviceInfo -> {
              deviceInfo.volumeSize = NEW_VOLUME_SIZE;
              deviceInfo.diskIops = NEW_DISK_IOPS;
              deviceInfo.throughput = NEW_DISK_THROUGHPUT;
            })
        .setInstanceType(NEW_INSTANCE_TYPE)
        .setCGroupSize(NEW_CGROUP_SIZE);

    TaskInfo taskInfo = submitTask(taskParams);
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    subTasks.stream()
        .filter(s -> s.getTaskType() == TaskType.ChangeInstanceType)
        .forEach(
            t ->
                assertEquals(
                    String.valueOf(NEW_CGROUP_SIZE), t.getTaskParams().get("cgroupSize").asText()));
    assertEquals(Success, taskInfo.getTaskState());
    assertUniverseData(true, true);
    Universe universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    UniverseDefinitionTaskParams.UserIntent userIntent =
        universe.getUniverseDetails().getPrimaryCluster().userIntent;
    UUID providerUUID = userIntent.maybeGetSingleProviderUUID().get();

    assertEquals(NEW_DISK_IOPS, (int) userIntent.getBaseDeviceInfo(providerUUID).diskIops);
    assertEquals(NEW_DISK_THROUGHPUT, (int) userIntent.getBaseDeviceInfo(providerUUID).throughput);
    assertEquals(NEW_CGROUP_SIZE, (int) userIntent.getCGroupSizeForProvider(providerUUID));

    MockUpgrade mockUpgrade = initMockUpgrade();
    mockUpgrade
        .precheckTasks(getPrecheckTasks(true))
        .upgradeRound(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, true)
        .withContext(instanceChangeContext(mockUpgrade))
        .tserverTask(
            TaskType.ChangeInstanceType,
            Json.newObject().put("cgroupSize", String.valueOf(NEW_CGROUP_SIZE)))
        .oneShotBefore(TaskType.MarkRollbackUnsafe, TaskType.InstanceActions)
        .tserverTask(TaskType.InstanceActions, Json.newObject().put("type", "Disk_Update"))
        .applyRound()
        .addTask(TaskType.PersistResizeNode, null)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testChangingOnlyThroughput() {
    ResizeNodeParams taskParams = createResizeParams();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    TestUtils.updateDeviceInfo(
        taskParams.getPrimaryCluster().userIntent,
        TSERVER,
        di -> {
          di.diskIops = DEFAULT_DISK_IOPS;
        });
    UniverseDefinitionTaskParams.UserIntent userIntent = taskParams.getPrimaryCluster().userIntent;
    TestUtils.existingProviderInitializer(userIntent)
        .setProviderType(Common.CloudType.aws)
        .updateDeviceInfo(
            di -> {
              di.storageType = PublicCloudConstants.StorageType.GP3;
              di.throughput = NEW_DISK_THROUGHPUT;
            });

    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    assertUniverseData(false, false);
    Universe universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
    DeviceInfo deviceInfo =
        userIntent.getBaseDeviceInfo(userIntent.maybeGetSingleProviderUUID().get());
    assertEquals(DEFAULT_VOLUME_SIZE, (int) deviceInfo.volumeSize);
    assertEquals(DEFAULT_DISK_IOPS, (int) deviceInfo.diskIops);
    assertEquals(NEW_DISK_THROUGHPUT, (int) deviceInfo.throughput);

    initMockUpgrade()
        .precheckTasks(getPrecheckTasks(false))
        .upgradeRound(UpgradeTaskParams.UpgradeOption.NON_RESTART_UPGRADE)
        .tserverTask(TaskType.InstanceActions, Json.newObject().put("type", "Disk_Update"))
        .applyToTservers()
        .addTask(TaskType.PersistResizeNode, null)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testChangingOnlyInstance() {
    ResizeNodeParams taskParams = createResizeParams();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    TestUtils.updateInstanceType(
        taskParams.getPrimaryCluster().userIntent,
        UniverseTaskBase.ServerType.TSERVER,
        NEW_INSTANCE_TYPE);
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    assertUniverseData(false, true);

    MockUpgrade mockUpgrade = initMockUpgrade();

    mockUpgrade
        .precheckTasks(getPrecheckTasks(true))
        .upgradeRound(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, true)
        .withContext(instanceChangeContext(mockUpgrade))
        .tserverTask(TaskType.ChangeInstanceType)
        .applyRound()
        .addTask(TaskType.PersistResizeNode, null)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testChangingInstanceWithReadonlyReplica() {
    UniverseDefinitionTaskParams.UserIntent curIntent =
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent;
    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    userIntent.numNodes = 3;
    userIntent.ybSoftwareVersion = curIntent.ybSoftwareVersion;
    userIntent.regionList = ImmutableList.of(region.getUuid());

    ProviderInitializer providerInitializer =
        TestUtils.getProviderInitializerForTests(
            userIntent, curIntent.maybeGetSingleProviderUUID().get());
    providerInitializer.setAccessCode(
        curIntent.getAccessKeyCodeForProvider(curIntent.maybeGetSingleProviderUUID().get()));
    providerInitializer.setProviderType(curIntent.getAllCloudTypes().iterator().next());
    DeviceInfo deviceInfo = new DeviceInfo();
    deviceInfo.numVolumes = 1;
    deviceInfo.volumeSize = DEFAULT_VOLUME_SIZE;
    providerInitializer.setDeviceInfo(deviceInfo);
    providerInitializer.setInstanceType(DEFAULT_INSTANCE_TYPE);

    PlacementInfo pi = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(az1.getUuid(), pi, 1, 1, false);
    PlacementInfoUtil.addPlacementZone(az2.getUuid(), pi, 1, 1, false);
    PlacementInfoUtil.addPlacementZone(az3.getUuid(), pi, 1, 1, true);

    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            ApiUtils.mockUniverseUpdaterWithReadReplica(userIntent, pi));

    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            universe ->
                universe
                    .getNodes()
                    .forEach(node -> node.cloudInfo.instance_type = DEFAULT_INSTANCE_TYPE));

    ResizeNodeParams taskParams = createResizeParams();
    taskParams.clusters =
        Collections.singletonList(defaultUniverse.getUniverseDetails().getPrimaryCluster());

    TestUtils.existingProviderInitializer(taskParams.getPrimaryCluster().userIntent)
        .updateDeviceInfo(di -> di.volumeSize = NEW_VOLUME_SIZE)
        .setInstanceType(NEW_INSTANCE_TYPE);

    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    assertUniverseData(true, true, true, false);

    MockUpgrade mockUpgrade = initMockUpgrade();
    mockUpgrade
        .precheckTasks(
            true,
            defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.numNodes,
            getPrecheckTasks(true))
        .upgradeRound(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, true)
        .withContext(instanceChangeContext(mockUpgrade))
        .tserverTask(TaskType.ChangeInstanceType)
        .oneShotBefore(TaskType.MarkRollbackUnsafe, TaskType.InstanceActions)
        .tserverTask(TaskType.InstanceActions, Json.newObject().put("type", "Disk_Update"))
        // Only primary cluster affected
        .applyToCluster(defaultUniverse.getUniverseDetails().getPrimaryCluster().uuid)
        .addTask(TaskType.PersistResizeNode, null)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testChangingInstanceWithReadonlyReplicaChanging() {
    UniverseDefinitionTaskParams.UserIntent curIntent =
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent;
    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    userIntent.numNodes = 3;
    userIntent.ybSoftwareVersion = curIntent.ybSoftwareVersion;
    userIntent.regionList = ImmutableList.of(region.getUuid());

    DeviceInfo deviceInfo = new DeviceInfo();
    deviceInfo.numVolumes = 1;
    deviceInfo.volumeSize = DEFAULT_VOLUME_SIZE;
    TestUtils.copyProviderFields(curIntent, userIntent, confGetter)
        .setInstanceType(DEFAULT_INSTANCE_TYPE)
        .setDeviceInfo(deviceInfo);

    PlacementInfo pi = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(az1.getUuid(), pi, 1, 1, false);
    PlacementInfoUtil.addPlacementZone(az2.getUuid(), pi, 1, 1, false);
    PlacementInfoUtil.addPlacementZone(az3.getUuid(), pi, 1, 1, true);

    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            ApiUtils.mockUniverseUpdaterWithReadReplica(userIntent, pi));

    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            universe ->
                universe
                    .getNodes()
                    .forEach(node -> node.cloudInfo.instance_type = DEFAULT_INSTANCE_TYPE));

    ResizeNodeParams taskParams = createResizeParams();
    List<UniverseDefinitionTaskParams.Cluster> copyPrimaryCluster =
        Collections.singletonList(defaultUniverse.getUniverseDetails().getPrimaryCluster());
    List<UniverseDefinitionTaskParams.Cluster> copyReadOnlyCluster =
        Collections.singletonList(
            defaultUniverse.getUniverseDetails().getReadOnlyClusters().get(0));
    List<UniverseDefinitionTaskParams.Cluster> copyClusterList = new ArrayList<>();
    copyClusterList.addAll(copyPrimaryCluster);
    copyClusterList.addAll(copyReadOnlyCluster);
    taskParams.clusters = copyClusterList;
    TestUtils.existingProviderInitializer(taskParams.getPrimaryCluster().userIntent)
        .updateDeviceInfo(di -> di.volumeSize = NEW_VOLUME_SIZE)
        .setInstanceType(NEW_INSTANCE_TYPE);
    TestUtils.existingProviderInitializer(taskParams.getReadOnlyClusters().get(0).userIntent)
        .updateDeviceInfo(di -> di.volumeSize = 250)
        .setInstanceType(NEW_READ_ONLY_INSTANCE_TYPE)
        .setProviderType(Common.CloudType.aws);
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    assertUniverseDataForReadReplicaClusters(
        true, true, true, true, 250, NEW_READ_ONLY_INSTANCE_TYPE);

    MockUpgrade mockUpgrade = initMockUpgrade();
    mockUpgrade
        .precheckTasks(getPrecheckTasks(true))
        .upgradeRound(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, true)
        .withContext(instanceChangeContext(mockUpgrade))
        .tserverTask(TaskType.ChangeInstanceType)
        .oneShotBefore(TaskType.MarkRollbackUnsafe, TaskType.InstanceActions)
        .tserverTask(TaskType.InstanceActions, Json.newObject().put("type", "Disk_Update"))
        // Primary cluster first
        .applyToCluster(defaultUniverse.getUniverseDetails().getPrimaryCluster().uuid)
        .addTask(TaskType.PersistResizeNode, null)
        .upgradeRound(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, true)
        .withContext(instanceChangeContext(mockUpgrade))
        .tserverTask(TaskType.ChangeInstanceType)
        .tserverTask(TaskType.InstanceActions, Json.newObject().put("type", "Disk_Update"))
        // Now read replica
        .applyToCluster(defaultUniverse.getUniverseDetails().getReadOnlyClusters().get(0).uuid)
        .addTask(TaskType.PersistResizeNode, null)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testChangingInstanceWithOnlyReadonlyReplicaChanging() {
    UniverseDefinitionTaskParams.UserIntent curIntent =
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent;
    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    userIntent.numNodes = 3;
    userIntent.ybSoftwareVersion = curIntent.ybSoftwareVersion;
    userIntent.regionList = ImmutableList.of(region.getUuid());

    DeviceInfo deviceInfo = new DeviceInfo();
    deviceInfo.numVolumes = 1;
    deviceInfo.volumeSize = DEFAULT_VOLUME_SIZE;
    TestUtils.copyProviderFields(curIntent, userIntent, confGetter)
        .setInstanceType(DEFAULT_INSTANCE_TYPE)
        .setDeviceInfo(deviceInfo);

    PlacementInfo pi = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(az1.getUuid(), pi, 1, 1, false);
    PlacementInfoUtil.addPlacementZone(az2.getUuid(), pi, 1, 1, false);
    PlacementInfoUtil.addPlacementZone(az3.getUuid(), pi, 1, 1, true);

    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            ApiUtils.mockUniverseUpdaterWithReadReplica(userIntent, pi));
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            universe ->
                universe
                    .getNodes()
                    .forEach(node -> node.cloudInfo.instance_type = DEFAULT_INSTANCE_TYPE));

    ResizeNodeParams taskParams = createResizeParams();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    TestUtils.existingProviderInitializer(taskParams.getReadOnlyClusters().get(0).userIntent)
        .updateDeviceInfo(di -> di.volumeSize = NEW_VOLUME_SIZE)
        .setInstanceType(NEW_INSTANCE_TYPE)
        .setProviderType(Common.CloudType.aws);

    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    assertUniverseData(true, true, false, true);

    MockUpgrade mockUpgrade = initMockUpgrade();
    mockUpgrade
        // Because only RR doesn't infer CheckNodesAreSafeToTakeDown.
        .precheckTasks(
            true,
            defaultUniverse.getUniverseDetails().getReadOnlyClusters().get(0).userIntent.numNodes,
            getPrecheckTasks(false, true))
        // Persist for Primary although we have no changes but this will help to avoid
        // inconsistency.
        .addTasks(TaskType.PersistResizeNode)
        .upgradeRound(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, true)
        .withContext(instanceChangeContext(mockUpgrade))
        .tserverTask(TaskType.ChangeInstanceType)
        .oneShotBefore(TaskType.MarkRollbackUnsafe, TaskType.InstanceActions)
        .tserverTask(TaskType.InstanceActions, Json.newObject().put("type", "Disk_Update"))
        // Only RR cluster
        .applyToCluster(defaultUniverse.getUniverseDetails().getReadOnlyClusters().get(0).uuid)
        .addTask(TaskType.PersistResizeNode, null)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testDedicatedNodesResizeOnlyTserver() {
    Pair<Integer, Integer> counts = modifyToDedicated();
    ResizeNodeParams taskParams = createResizeParams();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    TestUtils.existingProviderInitializer(taskParams.getPrimaryCluster().userIntent)
        .updateDeviceInfo(di -> di.volumeSize = NEW_VOLUME_SIZE)
        .setInstanceType(NEW_INSTANCE_TYPE);
    TaskInfo taskInfo = submitTask(taskParams);
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    assertEquals(Success, taskInfo.getTaskState());
    assertDedicatedIntent(
        NEW_INSTANCE_TYPE, NEW_VOLUME_SIZE, DEFAULT_INSTANCE_TYPE, DEFAULT_VOLUME_SIZE);
    int primaryNodeCount =
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.numNodes;

    MockUpgrade mockUpgrade = initMockUpgrade();
    mockUpgrade
        .precheckTasks(true, primaryNodeCount, getPrecheckTasks(true))
        .upgradeRound(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, true)
        .withContext(instanceChangeContext(mockUpgrade))
        .tserverTask(TaskType.ChangeInstanceType)
        .oneShotBefore(TaskType.MarkRollbackUnsafe, TaskType.InstanceActions)
        .tserverTask(TaskType.InstanceActions, Json.newObject().put("type", "Disk_Update"))
        .applyToTservers()
        .addTask(TaskType.PersistResizeNode, null)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testDedicatedNodesResizeBoth() {
    Pair<Integer, Integer> counts = modifyToDedicated();
    ResizeNodeParams taskParams = createResizeParams();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;

    TestUtils.existingProviderInitializer(taskParams.getPrimaryCluster().userIntent)
        .updateDeviceInfo(
            di -> {
              di.volumeSize = NEW_VOLUME_SIZE;
              di.diskIops = NEW_DISK_IOPS;
              di.throughput = NEW_DISK_THROUGHPUT;
            })
        .setInstanceType(NEW_INSTANCE_TYPE)
        .updateMasterDeviceInfo(
            di -> {
              di.volumeSize = NEW_VOLUME_SIZE * 2;
              di.diskIops = NEW_DISK_IOPS * 2;
              di.throughput = NEW_DISK_THROUGHPUT * 2;
            })
        .setMasterInstanceType(NEW_READ_ONLY_INSTANCE_TYPE);
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    assertDedicatedIntent(
        NEW_INSTANCE_TYPE, NEW_VOLUME_SIZE, NEW_READ_ONLY_INSTANCE_TYPE, NEW_VOLUME_SIZE * 2);
    Universe universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    UniverseDefinitionTaskParams.UserIntent intent =
        universe.getUniverseDetails().getPrimaryCluster().userIntent;
    UUID providerUUID = intent.maybeGetSingleProviderUUID().get();
    DeviceInfo masterDeviceInfo = intent.getBaseDeviceInfo(providerUUID, MASTER);

    assertNotNull(masterDeviceInfo);
    assertEquals(NEW_DISK_IOPS * 2, (int) (masterDeviceInfo.diskIops));
    assertEquals(NEW_DISK_THROUGHPUT * 2, (int) (masterDeviceInfo.throughput));

    MockUpgrade mockUpgrade = initMockUpgrade();
    mockUpgrade
        .precheckTasks(getPrecheckTasks(true))
        .upgradeRound(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, true)
        .withContext(instanceChangeContext(mockUpgrade))
        .tserverTask(TaskType.ChangeInstanceType)
        .oneShotBefore(TaskType.MarkRollbackUnsafe, TaskType.InstanceActions)
        .tserverTask(TaskType.InstanceActions, Json.newObject().put("type", "Disk_Update"))
        .masterTask(TaskType.ChangeInstanceType)
        .masterTask(TaskType.InstanceActions, Json.newObject().put("type", "Disk_Update"))
        .applyRound()
        .addTask(TaskType.PersistResizeNode, null)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testDedicatedResizeMasterDeviceTserverInstance() {
    Pair<Integer, Integer> counts = modifyToDedicated();
    ResizeNodeParams taskParams = createResizeParams();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    TestUtils.existingProviderInitializer(taskParams.getPrimaryCluster().userIntent)
        .updateMasterDeviceInfo(di -> di.volumeSize = NEW_VOLUME_SIZE * 2)
        .setInstanceType(NEW_INSTANCE_TYPE);
    TaskInfo taskInfo = submitTask(taskParams);
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    assertEquals(Success, taskInfo.getTaskState());
    assertDedicatedIntent(
        NEW_INSTANCE_TYPE, DEFAULT_VOLUME_SIZE, DEFAULT_INSTANCE_TYPE, NEW_VOLUME_SIZE * 2);

    MockUpgrade mockUpgrade = initMockUpgrade();
    mockUpgrade
        .precheckTasks(
            true,
            defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.numNodes,
            getPrecheckTasks(true))
        .upgradeRound(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, true)
        .withContext(instanceChangeContext(mockUpgrade))
        .tserverTask(TaskType.ChangeInstanceType)
        .applyToTservers()
        .addTask(TaskType.MarkRollbackUnsafe, null)
        .upgradeRound(UpgradeTaskParams.UpgradeOption.NON_RESTART_UPGRADE)
        .masterTask(TaskType.InstanceActions, Json.newObject().put("type", "Disk_Update"))
        .applyToMasters()
        .addTask(TaskType.PersistResizeNode, null)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testDedicatedNodesResizeOnlyMaster() {
    Pair<Integer, Integer> counts = modifyToDedicated();
    ResizeNodeParams taskParams = createResizeParams();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    TestUtils.existingProviderInitializer(taskParams.getPrimaryCluster().userIntent)
        .updateMasterDeviceInfo(di -> di.volumeSize = NEW_VOLUME_SIZE)
        .setMasterInstanceType(NEW_INSTANCE_TYPE);
    TaskInfo taskInfo = submitTask(taskParams);
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    assertEquals(Success, taskInfo.getTaskState());
    assertDedicatedIntent(
        DEFAULT_INSTANCE_TYPE, DEFAULT_VOLUME_SIZE, NEW_INSTANCE_TYPE, NEW_VOLUME_SIZE);
    int masterNodeCount = 0;
    for (NodeDetails node : defaultUniverse.getUniverseDetails().nodeDetailsSet) {
      if (node.isMaster) {
        masterNodeCount++;
      }
    }

    MockUpgrade mockUpgrade = initMockUpgrade();
    mockUpgrade
        .precheckTasks(true, masterNodeCount, getPrecheckTasks(true))
        .upgradeRound(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, true)
        .withContext(instanceChangeContext(mockUpgrade))
        .masterTask(TaskType.ChangeInstanceType)
        .oneShotBefore(TaskType.MarkRollbackUnsafe, TaskType.InstanceActions)
        .masterTask(TaskType.InstanceActions, Json.newObject().put("type", "Disk_Update"))
        .applyToMasters()
        .addTask(TaskType.PersistResizeNode, null)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testDedicatedNodesResizeOnlyMasterIops() {
    Pair<Integer, Integer> counts = modifyToDedicated();
    UniverseDefinitionTaskParams.UserIntent userIntent =
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent;
    TestUtils.existingProviderInitializer(userIntent)
        .setProviderType(Common.CloudType.aws)
        .updateDeviceInfo(
            di -> {
              di.storageType = PublicCloudConstants.StorageType.GP3;
            })
        .updateMasterDeviceInfo(
            di -> {
              di.diskIops = DEFAULT_DISK_IOPS;
              di.throughput = DEFAULT_DISK_THROUGHPUT;
            });
    ResizeNodeParams taskParams = createResizeParams();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    TestUtils.updateDeviceInfo(
        taskParams.getPrimaryCluster().userIntent, MASTER, di -> di.diskIops = NEW_DISK_IOPS);
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    assertDedicatedIntent(
        DEFAULT_INSTANCE_TYPE, DEFAULT_VOLUME_SIZE, DEFAULT_INSTANCE_TYPE, DEFAULT_VOLUME_SIZE);
    Universe universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    UniverseDefinitionTaskParams.UserIntent intent =
        universe.getUniverseDetails().getPrimaryCluster().userIntent;
    DeviceInfo masterDeviceInfo =
        intent.getBaseDeviceInfo(intent.maybeGetSingleProviderUUID().get(), MASTER);
    assertNotNull(masterDeviceInfo);
    assertEquals(NEW_DISK_IOPS, (int) (masterDeviceInfo.diskIops));
    assertEquals(DEFAULT_DISK_THROUGHPUT, (int) (masterDeviceInfo.throughput));

    initMockUpgrade()
        .precheckTasks(getPrecheckTasks(false))
        .upgradeRound(UpgradeTaskParams.UpgradeOption.NON_RESTART_UPGRADE)
        .masterTask(TaskType.InstanceActions, Json.newObject().put("type", "Disk_Update"))
        .applyToMasters()
        .addTask(TaskType.PersistResizeNode, null)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testDedicatedNodesResizeOnlyMasterDisk() {
    Pair<Integer, Integer> counts = modifyToDedicated();
    ResizeNodeParams taskParams = createResizeParams();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    TestUtils.updateDeviceInfo(
        taskParams.getPrimaryCluster().userIntent, MASTER, di -> di.volumeSize = NEW_VOLUME_SIZE);
    TaskInfo taskInfo = submitTask(taskParams);
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    assertEquals(Success, taskInfo.getTaskState());
    assertDedicatedIntent(
        DEFAULT_INSTANCE_TYPE, DEFAULT_VOLUME_SIZE, DEFAULT_INSTANCE_TYPE, NEW_VOLUME_SIZE);
    Universe universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    universe
        .getUniverseDetails()
        .getNodesInCluster(universe.getUniverseDetails().getPrimaryCluster().uuid)
        .forEach(
            node -> {
              if (node.isMaster) {
                assertNotNull(node.lastVolumeUpdateTime);
              } else {
                assertNull(node.lastVolumeUpdateTime);
              }
            });

    initMockUpgrade()
        .precheckTasks(getPrecheckTasks(false))
        .addTask(TaskType.MarkRollbackUnsafe, null)
        .upgradeRound(UpgradeTaskParams.UpgradeOption.NON_RESTART_UPGRADE)
        .masterTask(TaskType.InstanceActions, Json.newObject().put("type", "Disk_Update"))
        .applyToMasters()
        .addTasks(TaskType.PersistResizeNode)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testChangingOnlyInstanceWithGFlags() {
    ResizeNodeParams taskParams = createResizeParamsForCloud();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    TestUtils.existingProviderInitializer(taskParams.getPrimaryCluster().userIntent)
        .setInstanceType(NEW_INSTANCE_TYPE);

    taskParams.tserverGFlags = ImmutableMap.of("tserverFlag", "123");
    taskParams.masterGFlags = ImmutableMap.of("masterFlag", "123");
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    assertUniverseData(false, true);
    assertGflags(true, true, false);

    MockUpgrade mockUpgrade = initMockUpgrade();
    mockUpgrade
        .precheckTasks(getPrecheckTasks(true))
        .upgradeRound(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, true)
        .withContext(instanceChangeContext(mockUpgrade))
        .tserverTask(TaskType.ChangeInstanceType)
        .tserverTask(TaskType.AnsibleConfigureServers)
        .masterTask(TaskType.AnsibleConfigureServers)
        .applyRound()
        .addTasks(TaskType.PersistResizeNode, TaskType.UpdateAndPersistGFlags)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testChangingOnlyInstanceWithSpecificGFlags() {
    Universe.saveDetails(
        defaultUniverse.getUniverseUUID(),
        univ -> {
          UniverseDefinitionTaskParams.UserIntent primaryIntent =
              univ.getUniverseDetails().getPrimaryCluster().userIntent;
          primaryIntent.specificGFlags =
              SpecificGFlags.construct(primaryIntent.masterGFlags, primaryIntent.tserverGFlags);
        });
    ResizeNodeParams taskParams = createResizeParamsForCloud();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    TestUtils.existingProviderInitializer(taskParams.getPrimaryCluster().userIntent)
        .setInstanceType(NEW_INSTANCE_TYPE);

    taskParams.getPrimaryCluster().userIntent.specificGFlags =
        SpecificGFlags.construct(
            ImmutableMap.of("masterFlag", "123"), ImmutableMap.of("tserverFlag", "123"));
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    assertUniverseData(false, true);
    assertGflags(true, true, true);

    MockUpgrade mockUpgrade = initMockUpgrade();
    mockUpgrade
        .precheckTasks(getPrecheckTasks(true))
        .upgradeRound(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, true)
        .withContext(instanceChangeContext(mockUpgrade))
        .tserverTask(TaskType.ChangeInstanceType)
        .tserverTask(TaskType.AnsibleConfigureServers)
        .masterTask(TaskType.AnsibleConfigureServers)
        .applyRound()
        .addTasks(TaskType.PersistResizeNode, TaskType.UpdateAndPersistGFlags)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testChangingOnlyInstanceWithTserverGFlags() {
    ResizeNodeParams taskParams = createResizeParamsForCloud();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    TestUtils.existingProviderInitializer(taskParams.getPrimaryCluster().userIntent)
        .setInstanceType(NEW_INSTANCE_TYPE);
    taskParams.tserverGFlags = ImmutableMap.of("tserverFlag", "123");
    taskParams.masterGFlags = new HashMap<>();
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    assertUniverseData(false, true);
    assertGflags(false, true, false);

    MockUpgrade mockUpgrade = initMockUpgrade();
    mockUpgrade
        .precheckTasks(getPrecheckTasks(true))
        .upgradeRound(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, true)
        .withContext(instanceChangeContext(mockUpgrade))
        .tserverTask(TaskType.ChangeInstanceType)
        .tserverTask(TaskType.AnsibleConfigureServers)
        .applyRound()
        .addTasks(TaskType.PersistResizeNode, TaskType.UpdateAndPersistGFlags)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testChangingOnlyInstanceWithTserverSpecificGFlags() {
    Universe.saveDetails(
        defaultUniverse.getUniverseUUID(),
        univ -> {
          UniverseDefinitionTaskParams.UserIntent primaryIntent =
              univ.getUniverseDetails().getPrimaryCluster().userIntent;
          primaryIntent.specificGFlags =
              SpecificGFlags.construct(primaryIntent.masterGFlags, primaryIntent.tserverGFlags);
        });
    ResizeNodeParams taskParams = createResizeParamsForCloud();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    TestUtils.existingProviderInitializer(taskParams.getPrimaryCluster().userIntent)
        .setInstanceType(NEW_INSTANCE_TYPE);
    taskParams.getPrimaryCluster().userIntent.specificGFlags =
        SpecificGFlags.construct(
            taskParams.getPrimaryCluster().userIntent.masterGFlags,
            ImmutableMap.of("tserverFlag", "123"));
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    assertUniverseData(false, true);
    assertGflags(false, true, true);

    MockUpgrade mockUpgrade = initMockUpgrade();
    mockUpgrade
        .precheckTasks(getPrecheckTasks(true))
        .upgradeRound(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, true)
        .withContext(instanceChangeContext(mockUpgrade))
        .tserverTask(TaskType.ChangeInstanceType)
        .tserverTask(TaskType.AnsibleConfigureServers)
        .applyRound()
        .addTasks(TaskType.PersistResizeNode, TaskType.UpdateAndPersistGFlags)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testChangingOnlyInstanceWithMasterGFlags() {
    ResizeNodeParams taskParams = createResizeParamsForCloud();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    TestUtils.existingProviderInitializer(taskParams.getPrimaryCluster().userIntent)
        .setInstanceType(NEW_INSTANCE_TYPE);
    taskParams.masterGFlags = ImmutableMap.of("masterFlag", "123");
    taskParams.tserverGFlags = new HashMap<>();
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    assertUniverseData(false, true);
    assertGflags(true, false, false);

    MockUpgrade mockUpgrade = initMockUpgrade();
    mockUpgrade
        .precheckTasks(getPrecheckTasks(true))
        .upgradeRound(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, true)
        .withContext(instanceChangeContext(mockUpgrade))
        .tserverTask(TaskType.ChangeInstanceType)
        .masterTask(TaskType.AnsibleConfigureServers)
        .applyRound()
        .addTasks(TaskType.PersistResizeNode, TaskType.UpdateAndPersistGFlags)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testChangingOnlyInstanceWithMasterSpecificGFlags() {
    Universe.saveDetails(
        defaultUniverse.getUniverseUUID(),
        univ -> {
          UniverseDefinitionTaskParams.UserIntent primaryIntent =
              univ.getUniverseDetails().getPrimaryCluster().userIntent;
          primaryIntent.specificGFlags =
              SpecificGFlags.construct(primaryIntent.masterGFlags, primaryIntent.tserverGFlags);
        });
    ResizeNodeParams taskParams = createResizeParamsForCloud();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    TestUtils.existingProviderInitializer(taskParams.getPrimaryCluster().userIntent)
        .setInstanceType(NEW_INSTANCE_TYPE);
    taskParams.getPrimaryCluster().userIntent.specificGFlags =
        SpecificGFlags.construct(
            ImmutableMap.of("masterFlag", "123"),
            taskParams.getPrimaryCluster().userIntent.tserverGFlags);
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    assertUniverseData(false, true);
    assertGflags(true, false, true);

    MockUpgrade mockUpgrade = initMockUpgrade();
    mockUpgrade
        .precheckTasks(getPrecheckTasks(true))
        .upgradeRound(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, true)
        .withContext(instanceChangeContext(mockUpgrade))
        .tserverTask(TaskType.ChangeInstanceType)
        .masterTask(TaskType.AnsibleConfigureServers)
        .applyRound()
        .addTasks(TaskType.PersistResizeNode, TaskType.UpdateAndPersistGFlags)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testRemountDrives() {
    AtomicReference<String> nodeName = new AtomicReference<>();
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            univ -> {
              NodeDetails node = univ.getUniverseDetails().nodeDetailsSet.iterator().next();
              node.disksAreMountedByUUID = false;
              nodeName.set(node.getNodeName());
            });
    ResizeNodeParams taskParams = createResizeParams();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    TestUtils.existingProviderInitializer(taskParams.getPrimaryCluster().userIntent)
        .updateDeviceInfo(di -> di.volumeSize = NEW_VOLUME_SIZE)
        .setInstanceType(NEW_INSTANCE_TYPE);
    TaskInfo taskInfo = submitTask(taskParams);
    List<TaskInfo> subTasks = new ArrayList<>(taskInfo.getSubTasks());
    List<TaskInfo> updateMounts =
        subTasks.stream()
            .filter(t -> t.getTaskType() == TaskType.UpdateMountedDisks)
            .collect(Collectors.toList());

    assertEquals(1, updateMounts.size());
    assertEquals(nodeName.get(), updateMounts.get(0).getTaskParams().get("nodeName").textValue());
    assertEquals(5, updateMounts.get(0).getPosition().intValue());
    assertEquals(Success, taskInfo.getTaskState());
    assertUniverseData(true, true);

    MockUpgrade mockUpgrade = initMockUpgrade();
    mockUpgrade
        .precheckTasks(
            true,
            defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.numNodes,
            getPrecheckTasks(true))
        .addTask(TaskType.UpdateMountedDisks, Json.newObject().put("nodeName", nodeName.get()))
        .upgradeRound(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, true)
        .withContext(instanceChangeContext(mockUpgrade))
        .tserverTask(TaskType.ChangeInstanceType)
        .oneShotBefore(TaskType.MarkRollbackUnsafe, TaskType.InstanceActions)
        .tserverTask(TaskType.InstanceActions, Json.newObject().put("type", "Disk_Update"))
        .applyRound()
        .addTask(TaskType.PersistResizeNode, null)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testChangeInstanceForAZ() {
    ResizeNodeParams taskParams = createResizeParamsForCloud();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    UniverseDefinitionTaskParams.UserIntentOverrides userIntentOverrides =
        new UniverseDefinitionTaskParams.UserIntentOverrides();
    UniverseDefinitionTaskParams.AZOverrides azOverrides =
        new UniverseDefinitionTaskParams.AZOverrides();
    azOverrides.setInstanceType(NEW_INSTANCE_TYPE);
    userIntentOverrides.setAzOverrides(Collections.singletonMap(az2.getUuid(), azOverrides));
    taskParams.getPrimaryCluster().userIntent.setUserIntentOverrides(userIntentOverrides);
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());

    String azNodeName =
        defaultUniverse.getUniverseDetails().nodeDetailsSet.stream()
            .filter(n -> n.getAzUuid().equals(az2.getUuid()))
            .map(n -> n.getNodeName())
            .findFirst()
            .get();

    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    int nodeChangeCount = 0;
    for (NodeDetails nodeDetails : defaultUniverse.getUniverseDetails().nodeDetailsSet) {
      if (nodeDetails.getAzUuid().equals(az2.getUuid())) {
        assertEquals(azOverrides.getInstanceType(), nodeDetails.cloudInfo.instance_type);
        nodeChangeCount++;
      } else {
        assertEquals(DEFAULT_INSTANCE_TYPE, nodeDetails.cloudInfo.instance_type);
      }
    }

    MockUpgrade mockUpgrade = initMockUpgrade();
    mockUpgrade
        .precheckTasks(true, nodeChangeCount, getPrecheckTasks(true))
        .upgradeRound(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, true)
        .withContext(instanceChangeContext(mockUpgrade))
        .tserverTask(TaskType.ChangeInstanceType)
        // Only one node affected
        .applyToNodes(Collections.emptySet(), Collections.singleton(azNodeName))
        .addTask(TaskType.PersistResizeNode, null)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testChangeOnlyThroughputForAZ() {
    ResizeNodeParams taskParams = createResizeParamsForCloud();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    UniverseDefinitionTaskParams.UserIntentOverrides userIntentOverrides =
        new UniverseDefinitionTaskParams.UserIntentOverrides();
    UniverseDefinitionTaskParams.AZOverrides azOverrides =
        new UniverseDefinitionTaskParams.AZOverrides();
    azOverrides.setDeviceInfo(new DeviceInfo());
    azOverrides.getDeviceInfo().throughput = NEW_DISK_THROUGHPUT;
    userIntentOverrides.setAzOverrides(Collections.singletonMap(az2.getUuid(), azOverrides));
    taskParams.getPrimaryCluster().userIntent.setUserIntentOverrides(userIntentOverrides);
    TaskInfo taskInfo = submitTask(taskParams);
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    assertEquals(Success, taskInfo.getTaskState());
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));

    String azNodeName =
        defaultUniverse.getUniverseDetails().nodeDetailsSet.stream()
            .filter(n -> n.getAzUuid().equals(az2.getUuid()))
            .map(n -> n.getNodeName())
            .findFirst()
            .get();

    TaskInfo deviceTask = subTasksByPosition.get(3).get(0);
    JsonNode params = deviceTask.getTaskParams();
    assertEquals(azNodeName, params.get("nodeName").asText());
    JsonNode deviceParams = params.get("deviceInfo");
    DeviceInfo deviceInfo =
        defaultUniverse
            .getUniverseDetails()
            .getPrimaryCluster()
            .userIntent
            .getBaseDeviceInfo(defaultProvider.getUuid());
    deviceInfo.throughput = NEW_DISK_THROUGHPUT;
    assertEquals(Json.toJson(deviceInfo), deviceParams);
    Universe.getOrBadRequest(defaultUniverse.getUniverseUUID())
        .getUniverseDetails()
        .nodeDetailsSet
        .forEach(
            node -> {
              if (node.getAzUuid().equals(az2.getUuid())) {
                assertNotNull(node.lastVolumeUpdateTime);
              } else {
                assertNull(node.lastVolumeUpdateTime);
              }
            });

    initMockUpgrade()
        .precheckTasks(getPrecheckTasks(false))
        .upgradeRound(UpgradeTaskParams.UpgradeOption.NON_RESTART_UPGRADE)
        .tserverTask(TaskType.InstanceActions, Json.newObject().put("type", "Disk_Update"))
        .applyToNodes(Collections.emptySet(), Collections.singleton(azNodeName))
        .addTask(TaskType.PersistResizeNode, null)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testResetInstanceForAZ() {
    AtomicReference<String> nodeName = new AtomicReference<>();
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            univ -> {
              UniverseDefinitionTaskParams.UserIntentOverrides userIntentOverrides =
                  new UniverseDefinitionTaskParams.UserIntentOverrides();
              UniverseDefinitionTaskParams.AZOverrides azOverrides =
                  new UniverseDefinitionTaskParams.AZOverrides();
              azOverrides.setInstanceType(NEW_INSTANCE_TYPE);
              userIntentOverrides.setAzOverrides(
                  Collections.singletonMap(az2.getUuid(), azOverrides));
              UniverseDefinitionTaskParams.Cluster primaryCluster =
                  univ.getUniverseDetails().getPrimaryCluster();
              primaryCluster.userIntent.setUserIntentOverrides(userIntentOverrides);
              univ.getNodesInCluster(primaryCluster.uuid).stream()
                  .filter(n -> n.getAzUuid().equals(az2.getUuid()))
                  .peek(n -> nodeName.set(n.getNodeName()))
                  .forEach(n -> n.cloudInfo.instance_type = NEW_INSTANCE_TYPE);
            });

    ResizeNodeParams taskParams = createResizeParamsForCloud();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.getPrimaryCluster().userIntent.setUserIntentOverrides(null);
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());

    MockUpgrade mockUpgrade = initMockUpgrade();
    mockUpgrade
        .precheckTasks(true, 1, getPrecheckTasks(true))
        .upgradeRound(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, true)
        .withContext(instanceChangeContext(mockUpgrade))
        .tserverTask(TaskType.ChangeInstanceType)
        // Only one node affected
        .applyToNodes(Collections.emptySet(), Collections.singleton(nodeName.get()))
        .addTask(TaskType.PersistResizeNode, null)
        .verifyTasks(taskInfo.getSubTasks());

    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    for (NodeDetails nodeDetails : defaultUniverse.getUniverseDetails().nodeDetailsSet) {
      assertEquals(DEFAULT_INSTANCE_TYPE, nodeDetails.cloudInfo.instance_type);
    }
  }

  @Test
  public void testResizeNodeRetries() {
    ResizeNodeParams taskParams = new ResizeNodeParams();
    taskParams.expectedUniverseVersion = -1;
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.creatingUser = defaultUser;
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    TestUtils.existingProviderInitializer(taskParams.clusters.get(0).userIntent)
        .setInstanceType(NEW_INSTANCE_TYPE);
    TestUtils.setFakeHttpContext(defaultUser);
    super.verifyTaskRetries(
        defaultCustomer,
        CustomerTask.TaskType.ResizeNode,
        CustomerTask.TargetType.Universe,
        defaultUniverse.getUniverseUUID(),
        TaskType.ResizeNode,
        taskParams,
        false);
    checkUniverseNodesStates(taskParams.getUniverseUUID());
  }

  @Test
  public void testResizeNodeRetryLastNodeState() throws InterruptedException {
    ResizeNodeParams taskParams = new ResizeNodeParams();
    taskParams.expectedUniverseVersion = -1;
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.creatingUser = defaultUser;
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    TestUtils.existingProviderInitializer(taskParams.clusters.get(0).userIntent)
        .setInstanceType(NEW_INSTANCE_TYPE);
    TestUtils.setFakeHttpContext(defaultUser);
    setPausePosition(5);
    // Need not sleep for default 3min in tests.
    taskParams.sleepAfterMasterRestartMillis = 5;
    taskParams.sleepAfterTServerRestartMillis = 5;
    UUID taskUUID = commissioner.submit(TaskType.ResizeNode, taskParams);
    CustomerTask.create(
        defaultCustomer,
        defaultUniverse.getUniverseUUID(),
        taskUUID,
        CustomerTask.TargetType.Universe,
        CustomerTask.TaskType.ResizeNode,
        "fake-name");
    TaskInfo taskInfo = TaskInfo.getOrBadRequest(taskUUID);
    CommissionerBaseTest.waitForTaskPaused(taskInfo.getUuid(), commissioner);
    taskInfo = TaskInfo.getOrBadRequest(taskInfo.getUuid());
    int i = 0;
    int lastNodeUpdatePosition = 0;
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    for (TaskInfo subTask : subTasks) {
      if (subTask.getTaskType() == TaskType.ChangeInstanceType) {
        lastNodeUpdatePosition = i;
      }
      i++;
    }
    setAbortPosition(lastNodeUpdatePosition); // Aborting while resizing the last node.
    commissioner.resumeTask(taskInfo.getUuid());
    taskInfo = waitForTask(taskInfo.getUuid());
    assertEquals(TaskInfo.State.Aborted, taskInfo.getTaskState());
    clearAbortOrPausePositions();
    CustomerTask customerTask =
        customerTaskManager.retryCustomerTask(defaultCustomer.getUuid(), taskInfo.getUuid());
    taskUUID = customerTask.getTaskUUID();
    taskInfo = waitForTask(taskUUID);
    assertEquals(Success, taskInfo.getTaskState());
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    for (NodeDetails nodeDetails : defaultUniverse.getUniverseDetails().nodeDetailsSet) {
      assertEquals(NodeDetails.NodeState.Live, nodeDetails.state);
    }
  }

  @Test
  public void testChangingInstanceRRWithCRAzu() {
    factory
        .globalRuntimeConf()
        .setValue(ProviderConfKeys.enableCapacityReservationAzure.getKey(), "true");
    String defaultInstanceType = "Standard_EC2as_v5";
    String newInstanceType = "Standard_EC4as_v5";
    createInstanceType(azuProvider.getUuid(), defaultInstanceType);
    createInstanceType(azuProvider.getUuid(), newInstanceType);
    Region region1 = Region.create(azuProvider, "region-1", "region-1", "img");
    AvailabilityZone az1 = AvailabilityZone.getOrCreate(region1, "az-1", "az 1", "subn");
    Region region2 = Region.create(azuProvider, "region-2", "region-2", "img");
    AvailabilityZone az2 = AvailabilityZone.getOrCreate(region2, "az-2", "az 2", "subn");
    AvailabilityZone az3 = AvailabilityZone.getOrCreate(region2, "az-3", "az 3", "subn");
    AvailabilityZone az4 = AvailabilityZone.getOrCreate(region2, "az-4", "az 4", "subn");

    UniverseDefinitionTaskParams.UserIntent userIntent =
        createIntent(
            Common.CloudType.azu, defaultInstanceType, PublicCloudConstants.StorageType.Persistent);
    userIntent.numNodes = 3;
    userIntent.ybSoftwareVersion = "2.21.1.1-b1";
    userIntent.universeName = "universe-test";
    userIntent.regionList = ImmutableList.of(region1.getUuid(), region2.getUuid());

    PlacementInfo pi = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(az1.getUuid(), pi, 1, 1, false);
    PlacementInfoUtil.addPlacementZone(az2.getUuid(), pi, 1, 1, false);
    PlacementInfoUtil.addPlacementZone(az3.getUuid(), pi, 1, 1, true);

    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.nodePrefix = "univConfCreate";
    taskParams.upsertPrimaryCluster(userIntent, Collections.emptyList(), pi);
    taskParams.userAZSelected = true;
    PlacementInfoUtil.updateUniverseDefinition(
        taskParams, defaultCustomer.getId(), taskParams.getPrimaryCluster().uuid, CREATE);
    taskParams.expectedUniverseVersion = -1;

    UniverseDefinitionTaskParams.UserIntent rrIntent =
        createIntent(
            Common.CloudType.azu, defaultInstanceType, PublicCloudConstants.StorageType.Persistent);
    rrIntent.numNodes = 2;
    rrIntent.ybSoftwareVersion = "2.21.1.1-b1";
    rrIntent.replicationFactor = 2;
    rrIntent.regionList = ImmutableList.of(region1.getUuid(), region2.getUuid());
    PlacementInfo piRR = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(az1.getUuid(), piRR, 1, 1, false);
    PlacementInfoUtil.addPlacementZone(az4.getUuid(), piRR, 1, 1, false);

    UUID asyncUUID = UUID.randomUUID();
    taskParams.upsertCluster(
        rrIntent,
        Collections.emptyList(),
        piRR,
        asyncUUID,
        UniverseDefinitionTaskParams.ClusterType.ASYNC);
    taskParams.userAZSelected = true;

    PlacementInfoUtil.updateUniverseDefinition(
        taskParams, defaultCustomer.getId(), asyncUUID, CREATE);

    assertEquals(5, taskParams.nodeDetailsSet.size());
    AtomicInteger i = new AtomicInteger();
    Map<String, String> nodesByAZ = new HashMap<>();
    taskParams
        .getNodesInCluster(taskParams.getPrimaryCluster().uuid)
        .forEach(
            n -> {
              n.state = NodeDetails.NodeState.Live;
              n.cloudInfo.private_ip = "10.0.0." + i.incrementAndGet();
              n.isMaster = true;
              n.nodeName = "host-n" + i.get();
              nodesByAZ.put(
                  String.valueOf(
                      DoCapacityReservation.extractZoneNumber(
                          AvailabilityZone.getOrBadRequest(n.azUuid).getCode())),
                  n.nodeName);
            });
    i.set(0);
    Map<String, String> rrNodesByAZ = new HashMap<>();
    taskParams
        .getNodesInCluster(taskParams.getReadOnlyClusters().get(0).uuid)
        .forEach(
            n -> {
              n.state = NodeDetails.NodeState.Live;
              n.cloudInfo.private_ip = "10.0.1." + i.incrementAndGet();
              n.nodeName = "host-readonly-n" + i.get();
              rrNodesByAZ.put(
                  String.valueOf(
                      DoCapacityReservation.extractZoneNumber(
                          AvailabilityZone.getOrBadRequest(n.azUuid).getCode())),
                  n.nodeName);
            });
    defaultUniverse = Universe.create(taskParams, defaultCustomer.getId());

    ResizeNodeParams resizeNodeParams = createResizeParams();
    resizeNodeParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    resizeNodeParams.clusters.forEach(
        c -> {
          TestUtils.existingProviderInitializer(c.userIntent).setInstanceType(newInstanceType);
        });
    resizeNodeParams.nodeDetailsSet = defaultUniverse.getUniverseDetails().nodeDetailsSet;
    TaskInfo taskInfo = submitTask(resizeNodeParams);
    assertEquals(Success, taskInfo.getTaskState());
    assertUniverseData(false, true, true, true, defaultInstanceType, newInstanceType);

    MockUpgrade mockUpgrade = initMockUpgrade();
    mockUpgrade
        .precheckTasks(getPrecheckTasks(true))
        .addTasks(TaskType.DoCapacityReservation)
        .upgradeRound(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, true)
        .withContext(instanceChangeContext(mockUpgrade))
        .tserverTask(TaskType.ChangeInstanceType)
        // Primary cluster first
        .applyToCluster(defaultUniverse.getUniverseDetails().getPrimaryCluster().uuid)
        .addTask(TaskType.PersistResizeNode, null)
        .upgradeRound(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, true)
        .withContext(instanceChangeContext(mockUpgrade))
        .tserverTask(TaskType.ChangeInstanceType)
        // Now read replica
        .applyToCluster(defaultUniverse.getUniverseDetails().getReadOnlyClusters().get(0).uuid)
        .addTask(TaskType.PersistResizeNode, null)
        .addTasks(TaskType.DeleteCapacityReservation)
        .verifyTasks(taskInfo.getSubTasks());

    verifyCapacityReservationAZU(
        defaultUniverse.getUniverseUUID(),
        AzureReservationGroup.of(
            region1,
            Map.of(
                newInstanceType,
                Map.of("1", Arrays.asList(nodesByAZ.get("1"), rrNodesByAZ.get("1"))))),
        AzureReservationGroup.of(
            region2,
            Map.of(
                newInstanceType,
                Map.of(
                    "2",
                    Arrays.asList(nodesByAZ.get("2")),
                    "3",
                    Arrays.asList(nodesByAZ.get("3")),
                    "4",
                    Arrays.asList(rrNodesByAZ.get("4"))))));

    verifyNodeInteractionsCapacityReservation(
        5,
        NodeManager.NodeCommandType.Change_Instance_Type,
        params -> ((ChangeInstanceType.Params) params).capacityReservation,
        Map.of(
            DoCapacityReservation.getCapacityReservationGroupName(
                defaultUniverse.getUniverseUUID(),
                UniverseDefinitionTaskParams.ClusterType.PRIMARY.name(),
                region1.getCode()),
            Arrays.asList(nodesByAZ.get("1"), rrNodesByAZ.get("1")),
            DoCapacityReservation.getCapacityReservationGroupName(
                defaultUniverse.getUniverseUUID(),
                UniverseDefinitionTaskParams.ClusterType.PRIMARY.name(),
                region2.getCode()),
            Arrays.asList(nodesByAZ.get("2"), nodesByAZ.get("3"), rrNodesByAZ.get("4"))));
  }

  @Test
  public void testChangingInstanceRRWithCRAws() {
    factory
        .globalRuntimeConf()
        .setValue(ProviderConfKeys.enableCapacityReservationAws.getKey(), "true");
    createInstanceType(defaultProvider.getUuid(), DEFAULT_INSTANCE_TYPE);
    createInstanceType(defaultProvider.getUuid(), NEW_INSTANCE_TYPE);
    Region region1 = Region.getByCode(defaultProvider, "region-1");
    AvailabilityZone az1 = AvailabilityZone.getOrCreate(region1, "az-1", "az 1", "subn");
    Region region2 = Region.create(defaultProvider, "region-2", "region-2", "img");
    AvailabilityZone az2 = AvailabilityZone.getOrCreate(region2, "az-4", "az 4", "subn");
    AvailabilityZone az3 = AvailabilityZone.getOrCreate(region2, "az-5", "az 5", "subn");
    AvailabilityZone az4 = AvailabilityZone.getOrCreate(region2, "az-6", "az 6", "subn");

    UniverseDefinitionTaskParams.UserIntent userIntent =
        createIntent(
            Common.CloudType.aws,
            DEFAULT_INSTANCE_TYPE,
            PublicCloudConstants.StorageType.Persistent);
    userIntent.numNodes = 3;
    userIntent.ybSoftwareVersion = "2.21.1.1-b1";
    userIntent.universeName = "universe-test";
    userIntent.regionList = ImmutableList.of(region1.getUuid(), region2.getUuid());
    PlacementInfo pi = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(az1.getUuid(), pi, 1, 1, false);
    PlacementInfoUtil.addPlacementZone(az2.getUuid(), pi, 1, 1, false);
    PlacementInfoUtil.addPlacementZone(az3.getUuid(), pi, 1, 1, true);

    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.nodePrefix = "univConfCreate";
    taskParams.upsertPrimaryCluster(userIntent, Collections.emptyList(), pi);
    taskParams.userAZSelected = true;
    PlacementInfoUtil.updateUniverseDefinition(
        taskParams, defaultCustomer.getId(), taskParams.getPrimaryCluster().uuid, CREATE);
    taskParams.expectedUniverseVersion = -1;

    UniverseDefinitionTaskParams.UserIntent rrIntent =
        createIntent(
            Common.CloudType.aws,
            DEFAULT_INSTANCE_TYPE,
            PublicCloudConstants.StorageType.Persistent);
    rrIntent.numNodes = 2;
    rrIntent.ybSoftwareVersion = "2.21.1.1-b1";
    rrIntent.replicationFactor = 2;
    rrIntent.regionList = ImmutableList.of(region1.getUuid(), region2.getUuid());
    PlacementInfo piRR = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(az1.getUuid(), piRR, 1, 1, false);
    PlacementInfoUtil.addPlacementZone(az4.getUuid(), piRR, 1, 1, false);

    UUID asyncUUID = UUID.randomUUID();
    taskParams.upsertCluster(
        rrIntent,
        Collections.emptyList(),
        piRR,
        asyncUUID,
        UniverseDefinitionTaskParams.ClusterType.ASYNC);
    taskParams.userAZSelected = true;

    PlacementInfoUtil.updateUniverseDefinition(
        taskParams, defaultCustomer.getId(), asyncUUID, CREATE);

    assertEquals(5, taskParams.nodeDetailsSet.size());
    AtomicInteger i = new AtomicInteger();
    Map<String, String> nodesByAZ = new HashMap<>();
    taskParams
        .getNodesInCluster(taskParams.getPrimaryCluster().uuid)
        .forEach(
            n -> {
              n.state = NodeDetails.NodeState.Live;
              n.cloudInfo.private_ip = "10.0.0." + i.incrementAndGet();
              n.isMaster = true;
              n.nodeName = "host-n" + i.get();
              nodesByAZ.put(AvailabilityZone.getOrBadRequest(n.azUuid).getCode(), n.nodeName);
            });
    i.set(0);
    Map<String, String> rrNodesByAZ = new HashMap<>();
    taskParams
        .getNodesInCluster(taskParams.getReadOnlyClusters().get(0).uuid)
        .forEach(
            n -> {
              n.state = NodeDetails.NodeState.Live;
              n.cloudInfo.private_ip = "10.0.1." + i.incrementAndGet();
              n.nodeName = "host-readonly-n" + i.get();
              rrNodesByAZ.put(AvailabilityZone.getOrBadRequest(n.azUuid).getCode(), n.nodeName);
            });
    defaultUniverse = Universe.create(taskParams, defaultCustomer.getId());

    ResizeNodeParams resizeNodeParams = createResizeParams();
    resizeNodeParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    resizeNodeParams.clusters.forEach(
        c -> {
          TestUtils.existingProviderInitializer(c.userIntent).setInstanceType(NEW_INSTANCE_TYPE);
        });
    resizeNodeParams.nodeDetailsSet = defaultUniverse.getUniverseDetails().nodeDetailsSet;
    TaskInfo taskInfo = submitTask(resizeNodeParams);
    assertEquals(Success, taskInfo.getTaskState());
    assertUniverseData(false, true, true, true);

    MockUpgrade mockUpgrade = initMockUpgrade();
    mockUpgrade
        .precheckTasks(getPrecheckTasks(true))
        .addTasks(TaskType.DoCapacityReservation)
        .upgradeRound(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, true)
        .withContext(instanceChangeContext(mockUpgrade))
        .tserverTask(TaskType.ChangeInstanceType)
        // Primary cluster first
        .applyToCluster(defaultUniverse.getUniverseDetails().getPrimaryCluster().uuid)
        .addTask(TaskType.PersistResizeNode, null)
        .upgradeRound(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, true)
        .withContext(instanceChangeContext(mockUpgrade))
        .tserverTask(TaskType.ChangeInstanceType)
        // Now read replica
        .applyToCluster(defaultUniverse.getUniverseDetails().getReadOnlyClusters().get(0).uuid)
        .addTask(TaskType.PersistResizeNode, null)
        .addTasks(TaskType.DeleteCapacityReservation)
        .verifyTasks(taskInfo.getSubTasks());

    verifyCapacityReservationAws(
        defaultUniverse.getUniverseUUID(),
        Map.of(
            NEW_INSTANCE_TYPE,
            Map.of(
                "1",
                    new ZoneData(
                        "region-1", Arrays.asList(nodesByAZ.get("az-1"), rrNodesByAZ.get("az-1"))),
                "4", new ZoneData("region-2", Arrays.asList(nodesByAZ.get("az-4"))),
                "5", new ZoneData("region-2", Arrays.asList(nodesByAZ.get("az-5"))),
                "6", new ZoneData("region-2", Arrays.asList(rrNodesByAZ.get("az-6"))))));

    verifyNodeInteractionsCapacityReservation(
        5,
        NodeManager.NodeCommandType.Change_Instance_Type,
        params -> ((ChangeInstanceType.Params) params).capacityReservation,
        Map.of(
            DoCapacityReservation.getZoneInstanceCapacityReservationName(
                defaultUniverse.getUniverseUUID(),
                UniverseDefinitionTaskParams.ClusterType.PRIMARY.name(),
                "az-1",
                NEW_INSTANCE_TYPE),
            Arrays.asList(nodesByAZ.get("az-1"), rrNodesByAZ.get("az-1")),
            DoCapacityReservation.getZoneInstanceCapacityReservationName(
                defaultUniverse.getUniverseUUID(),
                UniverseDefinitionTaskParams.ClusterType.PRIMARY.name(),
                "az-4",
                NEW_INSTANCE_TYPE),
            Arrays.asList(nodesByAZ.get("az-4")),
            DoCapacityReservation.getZoneInstanceCapacityReservationName(
                defaultUniverse.getUniverseUUID(),
                UniverseDefinitionTaskParams.ClusterType.PRIMARY.name(),
                "az-5",
                NEW_INSTANCE_TYPE),
            Arrays.asList(nodesByAZ.get("az-5")),
            DoCapacityReservation.getZoneInstanceCapacityReservationName(
                defaultUniverse.getUniverseUUID(),
                UniverseDefinitionTaskParams.ClusterType.PRIMARY.name(),
                "az-6",
                NEW_INSTANCE_TYPE),
            Arrays.asList(rrNodesByAZ.get("az-6"))));
  }

  private void assertUniverseData(boolean increaseVolume, boolean changeInstance) {
    assertUniverseData(increaseVolume, changeInstance, true, false);
  }

  private void assertUniverseData(
      boolean increaseVolume,
      boolean changeInstance,
      boolean primaryChanged,
      boolean readonlyChanged) {
    assertUniverseData(
        increaseVolume,
        changeInstance,
        primaryChanged,
        readonlyChanged,
        DEFAULT_INSTANCE_TYPE,
        NEW_INSTANCE_TYPE);
  }

  private void assertUniverseData(
      boolean increaseVolume,
      boolean changeInstance,
      boolean primaryChanged,
      boolean readonlyChanged,
      String defaultInstanceType,
      String newInstanceType) {
    // false false means changing throughput or/and iops
    boolean lastVolumeUpdateTimeChanged = increaseVolume || (!increaseVolume && !changeInstance);
    int volumeSize = increaseVolume ? NEW_VOLUME_SIZE : DEFAULT_VOLUME_SIZE;
    String instanceType = changeInstance ? newInstanceType : defaultInstanceType;
    Universe universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    UniverseDefinitionTaskParams.Cluster primaryCluster =
        universe.getUniverseDetails().getPrimaryCluster();
    UniverseDefinitionTaskParams.UserIntent newIntent = primaryCluster.userIntent;
    if (primaryChanged) {
      UUID providerUUID = newIntent.maybeGetSingleProviderUUID().get();
      assertEquals(volumeSize, newIntent.getBaseDeviceInfo(providerUUID).volumeSize.intValue());
      assertEquals(instanceType, newIntent.getBaseInstanceType(providerUUID));
      for (NodeDetails nodeDetails : universe.getNodesInCluster(primaryCluster.uuid)) {
        assertEquals(instanceType, nodeDetails.cloudInfo.instance_type);
        if (lastVolumeUpdateTimeChanged) {
          assertNotNull(nodeDetails.lastVolumeUpdateTime);
        } else {
          assertNull(nodeDetails.lastVolumeUpdateTime);
        }
      }
    }
    if (!universe.getUniverseDetails().getReadOnlyClusters().isEmpty()) {
      UniverseDefinitionTaskParams.Cluster readonlyCluster =
          universe.getUniverseDetails().getReadOnlyClusters().get(0);
      UniverseDefinitionTaskParams.UserIntent readonlyIntent = readonlyCluster.userIntent;
      UUID providerUUID = readonlyIntent.maybeGetSingleProviderUUID().get();
      if (readonlyChanged) {
        assertEquals(
            volumeSize, readonlyIntent.getBaseDeviceInfo(providerUUID).volumeSize.intValue());
        assertEquals(instanceType, readonlyIntent.getBaseInstanceType(providerUUID));
        for (NodeDetails nodeDetails : universe.getNodesInCluster(readonlyCluster.uuid)) {
          assertEquals(instanceType, nodeDetails.cloudInfo.instance_type);
          if (lastVolumeUpdateTimeChanged) {
            assertNotNull(nodeDetails.lastVolumeUpdateTime);
          } else {
            assertNull(nodeDetails.lastVolumeUpdateTime);
          }
        }
      } else {
        assertEquals(
            DEFAULT_VOLUME_SIZE,
            readonlyIntent.getBaseDeviceInfo(providerUUID).volumeSize.intValue());
        assertEquals(defaultInstanceType, readonlyIntent.getBaseInstanceType(providerUUID));
        for (NodeDetails nodeDetails : universe.getNodesInCluster(readonlyCluster.uuid)) {
          assertEquals(defaultInstanceType, nodeDetails.cloudInfo.instance_type);
          assertNull(nodeDetails.lastVolumeUpdateTime);
        }
      }
    }
  }

  private void assertUniverseDataForReadReplicaClusters(
      boolean increaseVolume,
      boolean changeInstance,
      boolean primaryChanged,
      boolean readonlyChanged,
      Integer readReplicaVolumeSize,
      String readReplicaInstanceType) {
    int volumeSize = increaseVolume ? NEW_VOLUME_SIZE : DEFAULT_VOLUME_SIZE;
    String instanceType = changeInstance ? NEW_INSTANCE_TYPE : DEFAULT_INSTANCE_TYPE;
    Universe universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    UniverseDefinitionTaskParams.UserIntent newIntent =
        universe.getUniverseDetails().getPrimaryCluster().userIntent;
    UUID providerUUID = newIntent.maybeGetSingleProviderUUID().get();
    if (primaryChanged) {
      assertEquals(volumeSize, newIntent.getBaseDeviceInfo(providerUUID).volumeSize.intValue());
      assertEquals(instanceType, newIntent.getBaseInstanceType(providerUUID));
    }
    if (!universe.getUniverseDetails().getReadOnlyClusters().isEmpty()) {
      UniverseDefinitionTaskParams.UserIntent readonlyIntent =
          universe.getUniverseDetails().getReadOnlyClusters().get(0).userIntent;
      UUID rrProviderUUID = newIntent.maybeGetSingleProviderUUID().get();
      if (readonlyChanged) {
        assertEquals(
            readReplicaVolumeSize, readonlyIntent.getBaseDeviceInfo(rrProviderUUID).volumeSize);
        assertEquals(readReplicaInstanceType, readonlyIntent.getBaseInstanceType(rrProviderUUID));
      } else {
        assertEquals(
            DEFAULT_VOLUME_SIZE,
            readonlyIntent.getBaseDeviceInfo(rrProviderUUID).volumeSize.intValue());
        assertEquals(DEFAULT_INSTANCE_TYPE, readonlyIntent.getInstanceType(rrProviderUUID));
      }
    }
  }

  private UpgradeTaskBase.UpgradeContext instanceChangeContext(MockUpgrade mockUpgrade) {
    return UpgradeTaskBase.UpgradeContext.builder()
        .postAction(
            node -> {
              mockUpgrade.addTask(TaskType.UpdateUniverseFields, null);
            })
        .build();
  }

  @Test
  public void testLegacyGFlagsRecordedInStateTransitionTarget() throws InterruptedException {
    Map<String, String> beforeMaster = new HashMap<>(Map.of("old-master", "1"));
    Map<String, String> beforeTserver = new HashMap<>(Map.of("old-tserver", "2"));
    Map<String, String> targetMaster = ImmutableMap.of("masterFlag", "123");
    Map<String, String> targetTserver = ImmutableMap.of("tserverFlag", "123");
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            u -> {
              UniverseDefinitionTaskParams.UserIntent intent =
                  u.getUniverseDetails().getPrimaryCluster().userIntent;
              intent.specificGFlags = null;
              intent.masterGFlags = beforeMaster;
              intent.tserverGFlags = beforeTserver;
            });

    factory
        .forUniverse(defaultUniverse)
        .setValue(UniverseConfKeys.enableComprehensivePrechecks.getKey(), "false");
    ResizeNodeParams taskParams = createResizeParamsForCloud();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    TestUtils.existingProviderInitializer(taskParams.getPrimaryCluster().userIntent)
        .setInstanceType(NEW_INSTANCE_TYPE);
    taskParams.masterGFlags = targetMaster;
    taskParams.tserverGFlags = targetTserver;
    taskParams.expectedUniverseVersion = -1;
    taskParams.creatingUser = defaultUser;
    taskParams.sleepAfterMasterRestartMillis = 5;
    taskParams.sleepAfterTServerRestartMillis = 5;
    TestUtils.setFakeHttpContext(defaultUser);
    // Freeze finishes the first runSubTasks batch and captures the target; abort before
    // PersistResizeNode / UpdateAndPersistGFlags so the universe still holds before gflags.
    setPausePosition(3);
    UUID taskUUID = commissioner.submit(TaskType.ResizeNode, taskParams);
    CustomerTask.create(
        defaultCustomer,
        defaultUniverse.getUniverseUUID(),
        taskUUID,
        CustomerTask.TargetType.Universe,
        CustomerTask.TaskType.ResizeNode,
        "fake-name");
    TaskInfo taskInfo = TaskInfo.getOrBadRequest(taskUUID);
    CommissionerBaseTest.waitForTaskPaused(taskInfo.getUuid(), commissioner);
    taskInfo = TaskInfo.getOrBadRequest(taskInfo.getUuid());
    int freezePosition = -1;
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    for (int i = 0; i < subTasks.size(); i++) {
      if (subTasks.get(i).getTaskType() == TaskType.FreezeUniverse) {
        freezePosition = i;
        break;
      }
    }
    assertTrue(freezePosition >= 0);
    setAbortPosition(freezePosition + 1);
    commissioner.resumeTask(taskInfo.getUuid());
    try {
      taskInfo = waitForTask(taskInfo.getUuid());
      assertEquals(Aborted, taskInfo.getTaskState());
      Universe universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
      StateTransitionDetails details = universe.getStateTransitionDetails();
      assertNotNull(details);
      UniverseDefinitionTaskParams.UserIntent targetIntent =
          details.getTargetUniverseDetails().getPrimaryCluster().userIntent;
      assertEquals(targetMaster, targetIntent.masterGFlags);
      assertEquals(targetTserver, targetIntent.tserverGFlags);
      UniverseDefinitionTaskParams.UserIntent currentIntent =
          universe.getUniverseDetails().getPrimaryCluster().userIntent;
      assertEquals(beforeMaster, currentIntent.masterGFlags);
      assertEquals(beforeTserver, currentIntent.tserverGFlags);
    } finally {
      clearAbortOrPausePositions();
    }
  }

  @Test
  public void testMarkRollbackUnsafeAfterVolumeSizeCheckpoint() throws InterruptedException {
    ResizeNodeParams taskParams = createResizeParams();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.getPrimaryCluster().userIntent.deviceInfo.volumeSize = NEW_VOLUME_SIZE;
    taskParams.expectedUniverseVersion = -1;
    taskParams.creatingUser = defaultUser;
    taskParams.sleepAfterMasterRestartMillis = 5;
    taskParams.sleepAfterTServerRestartMillis = 5;
    TestUtils.setFakeHttpContext(defaultUser);
    // Freeze runs in the first runSubTasks batch; MarkRollbackUnsafe is created afterward.
    // Pause on the mark itself so the upgrade graph exists, then abort after it commits.
    setPausePosition(2);
    UUID taskUUID = commissioner.submit(TaskType.ResizeNode, taskParams);
    CustomerTask.create(
        defaultCustomer,
        defaultUniverse.getUniverseUUID(),
        taskUUID,
        CustomerTask.TargetType.Universe,
        CustomerTask.TaskType.ResizeNode,
        "fake-name");
    TaskInfo taskInfo = TaskInfo.getOrBadRequest(taskUUID);
    CommissionerBaseTest.waitForTaskPaused(taskInfo.getUuid(), commissioner);
    taskInfo = TaskInfo.getOrBadRequest(taskInfo.getUuid());
    int markPosition = -1;
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    for (int i = 0; i < subTasks.size(); i++) {
      if (subTasks.get(i).getTaskType() == TaskType.MarkRollbackUnsafe) {
        markPosition = i;
        break;
      }
    }
    assertTrue(markPosition >= 0);
    setAbortPosition(markPosition + 1);
    commissioner.resumeTask(taskInfo.getUuid());
    try {
      taskInfo = waitForTask(taskInfo.getUuid());
      assertEquals(Aborted, taskInfo.getTaskState());
      boolean sawMark =
          taskInfo.getSubTasks().stream()
              .anyMatch(
                  t ->
                      t.getTaskType() == TaskType.MarkRollbackUnsafe
                          && t.getTaskState() == Success);
      assertTrue(sawMark);
      Universe universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
      StateTransitionDetails details = universe.getStateTransitionDetails();
      assertNotNull(details);
      assertFalse(details.isRollbackSafe());
      assertFalse(commissioner.canTaskRollbackDetailed(taskInfo));
    } finally {
      clearAbortOrPausePositions();
    }
  }

  @Test
  public void testStillRollbackableBeforeVolumeSizeCheckpoint() throws InterruptedException {
    factory
        .forUniverse(defaultUniverse)
        .setValue(UniverseConfKeys.enableComprehensivePrechecks.getKey(), "false");
    ResizeNodeParams taskParams = createResizeParams();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.getPrimaryCluster().userIntent.deviceInfo.volumeSize = NEW_VOLUME_SIZE;
    taskParams.getPrimaryCluster().userIntent.instanceType = NEW_INSTANCE_TYPE;
    taskParams.expectedUniverseVersion = -1;
    taskParams.creatingUser = defaultUser;
    taskParams.sleepAfterMasterRestartMillis = 5;
    taskParams.sleepAfterTServerRestartMillis = 5;
    TestUtils.setFakeHttpContext(defaultUser);
    // Freeze finishes the first runSubTasks batch; MarkRollbackUnsafe is early in the rolling
    // graph. Pause there so ChangeInstanceType can complete but the checkpoint is not crossed.
    setPausePosition(3);
    UUID taskUUID = commissioner.submit(TaskType.ResizeNode, taskParams);
    CustomerTask.create(
        defaultCustomer,
        defaultUniverse.getUniverseUUID(),
        taskUUID,
        CustomerTask.TargetType.Universe,
        CustomerTask.TaskType.ResizeNode,
        "fake-name");
    TaskInfo taskInfo = TaskInfo.getOrBadRequest(taskUUID);
    CommissionerBaseTest.waitForTaskPaused(taskInfo.getUuid(), commissioner);
    taskInfo = TaskInfo.getOrBadRequest(taskInfo.getUuid());
    int firstChangeInstance = -1;
    int markPosition = -1;
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    for (int i = 0; i < subTasks.size(); i++) {
      TaskType type = subTasks.get(i).getTaskType();
      if (type == TaskType.ChangeInstanceType && firstChangeInstance < 0) {
        firstChangeInstance = i;
      }
      if (type == TaskType.MarkRollbackUnsafe) {
        markPosition = i;
      }
    }
    assertTrue(firstChangeInstance >= 0);
    assertTrue(markPosition > firstChangeInstance);
    assertTrue(
        "pause must be at or before MarkRollbackUnsafe, mark=" + markPosition, markPosition >= 3);
    setAbortPosition(markPosition);
    commissioner.resumeTask(taskInfo.getUuid());
    try {
      taskInfo = waitForTask(taskInfo.getUuid());
      assertEquals(Aborted, taskInfo.getTaskState());
      boolean sawMarkSuccess =
          taskInfo.getSubTasks().stream()
              .anyMatch(
                  t ->
                      t.getTaskType() == TaskType.MarkRollbackUnsafe
                          && t.getTaskState() == Success);
      assertFalse(sawMarkSuccess);
      boolean sawChangeInstanceSuccess =
          taskInfo.getSubTasks().stream()
              .anyMatch(
                  t ->
                      t.getTaskType() == TaskType.ChangeInstanceType
                          && t.getTaskState() == Success);
      assertTrue(sawChangeInstanceSuccess);
      Universe universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
      StateTransitionDetails details = universe.getStateTransitionDetails();
      assertNotNull(details);
      assertTrue(details.isRollbackSafe());
      // Listing hides Rollback while the flag is off (computer.isEnabled()).
      assertFalse(commissioner.canTaskRollback(taskInfo));
      assertFalse(commissioner.canTaskRollbackDetailed(taskInfo));
      factory.globalRuntimeConf().setValue("yb.task.allow_resize_node_rollback", "true");
      assertTrue(commissioner.canTaskRollback(taskInfo));
      assertTrue(commissioner.canTaskRollbackDetailed(taskInfo));
    } finally {
      clearAbortOrPausePositions();
    }
  }

  private TaskInfo submitTask(ResizeNodeParams requestParams) {
    factory.globalRuntimeConf().setValue("yb.checks.change_master_config.enabled", "false");
    return submitTask(requestParams, TaskType.ResizeNode, commissioner, -1);
  }

  private ResizeNodeParams createResizeParams() {
    ResizeNodeParams taskParams = new ResizeNodeParams();
    factory.globalRuntimeConf().setValue("yb.internal.allow_unsupported_instances", "true");
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    return taskParams;
  }

  private ResizeNodeParams createResizeParamsForCloud() {
    ResizeNodeParams taskParams = new ResizeNodeParams();
    factory.globalRuntimeConf().setValue("yb.cloud.enabled", "true");
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    return taskParams;
  }

  private void createInstanceType(UUID providerId, String type) {
    InstanceType.InstanceTypeDetails instanceTypeDetails = new InstanceType.InstanceTypeDetails();
    InstanceType.VolumeDetails volumeDetails = new InstanceType.VolumeDetails();
    volumeDetails.volumeType = InstanceType.VolumeType.SSD;
    volumeDetails.volumeSizeGB = 100;
    volumeDetails.mountPath = "/";
    instanceTypeDetails.volumeDetailsList = Collections.singletonList(volumeDetails);
    InstanceType.upsert(providerId, type, 1, 100d, instanceTypeDetails);
  }

  private MockUpgrade initMockUpgrade() {
    return initMockUpgrade(ResizeNode.class);
  }

  private void assertDedicatedIntent(
      String newInstanceType,
      int newVolumeSize,
      String newMasterInstanceType,
      int newMasterVolumeSize) {
    Universe universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    UniverseDefinitionTaskParams.UserIntent userIntent =
        universe.getUniverseDetails().getPrimaryCluster().userIntent;
    UUID providerUUID = userIntent.maybeGetSingleProviderUUID().get();
    assertEquals(newInstanceType, userIntent.getBaseInstanceType(providerUUID));
    assertEquals(newVolumeSize, (int) userIntent.getBaseDeviceInfo(providerUUID).volumeSize);
    assertEquals(newMasterInstanceType, userIntent.getBaseInstanceType(providerUUID, MASTER));
    assertEquals(
        newMasterVolumeSize, (int) userIntent.getBaseDeviceInfo(providerUUID, MASTER).volumeSize);
    universe
        .getUniverseDetails()
        .nodeDetailsSet
        .forEach(
            node -> {
              if (node.dedicatedTo == MASTER) {
                assertEquals(newMasterInstanceType, node.cloudInfo.instance_type);
              } else {
                assertEquals(newInstanceType, node.cloudInfo.instance_type);
              }
            });
  }

  private Pair<Integer, Integer> modifyToDedicated() {
    int currentNodeCount = defaultUniverse.getUniverseDetails().nodeDetailsSet.size();
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            universe -> {
              UniverseDefinitionTaskParams.UserIntent userIntent =
                  universe.getUniverseDetails().getPrimaryCluster().userIntent;
              UUID providerUUID = userIntent.maybeGetSingleProviderUUID().get();
              userIntent.dedicatedNodes = true;
              TestUtils.existingProviderInitializer(userIntent)
                  .setMasterDeviceInfo(userIntent.getBaseDeviceInfo(providerUUID, TSERVER).clone())
                  .setMasterInstanceType(userIntent.getBaseInstanceType(providerUUID, TSERVER));
              String masterLeader = universe.getMasterLeaderHostText();
              universe
                  .getUniverseDetails()
                  .nodeDetailsSet
                  .forEach(
                      node -> {
                        node.isMaster = false;
                      });
              PlacementInfoUtil.SelectMastersResult selectMastersResult =
                  PlacementInfoUtil.selectMasters(
                      masterLeader,
                      universe.getNodes(),
                      n -> true,
                      true,
                      universe.getUniverseDetails().clusters);
              AtomicInteger nodeIdx = new AtomicInteger(universe.getNodes().size());
              AtomicInteger cnt = new AtomicInteger();
              selectMastersResult.addedMasters.forEach(
                  newMaster -> {
                    newMaster.cloudInfo.private_ip = "1.1.1." + cnt.incrementAndGet();
                    universe.getUniverseDetails().nodeDetailsSet.add(newMaster);
                    newMaster.state = NodeDetails.NodeState.Live;
                    newMaster.nodeName = "host-n" + nodeIdx.incrementAndGet();
                  });
              PlacementInfoUtil.dedicateNodes(universe.getUniverseDetails().nodeDetailsSet);
            });
    int tserverNodes = 0;
    int masterNodes = 0;
    for (NodeDetails node : defaultUniverse.getUniverseDetails().nodeDetailsSet) {
      if (node.isMaster) {
        assertEquals(MASTER, node.dedicatedTo);
        assertFalse(node.isTserver);
        masterNodes++;
      } else {
        assertEquals(TSERVER, node.dedicatedTo);
        assertTrue(node.isTserver);
        tserverNodes++;
      }
    }
    assertTrue(defaultUniverse.getUniverseDetails().nodeDetailsSet.size() > currentNodeCount);
    assertEquals(
        tserverNodes + masterNodes, defaultUniverse.getUniverseDetails().nodeDetailsSet.size());
    return new Pair<>(tserverNodes, masterNodes);
  }

  private void assertGflags(
      boolean updateMasterGflags, boolean updateTserverGflags, boolean useSpecificGFlags) {
    Universe universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    UniverseDefinitionTaskParams.Cluster primaryCluster =
        universe.getUniverseDetails().getPrimaryCluster();
    UniverseDefinitionTaskParams.UserIntent newIntent = primaryCluster.userIntent;
    if (updateMasterGflags) {
      if (useSpecificGFlags) {
        assertEquals(
            newIntent.specificGFlags.getGFlags(null, MASTER),
            new HashMap<>(ImmutableMap.of("masterFlag", "123")));
      }
      assertEquals(newIntent.masterGFlags, ImmutableMap.of("masterFlag", "123"));
    }
    if (updateTserverGflags) {
      if (useSpecificGFlags) {
        assertEquals(
            newIntent.specificGFlags.getGFlags(null, TSERVER),
            new HashMap<>(ImmutableMap.of("tserverFlag", "123")));
      }
      assertEquals(newIntent.tserverGFlags, ImmutableMap.of("tserverFlag", "123"));
    }
  }

  @Test
  public void testResizeNodeIsAbortable() {
    // The task list reports abortable from the @Abortable annotation, which gates the Abort button.
    assertTrue(Commissioner.isTaskTypeAbortable(TaskType.ResizeNode));
    assertTrue(Commissioner.isTaskTypeAbortable(TaskType.RollbackResizeNode));
  }
}
