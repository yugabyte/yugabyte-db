// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.local;

import static com.yugabyte.yw.common.Util.YUGABYTE_DB;
import static com.yugabyte.yw.common.gflags.GFlagsUtil.POSTMASTER_CGROUP;
import static com.yugabyte.yw.forms.UniverseConfigureTaskParams.ClusterOperationType.CREATE;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.containsString;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import com.google.common.collect.Streams;
import com.google.common.net.HostAndPort;
import com.yugabyte.yw.cloud.PublicCloudConstants;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.commissioner.tasks.subtasks.CheckLeaderlessTablets;
import com.yugabyte.yw.commissioner.tasks.subtasks.CreateTableSpaces;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.RetryTaskUntilCondition;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.TableSpaceStructures;
import com.yugabyte.yw.common.TableSpaceUtil;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.common.nodeui.DumpEntitiesResponse;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.UniverseConfigureTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseResp;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.ImageBundle;
import com.yugabyte.yw.models.ImageBundleDetails;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.RuntimeConfigEntry;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import java.io.IOException;
import java.net.InetAddress;
import java.net.ServerSocket;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;
import java.util.function.Consumer;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.hamcrest.CoreMatchers;
import org.junit.Before;
import org.junit.Test;
import org.yb.client.YBClient;
import play.libs.Json;

@Slf4j
public class EditUniverseLocalTest extends LocalProviderUniverseTestBase {

  private Region region2;
  private AvailabilityZone az21;
  private AvailabilityZone az22;

  @Override
  protected Pair<Integer, Integer> getIpRange() {
    return new Pair<>(2, 30);
  }

  @Before
  public void setUpDNS() {
    provider.getDetails().getCloudInfo().local.setHostedZoneId("test");
    provider.update();
    localNodeManager.setCheckDNS(true);

    region2 = Region.create(provider, "region-2", "region-2", "default-image");
    az21 = AvailabilityZone.createOrThrow(region2, "az-21", "az-21", "subnet-1");
    az22 = AvailabilityZone.createOrThrow(region2, "az-22", "az-22", "subnet-1");
  }

  @Test
  public void testExpand() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    userIntent.setCgroupSize(100);
    Universe universe =
        createUniverse(
            userIntent,
            params -> {
              AtomicInteger i = new AtomicInteger(1);
              params
                  .getPrimaryCluster()
                  .placementInfo
                  .azStream()
                  .forEach(
                      az -> {
                        az.leaderPreference = i.get();
                        if (i.get() == 1) {
                          i.incrementAndGet();
                        }
                      });
            });
    initYSQL(universe);
    initAndStartPayload(universe);
    verifyMasterLBStatus(customer, universe, true /*enabled*/, true /*idle*/);
    Map<String, String> varz =
        getVarz(
            universe.getNodes().iterator().next(), universe, UniverseTaskBase.ServerType.TSERVER);
    assertEquals(GFlagsUtil.YSQL_CGROUP_PATH, varz.get(POSTMASTER_CGROUP));

    changeNumberOfNodesInPrimary(universe, 2);
    UUID taskID =
        universeCRUDHandler.update(
            customer,
            Universe.getOrBadRequest(universe.getUniverseUUID()),
            universe.getUniverseDetails());
    TaskInfo taskInfo = waitForTask(taskID, universe);

