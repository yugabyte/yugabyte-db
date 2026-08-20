// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.local;

import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static org.junit.Assert.assertEquals;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.forms.DrConfigFailoverForm;
import com.yugabyte.yw.forms.DrConfigReplaceReplicaForm;
import com.yugabyte.yw.forms.DrConfigRestartForm;
import com.yugabyte.yw.forms.DrConfigSafetimeResp;
import com.yugabyte.yw.forms.DrConfigSwitchoverForm;
import com.yugabyte.yw.forms.TableInfoForm;
import com.yugabyte.yw.models.DrConfig;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.io.IOException;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.junit.Test;
import org.yb.client.GetXClusterOutboundReplicationGroupInfoResponse;
import org.yb.client.YBClientApi;
import play.libs.Json;
import play.mvc.Result;

/**
 * Switchover, failover and replace-replica coverage for database-scoped xCluster. Split out of
 * {@link DRDbScopedLocalTest}, which was one of the two slowest classes in the local suite and
 * therefore set its wall-clock floor.
 */
@Slf4j
public class DRDbScopedSwitchoverLocalTest extends DRDbScopedLocalTestBase {

  @Test
  public void testDbScopedFailoverRestartWithSwitchover() throws InterruptedException, IOException {
    CreateDRMetadata createData = defaultDbDRCreate();
    UUID drConfigUUID = createData.drConfigUUID;
    Universe sourceUniverse = createData.sourceUniverse;
    Universe targetUniverse = createData.targetUniverse;
    List<Db> dbs = createData.dbs;
    List<Table> tables = createData.tables;

    Result safetimeResult = getSafeTime(drConfigUUID);
    JsonNode safeTimeJson = (ObjectNode) Json.parse(contentAsString(safetimeResult));
    DrConfigSafetimeResp safeTimeResp = Json.fromJson(safeTimeJson, DrConfigSafetimeResp.class);

    // Get the namespace info for the target universe.
    List<TableInfoForm.NamespaceInfoResp> namespaceInfo =
        tableHandler.listNamespaces(customer.getUuid(), targetUniverse.getUniverseUUID(), false);

    Set<String> targetDbIds = new HashSet<>();
    for (TableInfoForm.NamespaceInfoResp namespace : namespaceInfo) {
      for (Db db : dbs) {
        if (db.name.equals(namespace.name)) {
          targetDbIds.add(Util.getIdRepresentation(namespace.namespaceUUID));
          break;
        }
      }
    }

    assertEquals(dbs.size(), targetDbIds.size());
    assertEquals(targetDbIds.size(), safeTimeResp.safetimes.size());

    // Insert new rows that will not be restored by PITR safetime as we have already
    //   gotten the safetime earlier.
    insertRow(sourceUniverse, tables.get(0), Map.of("id", "8", "name", "'val8'"));
    validateRowCount(targetUniverse, tables.get(0), 2 /* expectedRows */);

    // Take down source universe nodes.
    for (NodeDetails sourceNode : sourceUniverse.getNodes()) {
      killProcessOnNode(sourceUniverse.getUniverseUUID(), sourceNode.nodeName, ServerType.TSERVER);
    }

    // Get xcluster config replication group name.
    // Then check that the outbound replication group is deleted.
    String oldReplicationGroupName =
        DrConfig.getOrBadRequest(drConfigUUID).getActiveXClusterConfig().getReplicationGroupName();

    // Failover DR config.
    DrConfigFailoverForm drFailoverForm = new DrConfigFailoverForm();
    drFailoverForm.primaryUniverseUuid = targetUniverse.getUniverseUUID();
    drFailoverForm.drReplicaUniverseUuid = sourceUniverse.getUniverseUUID();
    Map<String, Long> namespaceIdSafetimeEpochUsMap = new HashMap<>();
    for (DrConfigSafetimeResp.NamespaceSafetime namespaceSafetime : safeTimeResp.safetimes) {
      if (targetDbIds.contains(namespaceSafetime.getNamespaceId())) {
        namespaceIdSafetimeEpochUsMap.put(
            namespaceSafetime.getNamespaceId(), namespaceSafetime.getSafetimeEpochUs());
      }
    }
    assertEquals(2, namespaceIdSafetimeEpochUsMap.size());
    drFailoverForm.namespaceIdSafetimeEpochUsMap = namespaceIdSafetimeEpochUsMap;
    Result failoverResult = failover(drConfigUUID, drFailoverForm);
    assertOk(failoverResult);
    JsonNode json = Json.parse(contentAsString(failoverResult));
    TaskInfo taskInfo =
        waitForTask(UUID.fromString(json.get("taskUUID").asText()), sourceUniverse, targetUniverse);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    Universe newSourceUniverse = Universe.getOrBadRequest(targetUniverse.getUniverseUUID());
    Universe newTargetUniverse = Universe.getOrBadRequest(sourceUniverse.getUniverseUUID());
    verifyUniverseState(newSourceUniverse);

    for (NodeDetails newTargetNode : newTargetUniverse.getNodes()) {
      startProcessesOnNode(newTargetUniverse.getUniverseUUID(), newTargetNode, ServerType.TSERVER);
    }

    // Wait for tservers to start.
    try (YBClientApi client =
        ybClientService.getClient(
            newTargetUniverse.getMasterAddresses(), newTargetUniverse.getCertificateNodetoNode())) {
      waitTillNumOfTservers(client, 3);
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
    verifyUniverseState(newTargetUniverse);

    // Repair DR config.
    DrConfigRestartForm restartForm = new DrConfigRestartForm();
    restartForm.dbs = new HashSet<String>();
    Result repairResult = restart(drConfigUUID, restartForm);
    assertOk(repairResult);
    json = Json.parse(contentAsString(repairResult));
    taskInfo =
        waitForTask(
            UUID.fromString(json.get("taskUUID").asText()), newSourceUniverse, newTargetUniverse);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    verifyUniverseState(Universe.getOrBadRequest(newSourceUniverse.getUniverseUUID()));
    verifyUniverseState(Universe.getOrBadRequest(newTargetUniverse.getUniverseUUID()));

    // Validate newSourceUniverse -> newTargetUniverse replication succeeds.
    insertRow(newSourceUniverse, tables.get(0), Map.of("id", "2", "name", "'val2'"));
    validateRowCount(newTargetUniverse, tables.get(0), 2 /* expectedRows */);

    insertRow(newSourceUniverse, tables.get(1), Map.of("id", "11", "name", "'val11'"));
    validateRowCount(newTargetUniverse, tables.get(1), 2 /* expectedRows */);

    // Perform a switchover to make sure that new outbound replication group can be created even
    // though stale outbound replication group is persisted.
    DrConfigSwitchoverForm switchoverForm = new DrConfigSwitchoverForm();
    switchoverForm.primaryUniverseUuid = newTargetUniverse.getUniverseUUID();
    switchoverForm.drReplicaUniverseUuid = newSourceUniverse.getUniverseUUID();

    Result switchoverResult = switchover(drConfigUUID, switchoverForm);
    assertOk(switchoverResult);
    json = Json.parse(contentAsString(switchoverResult));
    taskInfo =
        waitForTask(
            UUID.fromString(json.get("taskUUID").asText()), newSourceUniverse, newTargetUniverse);
    sourceUniverse = Universe.getOrBadRequest(newTargetUniverse.getUniverseUUID());
    targetUniverse = Universe.getOrBadRequest(newSourceUniverse.getUniverseUUID());
    verifyUniverseState(Universe.getOrBadRequest(sourceUniverse.getUniverseUUID()));
    verifyUniverseState(Universe.getOrBadRequest(targetUniverse.getUniverseUUID()));

    // Sleep to make sure roles are propogated properly after switchover.
    Thread.sleep(5000);

    // Check outbound replication on old source universe is deleted.
    try (YBClientApi client =
        ybClientService.getClient(
            newTargetUniverse.getMasterAddresses(), newTargetUniverse.getCertificateNodetoNode())) {
      try {
        GetXClusterOutboundReplicationGroupInfoResponse rgInfo =
            client.getXClusterOutboundReplicationGroupInfo(oldReplicationGroupName);
        throw new RuntimeException(
            String.format(
                "oldReplicationGroupName %s should not be found and should have been deleted",
                oldReplicationGroupName));
      } catch (Exception ignored) {
        log.debug(
            "The outbound replication group does not exist as expected {}", ignored.getMessage());
        // The outbound replication group does not exist as expected.
      }
    } catch (Exception e) {
      throw new RuntimeException(e);
    }

    // Validate newSourceUniverse -> newTargetUniverse replication succeeds.
    insertRow(sourceUniverse, tables.get(0), Map.of("id", "3", "name", "'val3'"));
    validateRowCount(targetUniverse, tables.get(0), 3 /* expectedRows */);

    insertRow(sourceUniverse, tables.get(1), Map.of("id", "12", "name", "'val12'"));
    validateRowCount(targetUniverse, tables.get(1), 3 /* expectedRows */);

    deleteDrConfig(drConfigUUID, sourceUniverse, targetUniverse);

    for (Db db : dbs) {
      dropDatabase(sourceUniverse, db);
      dropDatabase(targetUniverse, db);
    }
  }

  @Test
  public void testDbScopedChangeReplica() throws InterruptedException {
    CreateDRMetadata createData = defaultDbDRCreate(1, 1);
    UUID drConfigUUID = createData.drConfigUUID;
    Universe sourceUniverse = createData.sourceUniverse;
    Universe targetUniverse = createData.targetUniverse;
    List<Db> dbs = createData.dbs;
    List<Table> tables = createData.tables;

    Universe newTargetUniverse =
        createDRUniverse(DB_SCOPED_STABLE_VERSION, "new-target-universe", true);
    createTestSet(newTargetUniverse, dbs, tables);

    // Replace replica to use newly created universe.
    DrConfigReplaceReplicaForm replaceReplicaForm = new DrConfigReplaceReplicaForm();
    replaceReplicaForm.primaryUniverseUuid = sourceUniverse.getUniverseUUID();
    replaceReplicaForm.drReplicaUniverseUuid = newTargetUniverse.getUniverseUUID();
    Result replaceReplicaResult = replaceReplica(drConfigUUID, replaceReplicaForm);
    assertOk(replaceReplicaResult);
    JsonNode json = Json.parse(contentAsString(replaceReplicaResult));
    TaskInfo taskInfo =
        waitForTask(
            UUID.fromString(json.get("taskUUID").asText()), sourceUniverse, newTargetUniverse);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    verifyUniverseState(Universe.getOrBadRequest(sourceUniverse.getUniverseUUID()));
    verifyUniverseState(Universe.getOrBadRequest(newTargetUniverse.getUniverseUUID()));

    // Validate new replication works.
    insertRow(sourceUniverse, tables.get(0), Map.of("id", "2", "name", "'val2'"));
    validateRowCount(newTargetUniverse, tables.get(0), 2 /* expectedRows */);

    insertRow(sourceUniverse, tables.get(1), Map.of("id", "11", "name", "'val11'"));
    validateRowCount(newTargetUniverse, tables.get(1), 2 /* expectedRows */);

    deleteDrConfig(drConfigUUID, sourceUniverse, newTargetUniverse);

    for (Db db : dbs) {
      dropDatabase(sourceUniverse, db);
      dropDatabase(targetUniverse, db);
      dropDatabase(newTargetUniverse, db);
    }
  }

  @Test
  public void testDbScopedFailoverChangeReplica() throws InterruptedException {
    CreateDRMetadata createData = defaultDbDRCreate(1, 1);
    UUID drConfigUUID = createData.drConfigUUID;
    Universe sourceUniverse = createData.sourceUniverse;
    Universe targetUniverse = createData.targetUniverse;
    List<Db> dbs = createData.dbs;
    List<Table> tables = createData.tables;

    Result safetimeResult = getSafeTime(drConfigUUID);
    JsonNode safeTimeJson = (ObjectNode) Json.parse(contentAsString(safetimeResult));
    DrConfigSafetimeResp safeTimeResp = Json.fromJson(safeTimeJson, DrConfigSafetimeResp.class);

    // Get the namespace info for the target universe.
    List<TableInfoForm.NamespaceInfoResp> namespaceInfo =
        tableHandler.listNamespaces(customer.getUuid(), targetUniverse.getUniverseUUID(), false);

    Set<String> targetDbIds = new HashSet<>();
    for (TableInfoForm.NamespaceInfoResp namespace : namespaceInfo) {
      for (Db db : dbs) {
        if (db.name.equals(namespace.name)) {
          targetDbIds.add(Util.getIdRepresentation(namespace.namespaceUUID));
          break;
        }
      }
    }

    assertEquals(dbs.size(), targetDbIds.size());
    assertEquals(targetDbIds.size(), safeTimeResp.safetimes.size());

    // Insert new rows that will not be restored by PITR safetime as we have already
    //   gotten the safetime earlier.
    insertRow(sourceUniverse, tables.get(0), Map.of("id", "8", "name", "'val8'"));
    validateRowCount(targetUniverse, tables.get(0), 2 /* expectedRows */);

    // Failover DR config.
    DrConfigFailoverForm drFailoverForm = new DrConfigFailoverForm();
    drFailoverForm.primaryUniverseUuid = targetUniverse.getUniverseUUID();
    drFailoverForm.drReplicaUniverseUuid = sourceUniverse.getUniverseUUID();
    Map<String, Long> namespaceIdSafetimeEpochUsMap = new HashMap<>();
    for (DrConfigSafetimeResp.NamespaceSafetime namespaceSafetime : safeTimeResp.safetimes) {
      if (targetDbIds.contains(namespaceSafetime.getNamespaceId())) {
        namespaceIdSafetimeEpochUsMap.put(
            namespaceSafetime.getNamespaceId(), namespaceSafetime.getSafetimeEpochUs());
      }
    }
    assertEquals(2, namespaceIdSafetimeEpochUsMap.size());
    drFailoverForm.namespaceIdSafetimeEpochUsMap = namespaceIdSafetimeEpochUsMap;
    Result failoverResult = failover(drConfigUUID, drFailoverForm);
    assertOk(failoverResult);
    JsonNode json = Json.parse(contentAsString(failoverResult));
    TaskInfo taskInfo =
        waitForTask(UUID.fromString(json.get("taskUUID").asText()), sourceUniverse, targetUniverse);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    verifyUniverseState(Universe.getOrBadRequest(sourceUniverse.getUniverseUUID()));
    verifyUniverseState(Universe.getOrBadRequest(targetUniverse.getUniverseUUID()));

    Universe newSourceUniverse = Universe.getOrBadRequest(targetUniverse.getUniverseUUID());
    Universe newTargetUniverse =
        createDRUniverse(DB_SCOPED_STABLE_VERSION, "new-target-universe", true);
    createTestSet(newTargetUniverse, dbs, tables);

    // Replace replica to use newly created universe.
    DrConfigReplaceReplicaForm replaceReplicaForm = new DrConfigReplaceReplicaForm();
    replaceReplicaForm.primaryUniverseUuid = newSourceUniverse.getUniverseUUID();
    replaceReplicaForm.drReplicaUniverseUuid = newTargetUniverse.getUniverseUUID();
    Result replaceReplicaResult = replaceReplica(drConfigUUID, replaceReplicaForm);
    assertOk(replaceReplicaResult);
    json = Json.parse(contentAsString(replaceReplicaResult));
    taskInfo =
        waitForTask(
            UUID.fromString(json.get("taskUUID").asText()), newSourceUniverse, newTargetUniverse);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    verifyUniverseState(Universe.getOrBadRequest(newSourceUniverse.getUniverseUUID()));
    verifyUniverseState(Universe.getOrBadRequest(newTargetUniverse.getUniverseUUID()));

    // Validate new replication works.
    insertRow(newSourceUniverse, tables.get(0), Map.of("id", "2", "name", "'val2'"));
    validateRowCount(newTargetUniverse, tables.get(0), 2 /* expectedRows */);

    insertRow(newSourceUniverse, tables.get(1), Map.of("id", "11", "name", "'val11'"));
    validateRowCount(newTargetUniverse, tables.get(1), 2 /* expectedRows */);

    deleteDrConfig(drConfigUUID, newSourceUniverse, newTargetUniverse);
  }

  @Test
  public void testDbScopedSwitchover() throws InterruptedException {
    CreateDRMetadata createData = defaultDbDRCreate();
    UUID drConfigUUID = createData.drConfigUUID;
    Universe sourceUniverse = createData.sourceUniverse;
    Universe targetUniverse = createData.targetUniverse;
    List<Db> dbs = createData.dbs;
    List<Table> tables = createData.tables;

    DrConfigSwitchoverForm switchoverForm = new DrConfigSwitchoverForm();
    switchoverForm.primaryUniverseUuid = targetUniverse.getUniverseUUID();
    switchoverForm.drReplicaUniverseUuid = sourceUniverse.getUniverseUUID();

    Result switchoverResult = switchover(drConfigUUID, switchoverForm);
    assertOk(switchoverResult);
    JsonNode json = Json.parse(contentAsString(switchoverResult));
    TaskInfo taskInfo =
        waitForTask(UUID.fromString(json.get("taskUUID").asText()), sourceUniverse, targetUniverse);
    Universe newSourceUniverse = Universe.getOrBadRequest(targetUniverse.getUniverseUUID());
    Universe newTargetUniverse = Universe.getOrBadRequest(sourceUniverse.getUniverseUUID());
    verifyUniverseState(Universe.getOrBadRequest(newSourceUniverse.getUniverseUUID()));
    verifyUniverseState(Universe.getOrBadRequest(newTargetUniverse.getUniverseUUID()));

    // Sleep to make sure roles are propogated properly after switchover.
    Thread.sleep(5000);

    // Validate newSourceUniverse -> newTargetUniverse replication succeeds.
    insertRow(newSourceUniverse, tables.get(0), Map.of("id", "2", "name", "'val2'"));
    validateRowCount(newTargetUniverse, tables.get(0), 2 /* expectedRows */);

    insertRow(newSourceUniverse, tables.get(1), Map.of("id", "11", "name", "'val11'"));
    validateRowCount(newTargetUniverse, tables.get(1), 2 /* expectedRows */);

    DrConfigSwitchoverForm newSwitchoverForm = new DrConfigSwitchoverForm();
    newSwitchoverForm.primaryUniverseUuid = newTargetUniverse.getUniverseUUID();
    newSwitchoverForm.drReplicaUniverseUuid = newSourceUniverse.getUniverseUUID();

    Result newSwitchoverResult = switchover(drConfigUUID, newSwitchoverForm);
    assertOk(newSwitchoverResult);
    json = Json.parse(contentAsString(newSwitchoverResult));
    taskInfo =
        waitForTask(
            UUID.fromString(json.get("taskUUID").asText()), newSourceUniverse, newTargetUniverse);
    Universe newSourceUniverse2 = Universe.getOrBadRequest(newTargetUniverse.getUniverseUUID());
    Universe newTargetUniverse2 = Universe.getOrBadRequest(newSourceUniverse.getUniverseUUID());
    verifyUniverseState(Universe.getOrBadRequest(newSourceUniverse2.getUniverseUUID()));
    verifyUniverseState(Universe.getOrBadRequest(newTargetUniverse2.getUniverseUUID()));

    // Sleep to make sure roles are propogated properly after switchover.
    Thread.sleep(5000);

    // Validate newSourceUniverse -> newTargetUniverse replication succeeds.
    insertRow(newSourceUniverse2, tables.get(0), Map.of("id", "3", "name", "'val3'"));
    validateRowCount(newTargetUniverse2, tables.get(0), 3 /* expectedRows */);

    insertRow(newSourceUniverse2, tables.get(1), Map.of("id", "12", "name", "'val12'"));
    validateRowCount(newTargetUniverse2, tables.get(1), 3 /* expectedRows */);

    deleteDrConfig(drConfigUUID, newSourceUniverse2, newTargetUniverse2);

    // Should be able to drop dbs as PITRs are deleted.
    for (Db db : dbs) {
      dropDatabase(newSourceUniverse2, db);
      dropDatabase(newTargetUniverse2, db);
    }
  }
}
