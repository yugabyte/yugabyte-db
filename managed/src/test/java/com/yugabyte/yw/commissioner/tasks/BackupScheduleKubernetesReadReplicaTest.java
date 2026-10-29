// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor.CommandType;
import com.yugabyte.yw.commissioner.tasks.upgrade.KubernetesUpgradeTaskTest;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.KubernetesUtil;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.controllers.UniverseControllerRequestBinder;
import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.backuprestore.BackupScheduleTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.PlacementInfo.PlacementAZ;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.helpers.TimeUnit;
import java.util.List;
import java.util.UUID;
import java.util.stream.Collectors;
import org.junit.Before;
import org.junit.Test;
import org.yb.CommonTypes.TableType;
import play.libs.Json;

public class BackupScheduleKubernetesReadReplicaTest extends KubernetesUpgradeTaskTest {

  private CustomerConfig storageConfig;

  @Override
  @Before
  public void setUp() {
    super.setUp();
    setupUniverseSingleAZ(true /* setMasters */, true /* mockGetLeaderMaster */);
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            u -> u.getUniverseDetails().getPrimaryCluster().userIntent.enableYSQL = true);
    useKubernetesNodeNames();
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            u -> {
              UniverseDefinitionTaskParams details = u.getUniverseDetails();
              details.getPrimaryCluster().userIntent.ybSoftwareVersion = "2.25.0.0-b1";
              details.getPrimaryCluster().userIntent.setUseYbdbInbuiltYbc(true);
              details.setYbcInstalled(true);
              details.setEnableYbc(true);
              u.setUniverseDetails(details);
            });
    storageConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "S3-config");
    try {
      when(mockClient.setFlag(any(), anyString(), anyString(), anyBoolean())).thenReturn(true);
    } catch (Exception ignored) {
    }
  }

  private TaskInfo submitCreateSchedule() throws InterruptedException {
    BackupRequestParams scheduleParams = new BackupRequestParams();
    scheduleParams.schedulingFrequency = 3600000L;
    scheduleParams.frequencyTimeUnit = TimeUnit.HOURS;
    scheduleParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    scheduleParams.storageConfigUUID = storageConfig.getConfigUUID();
    scheduleParams.enablePointInTimeRestore = true;
    scheduleParams.scheduleName = "pit-schedule";
    scheduleParams.backupType = TableType.PGSQL_TABLE_TYPE;
    scheduleParams.customerUUID = defaultCustomer.getUuid();

    BackupScheduleTaskParams params =
        UniverseControllerRequestBinder.deepCopy(
            defaultUniverse.getUniverseDetails(), BackupScheduleTaskParams.class);
    params.setUniverseUUID(defaultUniverse.getUniverseUUID());
    params.scheduleParams = scheduleParams;
    params.customerUUID = defaultCustomer.getUuid();
    UUID taskUUID = commissioner.submit(TaskType.CreateBackupScheduleKubernetes, params);
    TestUtils.setFakeHttpContext(ModelFactory.testUser(defaultCustomer));
    CustomerTask.create(
        defaultCustomer,
        defaultUniverse.getUniverseUUID(),
        taskUUID,
        CustomerTask.TargetType.Schedule,
        CustomerTask.TaskType.Create,
        defaultUniverse.getName());
    return waitForTask(taskUUID);
  }

  // PLAT-23391: the history retention pass over the read replica must target its own releases.
  @Test
  public void testPitScheduleTreatsReadReplicaAsReadReplica() throws InterruptedException {
    Cluster readReplica = addReadReplicaCluster();
    TaskInfo taskInfo = submitCreateSchedule();
    assertEquals(Success, taskInfo.getTaskState());
    assertReadReplicaCommandsTargetReadReplica(taskInfo, readReplica);
  }

  /**
   * Replaces the primary cluster's nodes with pod-named ones (yb-master-N, yb-tserver-N), as
   * POD_INFO names them, so subtasks created up front by node name still find their nodes after
   * POD_INFO.
   */
  private void useKubernetesNodeNames() {
    UserIntent userIntent = defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent;
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            ApiUtils.mockUniverseUpdaterForK8sEdit(
                userIntent, NODE_PREFIX, true /* setMasters */, false /* updateInProgress */));
  }

  /** Adds a one-node read replica cluster in the primary's zone. */
  private Cluster addReadReplicaCluster() {
    Cluster primary = defaultUniverse.getUniverseDetails().getPrimaryCluster();
    UserIntent rrIntent = primary.userIntent.clone();
    rrIntent.numNodes = 1;
    rrIntent.replicationFactor = 1;
    PlacementAZ primaryAz = primary.placementInfo.cloudList.get(0).regionList.get(0).azList.get(0);
    PlacementInfo rrPlacement = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(primaryAz.uuid, rrPlacement, 1, 1, false);
    Cluster readReplica = new Cluster(ClusterType.ASYNC, rrIntent);
    readReplica.placementInfo = rrPlacement;
    AvailabilityZone zone = AvailabilityZone.getOrBadRequest(primaryAz.uuid);
    NodeDetails node =
        KubernetesUtil.getKubernetesNodeName(
            0, zone.getCode(), ServerType.TSERVER, false, true /* isReadCluster */);
    node.nodeUuid = UUID.randomUUID();
    node.placementUuid = readReplica.uuid;
    node.azUuid = zone.getUuid();
    node.cloudInfo = new CloudSpecificInfo();
    node.cloudInfo.cloud = zone.getProviderCloudCode().name();
    node.cloudInfo.region = zone.getRegion().getCode();
    node.cloudInfo.az = zone.getCode();
    node.cloudInfo.private_ip = node.nodeName + ".svc.cluster.local";
    node.isTserver = true;
    node.isMaster = false;
    node.isYsqlServer = true;
    node.state = NodeDetails.NodeState.Live;
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            u -> {
              UniverseDefinitionTaskParams details = u.getUniverseDetails();
              details.clusters.add(readReplica);
              details.nodeDetailsSet.add(node);
              u.setUniverseDetails(details);
            });
    return readReplica;
  }

  /**
   * The read replica's POD_INFO and helm upgrade commands must target the read replica releases.
   * POD_INFO carries the read replica placement. A helm upgrade carries a per-AZ
   * placement, and in these test universes only the read replica's has no masters.
   */
  private void assertReadReplicaCommandsTargetReadReplica(
      TaskInfo taskInfo, Cluster readReplica) {
    JsonNode readReplicaPlacement = Json.toJson(readReplica.placementInfo);
    List<JsonNode> readReplicaCommands =
        taskInfo.getSubTasks().stream()
            .filter(t -> t.getTaskType() == TaskType.KubernetesCommandExecutor)
            .map(TaskInfo::getTaskParams)
            .filter(
                p ->
                    readReplicaPlacement.equals(p.get("placementInfo"))
                        || (CommandType.HELM_UPGRADE.name().equals(p.path("commandType").asText())
                            && p.path("placementInfo").findValues("replicationFactor").stream()
                                .allMatch(rf -> rf.asInt() == 0)))
            .collect(Collectors.toList());
    String commands =
        readReplicaCommands.stream()
            .map(p -> p.path("commandType").asText() + "/" + p.path("isReadOnlyCluster"))
            .collect(Collectors.toList())
            .toString();
    for (CommandType commandType : List.of(CommandType.POD_INFO, CommandType.HELM_UPGRADE)) {
      assertTrue(
          "No read replica " + commandType + " in " + commands,
          readReplicaCommands.stream()
              .anyMatch(p -> commandType.name().equals(p.path("commandType").asText())));
    }
    assertTrue(
        "Read replica commands: " + commands,
        readReplicaCommands.stream().allMatch(p -> p.path("isReadOnlyCluster").asBoolean()));
  }
}