    verifyUniverseTaskSuccess(taskInfo);
    verifyUniverseState(Universe.getOrBadRequest(universe.getUniverseUUID()));
    verifyYSQL(universe);
    verifyPayload();
  }

  @Test
  public void testChangeIB() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    ImageBundleDetails details = new ImageBundleDetails();
    Map<String, ImageBundleDetails.BundleInfo> regionImageInfo = new HashMap<>();
    details.setRegions(regionImageInfo);
    details.setArch(PublicCloudConstants.Architecture.x86_64);
    details.setGlobalYbImage("yb_image");
    ImageBundle imageBundle = ImageBundle.create(provider, "ib-1", details, true);

    ImageBundleDetails details2 = new ImageBundleDetails();
    details2.setRegions(regionImageInfo);
    details2.setArch(PublicCloudConstants.Architecture.x86_64);
    details2.setGlobalYbImage("yb_image2");
    ImageBundle imageBundle2 = ImageBundle.create(provider, "ib-1", details2, false);

    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    userIntent.imageBundleUUID = imageBundle.getUuid();

    Universe universe = createUniverse(userIntent);
    Map<String, String> gg =
        localNodeManager.getProvisionedArgs(
            universe.getUniverseDetails().nodeDetailsSet.iterator().next().nodeName);
    assertEquals("yb_image", gg.get("--machine_image"));
    initYSQL(universe);
    verifyMasterLBStatus(customer, universe, true /*enabled*/, true /*idle*/);
    UniverseDefinitionTaskParams.Cluster cluster =
        universe.getUniverseDetails().getPrimaryCluster();
    cluster.userIntent.imageBundleUUID = imageBundle2.getUuid();
    PlacementInfoUtil.updateUniverseDefinition(
        universe.getUniverseDetails(),
        customer.getId(),
        cluster.uuid,
        UniverseConfigureTaskParams.ClusterOperationType.EDIT);
    verifyNodeModifications(universe, 3, 3);
    UUID taskID =
        universeCRUDHandler.update(
            customer,
            Universe.getOrBadRequest(universe.getUniverseUUID()),
            universe.getUniverseDetails());
    TaskInfo taskInfo = waitForTask(taskID, universe);

    verifyUniverseTaskSuccess(taskInfo);
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    gg =
        localNodeManager.getProvisionedArgs(
            universe.getUniverseDetails().nodeDetailsSet.iterator().next().nodeName);
    assertEquals("yb_image2", gg.get("--machine_image"));
    verifyUniverseState(Universe.getOrBadRequest(universe.getUniverseUUID()));
    verifyYSQL(universe);
  }

  @Test
  public void testFullMove() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    Universe universe = createUniverse(userIntent);
    Map<String, String> varz =
        getVarz(
            universe.getNodes().iterator().next(), universe, UniverseTaskBase.ServerType.TSERVER);
    assertEquals("", varz.get(POSTMASTER_CGROUP));
    initYSQL(universe);
    RuntimeConfigEntry.upsert(universe, "yb.checks.node_disk_size.target_usage_percentage", "0");
    UniverseDefinitionTaskParams.Cluster cluster =
        universe.getUniverseDetails().getPrimaryCluster();
    cluster.userIntent.instanceType = INSTANCE_TYPE_CODE_2;
    PlacementInfoUtil.updateUniverseDefinition(
        universe.getUniverseDetails(),
        customer.getId(),
        cluster.uuid,
        UniverseConfigureTaskParams.ClusterOperationType.EDIT);
    verifyNodeModifications(universe, 3, 3);
    UUID taskID =
        universeCRUDHandler.update(
            customer,
            Universe.getOrBadRequest(universe.getUniverseUUID()),
            universe.getUniverseDetails());
    TaskInfo taskInfo = waitForTask(taskID, universe);
    verifyUniverseTaskSuccess(taskInfo);
    verifyUniverseState(Universe.getOrBadRequest(universe.getUniverseUUID()));
    verifyYSQL(universe);
  }

  @Test
  public void testAZMove() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    Universe universe = createUniverse(userIntent);
    initYSQL(universe);
    RuntimeConfigEntry.upsert(universe, "yb.checks.node_disk_size.target_usage_percentage", "0");
    UniverseDefinitionTaskParams.Cluster cluster =
        universe.getUniverseDetails().getPrimaryCluster();
    moveAZ(cluster.placementInfo, az1.getUuid(), az4.getUuid());
    PlacementInfoUtil.updateUniverseDefinition(
        universe.getUniverseDetails(),
        customer.getId(),
        cluster.uuid,
        UniverseConfigureTaskParams.ClusterOperationType.EDIT);
    verifyNodeModifications(universe, 1, 1);
    UUID taskID =
        universeCRUDHandler.update(
            customer,
            Universe.getOrBadRequest(universe.getUniverseUUID()),
            universe.getUniverseDetails());
    TaskInfo taskInfo = waitForTask(taskID, universe);
    verifyUniverseTaskSuccess(taskInfo);
    verifyUniverseState(Universe.getOrBadRequest(universe.getUniverseUUID()));
    verifyYSQL(universe);
  }

  @Test
  public void testTwoAZMoves() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.specificGFlags = getGFlags("follower_unavailable_considered_failed_sec", "5");
    Universe universe = createUniverse(userIntent);
    initYSQL(universe);
    UniverseDefinitionTaskParams.Cluster cluster =
        universe.getUniverseDetails().getPrimaryCluster();
    AtomicReference<UUID> removingAz = new AtomicReference<>();
    cluster
        .placementInfo
        .azStream()
        .limit(1)
        .forEach(
            az -> {
              removingAz.set(az.uuid);
              az.uuid = az4.getUuid();
            });
    RuntimeConfigEntry.upsert(universe, "yb.checks.node_disk_size.target_usage_percentage", "0");
    PlacementInfoUtil.updateUniverseDefinition(
        universe.getUniverseDetails(),
        customer.getId(),
        cluster.uuid,
        UniverseConfigureTaskParams.ClusterOperationType.EDIT);
    verifyNodeModifications(universe, 1, 1);
    UUID taskID =
        universeCRUDHandler.update(
            customer,
            Universe.getOrBadRequest(universe.getUniverseUUID()),
            universe.getUniverseDetails());
    TaskInfo taskInfo = waitForTask(taskID, universe);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    verifyUniverseState(Universe.getOrBadRequest(universe.getUniverseUUID()));
    verifyYSQL(universe);
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    cluster = universe.getUniverseDetails().getPrimaryCluster();
    cluster
        .placementInfo
        .azStream()
        .filter(az -> az.uuid.equals(az4.getUuid()))
        .limit(1)
        .forEach(az -> az.uuid = removingAz.get()); // Going back to prev az.
    PlacementInfoUtil.updateUniverseDefinition(
        universe.getUniverseDetails(),
        customer.getId(),
        cluster.uuid,
        UniverseConfigureTaskParams.ClusterOperationType.EDIT);
    verifyNodeModifications(universe, 1, 1);
    taskID =
        universeCRUDHandler.update(
            customer,
            Universe.getOrBadRequest(universe.getUniverseUUID()),
            universe.getUniverseDetails());
    taskInfo = waitForTask(taskID, universe);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
  }

  @Test
  public void testExpandWithRR() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    Universe universe = createUniverse(userIntent);
    initYSQL(universe);
    initAndStartPayload(universe);
    doAddReadReplica(universe, getDefaultUserIntent());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    verifyYSQL(universe, true);

    changeNumberOfNodesInPrimary(universe, 2);
    UUID taskID =
        universeCRUDHandler.update(
            customer,
            Universe.getOrBadRequest(universe.getUniverseUUID()),
            universe.getUniverseDetails());
    TaskInfo taskInfo = waitForTask(taskID, universe);
    verifyUniverseTaskSuccess(taskInfo);
    verifyUniverseState(Universe.getOrBadRequest(universe.getUniverseUUID()));
    verifyYSQL(universe);
    verifyYSQL(universe, true);
    verifyPayload();
  }

  @Test
  public void testIncreaseRFInRR() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    Universe universe = createUniverse(userIntent);
    initYSQL(universe);
    initAndStartPayload(universe);
    UniverseDefinitionTaskParams.UserIntent rrIntent = getDefaultUserIntent();
    rrIntent.replicationFactor = 1;
    rrIntent.numNodes = 3;
    doAddReadReplica(universe, rrIntent);
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    verifyYSQL(universe, true);

    UniverseDefinitionTaskParams.Cluster cluster =
        universe.getUniverseDetails().getReadOnlyClusters().get(0);
    cluster.userIntent.replicationFactor++;
    PlacementInfoUtil.updateUniverseDefinition(
        universe.getUniverseDetails(),
        customer.getId(),
        cluster.uuid,
        UniverseConfigureTaskParams.ClusterOperationType.EDIT);
    assertEquals(
        0,
        universe.getUniverseDetails().nodeDetailsSet.stream()
            .filter(
                n ->
                    n.state == NodeDetails.NodeState.ToBeAdded
                        || n.state == NodeDetails.NodeState.ToBeRemoved)
            .count());

    UniverseDefinitionTaskParams taskParams = universe.getUniverseDetails();
    taskParams.currentClusterType = UniverseDefinitionTaskParams.ClusterType.ASYNC;

    UUID taskID =
        universeCRUDHandler.update(
            customer, Universe.getOrBadRequest(universe.getUniverseUUID()), taskParams);
    TaskInfo taskInfo = waitForTask(taskID, universe);
    verifyUniverseTaskSuccess(taskInfo);
    verifyUniverseState(Universe.getOrBadRequest(universe.getUniverseUUID()));
    verifyYSQL(universe);
    verifyYSQL(universe, true);
    verifyPayload();
  }

  @Test
  public void testDecreaseRFInRR() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    Universe universe = createUniverse(userIntent);
    initYSQL(universe);
    initAndStartPayload(universe);
    UniverseDefinitionTaskParams.UserIntent rrIntent = getDefaultUserIntent();
    rrIntent.replicationFactor = 3;
    rrIntent.numNodes = 3;
    doAddReadReplica(universe, rrIntent);
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    RuntimeConfigEntry.upsert(universe, "yb.checks.node_disk_size.target_usage_percentage", "0");
    verifyYSQL(universe, true);
    UniverseDefinitionTaskParams.Cluster cluster =
        universe.getUniverseDetails().getReadOnlyClusters().get(0);
    cluster.userIntent.replicationFactor--;
    PlacementInfoUtil.updateUniverseDefinition(
        universe.getUniverseDetails(),
        customer.getId(),
        cluster.uuid,
        UniverseConfigureTaskParams.ClusterOperationType.EDIT);
    UniverseDefinitionTaskParams taskParams = universe.getUniverseDetails();
    taskParams.currentClusterType = UniverseDefinitionTaskParams.ClusterType.ASYNC;

    UUID taskID =
        universeCRUDHandler.update(
            customer, Universe.getOrBadRequest(universe.getUniverseUUID()), taskParams);
    TaskInfo taskInfo = waitForTask(taskID, universe);
    verifyUniverseTaskSuccess(taskInfo);
    verifyUniverseState(Universe.getOrBadRequest(universe.getUniverseUUID()));
    verifyYSQL(universe);
    verifyYSQL(universe, true);
    verifyPayload();
  }

  @Test
  public void testAddNodesInRR() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    Universe universe = createUniverse(userIntent);
    initYSQL(universe);
    initAndStartPayload(universe);
    UniverseDefinitionTaskParams.UserIntent rrIntent = getDefaultUserIntent();
    rrIntent.replicationFactor = 1;
    rrIntent.numNodes = 1;
    doAddReadReplica(universe, rrIntent);
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    verifyYSQL(universe, true);

    UniverseDefinitionTaskParams.Cluster cluster =
        universe.getUniverseDetails().getReadOnlyClusters().get(0);
    cluster.userIntent.numNodes += 2;
    PlacementInfoUtil.updateUniverseDefinition(
        universe.getUniverseDetails(),
        customer.getId(),
        cluster.uuid,
        UniverseConfigureTaskParams.ClusterOperationType.EDIT);
    assertEquals(
        2,
        universe.getUniverseDetails().nodeDetailsSet.stream()
            .filter(n -> n.state == NodeDetails.NodeState.ToBeAdded)
            .count());

    UniverseDefinitionTaskParams taskParams = universe.getUniverseDetails();
    taskParams.currentClusterType = UniverseDefinitionTaskParams.ClusterType.ASYNC;

    UUID taskID =
        universeCRUDHandler.update(
            customer, Universe.getOrBadRequest(universe.getUniverseUUID()), taskParams);
    TaskInfo taskInfo = waitForTask(taskID, universe);
    verifyUniverseTaskSuccess(taskInfo);
    verifyUniverseState(Universe.getOrBadRequest(universe.getUniverseUUID()));
    verifyYSQL(universe);
    verifyYSQL(universe, true);
    verifyPayload();
  }

  @Test
  public void testIncreaseRFPrimary() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.numNodes = 3;
    userIntent.replicationFactor = 1;
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    Universe universe = createUniverse(userIntent);
    initYSQL(universe);
    initAndStartPayload(universe);
    RuntimeConfigEntry.upsertGlobal(GlobalConfKeys.enableRFChange.getKey(), "true");
    Thread.sleep(500);
    UniverseDefinitionTaskParams.Cluster cluster =
        universe.getUniverseDetails().getPrimaryCluster();
    cluster.userIntent.replicationFactor = 3;
    PlacementInfoUtil.updateUniverseDefinition(
        universe.getUniverseDetails(),
        customer.getId(),
        cluster.uuid,
        UniverseConfigureTaskParams.ClusterOperationType.EDIT);
    verifyNodeModifications(universe, 0, 0);
    UUID taskID =
        universeCRUDHandler.update(
            customer,
            Universe.getOrBadRequest(universe.getUniverseUUID()),
            universe.getUniverseDetails());
    TaskInfo taskInfo = waitForTask(taskID, universe);
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    cluster = universe.getUniverseDetails().getPrimaryCluster();
    cluster.userIntent.instanceType = INSTANCE_TYPE_CODE_2;
    PlacementInfoUtil.updateUniverseDefinition(
        universe.getUniverseDetails(),
        customer.getId(),
        cluster.uuid,
        UniverseConfigureTaskParams.ClusterOperationType.EDIT);
    verifyNodeModifications(universe, 3, 3);
    taskID =
        universeCRUDHandler.update(
            customer,
            Universe.getOrBadRequest(universe.getUniverseUUID()),
            universe.getUniverseDetails());
    taskInfo = waitForTask(taskID, universe);
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    verifyUniverseTaskSuccess(taskInfo);
    verifyUniverseState(universe);
    verifyYSQL(universe);
    verifyPayload();
    assertEquals(3, universe.getMasters().size());
  }

  //  @Test
  //  Right now we don't support decreasing of RF, but our code for VMs can already handle it.
  public void testDecreaseRFPrimary() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    Universe universe = createUniverse(userIntent);
    initYSQL(universe);
    UniverseDefinitionTaskParams.Cluster cluster =
        universe.getUniverseDetails().getPrimaryCluster();
    cluster.userIntent.replicationFactor = 1;
    PlacementInfoUtil.updateUniverseDefinition(
        universe.getUniverseDetails(),
        customer.getId(),
        cluster.uuid,
        UniverseConfigureTaskParams.ClusterOperationType.EDIT);
    verifyNodeModifications(universe, 2, 2);
    UUID taskID =
        universeCRUDHandler.update(
            customer,
            Universe.getOrBadRequest(universe.getUniverseUUID()),
            universe.getUniverseDetails());
    TaskInfo taskInfo = waitForTask(taskID, universe);
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    verifyUniverseTaskSuccess(taskInfo);
    verifyUniverseState(universe);
    verifyYSQL(universe);
    assertEquals(1, universe.getMasters().size());
  }

  //   @Test
  public void testUpdateCommPorts() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    Universe universe = createUniverse(userIntent);
    initYSQL(universe);
    UniverseDefinitionTaskParams taskParams = universe.getUniverseDetails();
    UniverseTaskParams.CommunicationPorts newPorts = new UniverseTaskParams.CommunicationPorts();
    newPorts.masterHttpPort = 11010;
    newPorts.masterRpcPort = 11011;
    newPorts.tserverHttpPort = 11050;
    newPorts.tserverRpcPort = 11051;
    newPorts.nodeExporterPort = 12555;
    taskParams.communicationPorts = newPorts;

    PlacementInfoUtil.updateUniverseDefinition(
        taskParams,
        customer.getId(),
        taskParams.getPrimaryCluster().uuid,
        UniverseConfigureTaskParams.ClusterOperationType.EDIT);
    verifyNodeModifications(universe, 3, 3);

    UUID taskID =
        universeCRUDHandler.update(
            customer, Universe.getOrBadRequest(universe.getUniverseUUID()), taskParams);
    TaskInfo taskInfo = waitForTask(taskID, universe);
    verifyUniverseTaskSuccess(taskInfo);
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    verifyUniverseState(universe);
    assertEquals(newPorts, universe.getUniverseDetails().communicationPorts);
    for (NodeDetails nodeDetails : universe.getNodes()) {
      Map<String, String> provisionArgs =
          localNodeManager.getProvisionedArgs(nodeDetails.getNodeName());
      assertEquals("12555", provisionArgs.get("--node_exporter_port"));
      if (nodeDetails.isMaster) {
        verifyListeningPort(nodeDetails, newPorts.masterHttpPort);
        verifyListeningPort(nodeDetails, newPorts.masterRpcPort);
      }
      if (nodeDetails.isTserver) {
        verifyListeningPort(nodeDetails, newPorts.tserverHttpPort);
        verifyListeningPort(nodeDetails, newPorts.tserverRpcPort);
      }
    }
  }

  private void verifyListeningPort(NodeDetails nodeDetails, int port) {
    InetAddress inetAddress = null;
    try {
      inetAddress = InetAddress.getByName(nodeDetails.cloudInfo.private_ip);
      ServerSocket ignored = new ServerSocket(port, 50, inetAddress);
      throw new IllegalStateException(
          String.format("Expected %s to listen %s port", nodeDetails.cloudInfo.private_ip, port));
    } catch (IOException ign) {
    }
  }

  // FAILURE TESTS

  @Test
  public void testUnknownMasterBeforeEditFAIL() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.numNodes = 5;
    userIntent.replicationFactor = 5;
    Universe universe = createUniverse(userIntent);
    initYSQL(universe);
    String masterLeaderIP = getMasterLeader(universe);

    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u -> {
              UniverseDefinitionTaskParams details = u.getUniverseDetails();
              details.nodeDetailsSet.stream()
                  .filter(n -> !n.cloudInfo.private_ip.equals(masterLeaderIP))
                  .limit(2)
                  .forEach(n -> n.isMaster = false);
              details.getPrimaryCluster().userIntent.replicationFactor =
                  3; // Pretending we have RF3
              u.setUniverseDetails(details);
            });
    log.debug("Universe {}", Json.toJson(universe.getUniverseDetails()));
    UniverseDefinitionTaskParams.Cluster cluster =
        universe.getUniverseDetails().getPrimaryCluster();
    cluster.userIntent.numNodes += 1;
    PlacementInfoUtil.updateUniverseDefinition(
        universe.getUniverseDetails(),
        customer.getId(),
        cluster.uuid,
        UniverseConfigureTaskParams.ClusterOperationType.EDIT);

    UUID taskID =
        universeCRUDHandler.update(
            customer,
            Universe.getOrBadRequest(universe.getUniverseUUID()),
            universe.getUniverseDetails());
    TaskInfo taskInfo = waitForTask(taskID, universe);
    assertEquals(TaskInfo.State.Failure, taskInfo.getTaskState());
    String error = getAllErrorsStr(taskInfo);
    assertThat(error, containsString("Unexpected MASTER: "));
  }

  @Test
  public void testUnknownTserverBeforeEditFAIL() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.numNodes = 4;
    Universe universe = createUniverse(userIntent);
    initYSQL(universe);
    NodeDetails removed = silentlyRemoveNode(universe, false, true);
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    changeNumberOfNodesInPrimary(universe, 1);

    UUID taskID =
        universeCRUDHandler.update(
            customer,
            Universe.getOrBadRequest(universe.getUniverseUUID()),
            universe.getUniverseDetails());
    TaskInfo taskInfo = waitForTask(taskID, universe);
    assertEquals(TaskInfo.State.Failure, taskInfo.getTaskState());
    String error = getAllErrorsStr(taskInfo);
    assertThat(error, containsString("Unexpected TSERVER: " + removed.cloudInfo.private_ip));
  }

  @Test
  public void testGeoPartitioningRemoveZone() throws InterruptedException {
    // Primary partition:
    // az1 -> 1, az2 -> 1, az3 -> 2
    // Secondary partition:
    // az21 -> 2
    // az22 -> 2
    Universe universe =
        createGeopartitionedUniverse(
            p ->
                p.azStream()
                    .filter(az -> az.uuid.equals(az3.getUuid()))
                    .forEach(az -> az.numNodesInAZ += 1));
    UniverseDefinitionTaskParams.Cluster cluster =
        universe.getUniverseDetails().getPrimaryCluster();
    UniverseDefinitionTaskParams.PartitionInfo partition = cluster.getPartitions().get(0);

    initYSQL(universe, "table_in_main", TableSpaceUtil.getTablespaceName(partition));
    // Target primary partition:
    // az4 -> 1, az2 -> 1, az3 -> 2

    // Tmp tablespace: az2 -> 1, az3 -> 2
    TableSpaceStructures.TableSpaceInfo tmpTablespace =
        initTablespace("tmp_tablespace", az3.getUuid(), 2, az2.getUuid(), 1);
    NodeDetails nodeInAz3 =
        universe.getUniverseDetails().nodeDetailsSet.stream()
            .filter(n -> n.azUuid.equals(az3.getUuid()))
            .findFirst()
            .get();

    ShellResponse response =
        localNodeUniverseManager.runYsqlCommand(
            nodeInAz3,
            universe,
            YUGABYTE_DB,
            CreateTableSpaces.getTablespaceCreationQuery(tmpTablespace),
            10,
            universe.getUniverseDetails().getPrimaryCluster().userIntent.isYSQLAuthEnabled(),
            false);
    assertEquals(response.getMessage(), true, response.isSuccess());

    response =
        localNodeUniverseManager.runYsqlCommand(
            nodeInAz3,
            universe,
            YUGABYTE_DB,
            "ALTER TABLE table_in_main SET TABLESPACE " + tmpTablespace.name,
            10,
            universe.getUniverseDetails().getPrimaryCluster().userIntent.isYSQLAuthEnabled(),
            false);
    assertEquals(response.getMessage(), true, response.isSuccess());

    response =
        localNodeUniverseManager.runYsqlCommand(
            nodeInAz3,
            universe,
            YUGABYTE_DB,
            "DROP TABLESPACE " + TableSpaceUtil.getTablespaceName(partition),
            10,
            universe.getUniverseDetails().getPrimaryCluster().userIntent.isYSQLAuthEnabled(),
            false);
    assertEquals(response.getMessage(), true, response.isSuccess());

    moveAZ(partition.getPlacement(), az1.getUuid(), az4.getUuid());
    PlacementInfoUtil.updateUniverseDefinition(
        universe.getUniverseDetails(),
        customer.getId(),
        cluster.uuid,
        UniverseConfigureTaskParams.ClusterOperationType.EDIT);
    verifyNodeModifications(universe, 1, 1);
    UUID taskID =
        universeCRUDHandler.update(
            customer,
            Universe.getOrBadRequest(universe.getUniverseUUID()),
            universe.getUniverseDetails());
    TaskInfo taskInfo = waitForTask(taskID);
    // This will still fail because tablets are not yet moved.
    assertEquals(TaskInfo.State.Failure, taskInfo.getTaskState());

    UUID universeUUID = universe.getUniverseUUID();
    HostAndPort hp =
        HostAndPort.fromParts(
            universe.getUniverseDetails().nodeDetailsSet.stream()
                .filter(n -> n.azUuid.equals(az1.getUuid()))
                .findFirst()
                .get()
                .cloudInfo
                .private_ip,
            universe.getUniverseDetails().communicationPorts.tserverRpcPort);
    // Waiting for table to move.
    RetryTaskUntilCondition<Boolean> condition =
        new RetryTaskUntilCondition<>(
            () -> {
              DumpEntitiesResponse dumpEntitiesResponse =
                  UniverseTaskBase.dumpDbEntities(
                      Universe.getOrBadRequest(universeUUID), null, nodeUIApiHelper);
              Set<String> tables =
                  dumpEntitiesResponse.getTablesByTserverAddresses(hp).stream()
                      .map(t -> t.getTableName())
                      .collect(Collectors.toSet());
              log.debug("Tables on {}: {}", hp.getHost(), tables);
              return !tables.contains("table_in_main");
            },
            (x) -> x);
    if (!condition.retryUntilCond(20, 300)) {
      throw new RuntimeException("Failed to wait till tablets are moved");
    }
    universe = Universe.getOrBadRequest(universeUUID);
    moveAZ(
        universe.getUniverseDetails().getPrimaryCluster().getPartitions().get(0).getPlacement(),
        az1.getUuid(),
        az4.getUuid());
    PlacementInfoUtil.updateUniverseDefinition(
        universe.getUniverseDetails(),
        customer.getId(),
        cluster.uuid,
        UniverseConfigureTaskParams.ClusterOperationType.EDIT);
    taskID =
        universeCRUDHandler.update(
            customer,
            Universe.getOrBadRequest(universe.getUniverseUUID()),
            universe.getUniverseDetails());
    taskInfo = waitForTask(taskID);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    verifyYSQL(
        universe,
        false,
        YUGABYTE_DB,
        "table_in_main",
        universe.getUniverseDetails().getPrimaryCluster().userIntent.isYSQLAuthEnabled(),
        false);
  }

  @Test
  public void testGeoPartitioningRemoveGeo() throws InterruptedException {
    Universe universe = createGeopartitionedUniverse(null);
    UniverseDefinitionTaskParams.Cluster cluster =
        universe.getUniverseDetails().getPrimaryCluster();
    initYSQL(
        universe,
        "table_in_main",
        TableSpaceUtil.getTablespaceName(cluster.getPartitions().get(0)));

    // Removing secondary partition.
    universe
        .getUniverseDetails()
        .getPrimaryCluster()
        .setPartitions(Collections.singletonList(cluster.getPartitions().get(0)));
    PlacementInfoUtil.updateUniverseDefinition(
        universe.getUniverseDetails(),
        customer.getId(),
        cluster.uuid,
        UniverseConfigureTaskParams.ClusterOperationType.EDIT);
    verifyNodeModifications(universe, 0, 3);
    UUID taskID =
        universeCRUDHandler.update(
            customer,
            Universe.getOrBadRequest(universe.getUniverseUUID()),
            universe.getUniverseDetails());
    TaskInfo taskInfo = waitForTask(taskID, universe);

    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());

    verifyYSQL(
        universe,
        false,
        YUGABYTE_DB,
        "table_in_main",
        universe.getUniverseDetails().getPrimaryCluster().userIntent.isYSQLAuthEnabled(),
        false);
  }

  @Test
  public void testGeoPartitioningRemoveGeoFAIL() throws InterruptedException {
    Universe universe = createGeopartitionedUniverse(null);
    UniverseDefinitionTaskParams.Cluster cluster =
        universe.getUniverseDetails().getPrimaryCluster();
    initYSQL(
        universe,
        "table_in_main",
        TableSpaceUtil.getTablespaceName(cluster.getPartitions().get(0)));
    UniverseDefinitionTaskParams.PartitionInfo secondaryGeo = cluster.getPartitions().get(1);
    initYSQL(universe, "table_in_secondary", TableSpaceUtil.getTablespaceName(secondaryGeo));

    // Removing secondary partition.
    universe
        .getUniverseDetails()
        .getPrimaryCluster()
        .setPartitions(Collections.singletonList(cluster.getPartitions().get(0)));
    PlacementInfoUtil.updateUniverseDefinition(
        universe.getUniverseDetails(),
        customer.getId(),
        cluster.uuid,
        UniverseConfigureTaskParams.ClusterOperationType.EDIT);
    verifyNodeModifications(universe, 0, 3);
    UUID taskID =
        universeCRUDHandler.update(
            customer,
            Universe.getOrBadRequest(universe.getUniverseUUID()),
            universe.getUniverseDetails());
    TaskInfo taskInfo = waitForTask(taskID, universe);
    // Fail because of existing table.
    assertEquals(TaskInfo.State.Failure, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    TaskInfo subTaskInfo =
        taskInfo.getSubTasks().stream()
            .filter(st -> st.getTaskType() == TaskType.TablespaceValidationOnRemove)
            .findFirst()
            .get();
    String expectedMsg = "These tables: [table_in_secondary] have tablets on removed AZs";
    assertThat(subTaskInfo.getErrorMessage(), CoreMatchers.containsString(expectedMsg));
    assertNull(universe.getUniverseDetails().placementModificationTaskUuid);
    assertTrue(universe.getUniverseDetails().updateSucceeded);
  }

  private Universe createGeopartitionedUniverse(Consumer<PlacementInfo> customizer)
      throws InterruptedException {
    UniverseDefinitionTaskParams.PartitionInfo main =
        new UniverseDefinitionTaskParams.PartitionInfo();
    main.setName("main");
    main.setDefaultPartition(true);
    main.setPlacement(toPlacement(az1, az2, az3));
    if (customizer != null) {
      customizer.accept(main.getPlacement());
    }
    PlacementInfoUtil.checkAndSetPerAZRF(main.getPlacement(), 3, null, false);
    main.setReplicationFactor(3);

    UniverseDefinitionTaskParams.PartitionInfo secondary =
        new UniverseDefinitionTaskParams.PartitionInfo();
    secondary.setName("sec");
    secondary.setPlacement(toPlacement(az21, az22));
    secondary.getPlacement().azStream().limit(1).forEach(az -> az.numNodesInAZ = 2);
    secondary.setReplicationFactor(3);
    AtomicInteger i = new AtomicInteger(1);
    secondary.getPlacement().azStream().forEach(az -> az.replicationFactor = i.getAndIncrement());

    int numNodes =
        Streams.concat(main.getPlacement().azStream(), secondary.getPlacement().azStream())
            .mapToInt(az -> az.numNodesInAZ)
            .sum();
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.replicationFactor = 3;
    userIntent.numNodes = numNodes;
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.nodePrefix = "univConfCreate";
    taskParams.upsertPrimaryCluster(userIntent, Arrays.asList(main, secondary), null);
    PlacementInfoUtil.updateUniverseDefinition(
        taskParams, customer.getId(), taskParams.getPrimaryCluster().uuid, CREATE);
    taskParams.expectedUniverseVersion = -1;
    // CREATE
    UniverseResp universeResp = universeCRUDHandler.createUniverse(customer, taskParams);
    TaskInfo taskInfo =
        waitForTask(universeResp.taskUUID, Universe.getOrBadRequest(universeResp.universeUUID));
    verifyUniverseTaskSuccess(taskInfo);
    return Universe.getOrBadRequest(universeResp.universeUUID);
  }

  private PlacementInfo toPlacement(AvailabilityZone... zones) {
    PlacementInfo placementInfo = new PlacementInfo();
    for (AvailabilityZone zone : zones) {
      PlacementInfoUtil.addPlacementZone(zone.getUuid(), placementInfo);
    }
    return placementInfo;
  }

  //  @Test
  public void testLeaderlessTabletsBeforeEditFAIL() throws Exception {
    RuntimeConfigEntry.upsertGlobal("yb.checks.leaderless_tablets.timeout", "10s");
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.numNodes = 3;
    userIntent.replicationFactor = 3;
    Universe universe = createUniverse(userIntent);
    initYSQL(universe);
    log.debug("Universe {}", Json.toJson(universe.getUniverseDetails()));
    universe.getNodes().stream()
        .limit(2)
        .forEach(
            n -> {
              try {
                localNodeManager.killProcess(n.getNodeName(), UniverseTaskBase.ServerType.TSERVER);
              } catch (Exception e) {
                throw new RuntimeException("Failed to kill process", e);
              }
            });

    try (YBClient client =
        ybClientService.getClient(
            universe.getMasterAddresses(), universe.getCertificateNodetoNode())) {
      RetryTaskUntilCondition<List<String>> waiter =
          new RetryTaskUntilCondition<>(
              () ->
                  CheckLeaderlessTablets.doGetLeaderlessTablets(
                      universe.getUniverseUUID(),
                      client,
                      nodeUIApiHelper,
                      universe.getNodes().iterator().next().masterHttpPort),
              (lst) -> lst.size() > 0);
      if (!waiter.retryUntilCond(10, TimeUnit.MINUTES.toSeconds(2))) {
        throw new RuntimeException("Failed to wait for leaderless tablets");
      }
    }
    UniverseDefinitionTaskParams.Cluster cluster =
        universe.getUniverseDetails().getPrimaryCluster();
    cluster.userIntent.numNodes += 1;
    PlacementInfoUtil.updateUniverseDefinition(
        universe.getUniverseDetails(),
        customer.getId(),
        cluster.uuid,
        UniverseConfigureTaskParams.ClusterOperationType.EDIT);
    UUID taskID =
        universeCRUDHandler.update(
            customer,
            Universe.getOrBadRequest(universe.getUniverseUUID()),
            universe.getUniverseDetails());
    TaskInfo taskInfo = waitForTask(taskID, universe);
    assertEquals(TaskInfo.State.Failure, taskInfo.getTaskState());
    String error = getAllErrorsStr(taskInfo);
    assertThat(error, containsString("There are leaderless tablets"));
    assertThat(error, containsString(UniverseConfKeys.leaderlessTabletsCheckEnabled.getKey()));
  }

  @Test
  public void testTimeoutingChangeMasterConfigFAIL() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    RuntimeConfigEntry.upsertGlobal("yb.checks.change_master_config.timeout", "30s");
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    Universe universe = createUniverse(userIntent);
    initYSQL(universe);
    UniverseDefinitionTaskParams.Cluster cluster =
        universe.getUniverseDetails().getPrimaryCluster();
    cluster.placementInfo.azStream().limit(1).forEach(az -> az.uuid = az4.getUuid());
    PlacementInfoUtil.updateUniverseDefinition(
        universe.getUniverseDetails(),
        customer.getId(),
        cluster.uuid,
        UniverseConfigureTaskParams.ClusterOperationType.EDIT);
    assertEquals(
        1,
        universe.getUniverseDetails().nodeDetailsSet.stream()
            .filter(n -> n.state == NodeDetails.NodeState.ToBeAdded)
            .count());
    assertEquals(
        1,
        universe.getUniverseDetails().nodeDetailsSet.stream()
            .filter(n -> n.state == NodeDetails.NodeState.ToBeRemoved)
            .count());
    localNodeManager.setAdditionalGFlags(
        SpecificGFlags.construct(
            Map.of("TEST_inject_latency_during_remote_bootstrap_secs", "600"), new HashMap<>()));
    UUID taskID =
        universeCRUDHandler.update(
            customer,
            Universe.getOrBadRequest(universe.getUniverseUUID()),
            universe.getUniverseDetails());
    TaskInfo taskInfo = waitForTask(taskID, universe);
    assertEquals(TaskInfo.State.Failure, taskInfo.getTaskState());
    assertThat(
        taskInfo.getSubTasks().stream()
            .filter(t -> t.getTaskType() == TaskType.ChangeMasterConfig)
            .findFirst()
            .get()
            .getErrorMessage(),
        containsString("AddMaster operation has not completed within PT30S"));
  }

  @Test
  public void testCreateWrongMasterUniverseUUID_FAIL() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    UUID randomUUID = UUID.randomUUID();
    localNodeManager.setAdditionalGFlags(
        SpecificGFlags.construct(
            Collections.singletonMap(GFlagsUtil.CLUSTER_UUID, randomUUID.toString()),
            Collections.emptyMap()));
    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.nodePrefix = "univConfCreate";
    taskParams.upsertPrimaryCluster(userIntent, null, null);
    PlacementInfoUtil.updateUniverseDefinition(
        taskParams, customer.getId(), taskParams.getPrimaryCluster().uuid, CREATE);
    taskParams.expectedUniverseVersion = -1;
    // CREATE
    UniverseResp universeResp = universeCRUDHandler.createUniverse(customer, taskParams);
    TaskInfo taskInfo =
        waitForTask(universeResp.taskUUID, Universe.getOrBadRequest(universeResp.universeUUID));
    assertEquals(TaskInfo.State.Failure, taskInfo.getTaskState());
    String error = getAllErrorsStr(taskInfo);
    assertThat(
        error,
        containsString(
            String.format("for MASTER cluster_uuid gflag, found '%s'", randomUUID.toString())));
  }

  @Test
  public void testCreateWrongTserverUniverseUUID_FAIL() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    UUID randomUUID = UUID.randomUUID();
    localNodeManager.setAdditionalGFlags(
        SpecificGFlags.construct(
            Collections.emptyMap(),
            Collections.singletonMap(GFlagsUtil.CLUSTER_UUID, randomUUID.toString())));
    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.nodePrefix = "univConfCreate";
    taskParams.upsertPrimaryCluster(userIntent, null, null);
    PlacementInfoUtil.updateUniverseDefinition(
        taskParams, customer.getId(), taskParams.getPrimaryCluster().uuid, CREATE);
    taskParams.expectedUniverseVersion = -1;
    // CREATE
    UniverseResp universeResp = universeCRUDHandler.createUniverse(customer, taskParams);
    TaskInfo taskInfo =
        waitForTask(universeResp.taskUUID, Universe.getOrBadRequest(universeResp.universeUUID));
    assertEquals(TaskInfo.State.Failure, taskInfo.getTaskState());
    String error = getAllErrorsStr(taskInfo);
    assertThat(
        error,
        containsString(
            String.format("for TSERVER cluster_uuid gflag, found '%s'", randomUUID.toString())));
  }

  private NodeDetails silentlyRemoveNode(Universe universe, boolean isMaster, boolean isTserver) {
    AtomicReference<NodeDetails> node = new AtomicReference<>();
    Universe.saveDetails(
        universe.getUniverseUUID(),
        u -> {
          UniverseDefinitionTaskParams details = u.getUniverseDetails();
          details.getPrimaryCluster().userIntent.numNodes--;
          NodeDetails nodeToRemove =
              details.nodeDetailsSet.stream()
                  .filter(n -> n.isTserver == isTserver && n.isMaster == isMaster)
                  .findFirst()
                  .get();
          node.set(nodeToRemove);
          details.nodeDetailsSet.remove(nodeToRemove);
          details
              .getPrimaryCluster()
              .placementInfo
              .azStream()
              .filter(az -> az.uuid.equals(nodeToRemove.azUuid))
              .forEach(az -> az.numNodesInAZ--);
          u.setUniverseDetails(details);
        });
    return node.get();
  }

  private void changeNumberOfNodesInPrimary(Universe universe, int increment) {
    changeNumberOfNodesInCluster(
        universe, universe.getUniverseDetails().getPrimaryCluster().uuid, increment);
  }

  private void changeNumberOfNodesInCluster(Universe universe, UUID clusterUUID, int increment) {
    UniverseDefinitionTaskParams.Cluster cluster =
        universe.getUniverseDetails().getClusterByUuid(clusterUUID);
    cluster.userIntent.numNodes += increment;
    PlacementInfoUtil.updateUniverseDefinition(
        universe.getUniverseDetails(),
        customer.getId(),
        cluster.uuid,
        UniverseConfigureTaskParams.ClusterOperationType.EDIT);
    verifyNodeModifications(
        universe, increment > 0 ? increment : 0, increment < 0 ? -increment : 0);
  }
}
