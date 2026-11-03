// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models.helpers;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.DeltaEvaluator;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import java.util.Map;
import java.util.UUID;
import org.junit.Test;

public class StateTransitionDetailsTest {

  @Test
  public void applyBeforeToPreservesCurrentYbcFields() {
    UniverseDefinitionTaskParams before = new UniverseDefinitionTaskParams();
    before.setYbcSoftwareVersion("1.0.0-b1");
    before.setEnableYbc(true);
    before.setYbcInstalled(true);
    before.nodePrefix = "before-prefix";
    before.sequenceNumber = 2;

    UniverseDefinitionTaskParams target = new UniverseDefinitionTaskParams();
    target.setYbcSoftwareVersion("1.0.0-b1");
    target.setEnableYbc(true);
    target.setYbcInstalled(true);
    target.nodePrefix = "after-prefix";
    target.sequenceNumber = 2;

    JsonNode delta = DeltaEvaluator.buildDeltaJsonTree(before, target);
    StateTransitionDetails details = new StateTransitionDetails(true /* rollbackSafe */, delta);

    UniverseDefinitionTaskParams current = new UniverseDefinitionTaskParams();
    current.setYbcSoftwareVersion("2.0.0-b10");
    current.setEnableYbc(true);
    current.setYbcInstalled(true);
    current.updateInProgress = true;
    current.nodePrefix = "after-prefix";
    current.sequenceNumber = 4;

    UniverseDefinitionTaskParams restored = details.applyBeforeTo(current);

    assertEquals("2.0.0-b10", restored.getYbcSoftwareVersion());
    assertTrue(restored.isEnableYbc());
    assertTrue(restored.isYbcInstalled());
    assertEquals("before-prefix", restored.nodePrefix);
    assertTrue(restored.updateInProgress);
    assertEquals(4, restored.sequenceNumber);
  }

  @Test
  public void applyBeforeToKeepsFieldsWrittenByOtherTasks() {
    UUID primaryUuid = UUID.randomUUID();
    UUID readReplicaUuid = UUID.randomUUID();
    UniverseDefinitionTaskParams before =
        paramsWithClusters(primaryUuid, readReplicaUuid, "before-prefix");
    before.installNodeAgent = true;
    before.nodeAgentMissing = false;
    before.setPaCollectorUuid(UUID.randomUUID());
    before.setPaEndpointUuid(UUID.randomUUID());
    before.clusters.forEach(c -> c.userIntent.ybcFlags = Map.of("max_concurrent_uploads", "2"));
    UniverseDefinitionTaskParams target = paramsWithClusters(primaryUuid, readReplicaUuid, "after");
    target.installNodeAgent = before.installNodeAgent;
    target.setPaCollectorUuid(before.getPaCollectorUuid());
    target.setPaEndpointUuid(before.getPaEndpointUuid());
    target.clusters.forEach(c -> c.userIntent.ybcFlags = Map.of("max_concurrent_uploads", "2"));
    StateTransitionDetails details =
        new StateTransitionDetails(
            true /* rollbackSafe */, DeltaEvaluator.buildDeltaJsonTree(before, target));

    // Written while the failed task waited: node agent install, PA sync dropping the collector,
    // throttle update.
    UniverseDefinitionTaskParams current =
        paramsWithClusters(primaryUuid, readReplicaUuid, "after");
    current.installNodeAgent = false;
    current.nodeAgentMissing = true;
    current.setPaEndpointUuid(before.getPaEndpointUuid());
    current.getPrimaryCluster().userIntent.ybcFlags = Map.of("max_concurrent_uploads", "8");
    current.getClusterByUuid(readReplicaUuid).userIntent.ybcFlags =
        Map.of("max_concurrent_uploads", "6");

    UniverseDefinitionTaskParams restored = details.applyBeforeTo(current);

    assertEquals("before-prefix", restored.nodePrefix);
    assertFalse(restored.installNodeAgent);
    assertTrue(restored.nodeAgentMissing);
    assertNull(restored.getPaCollectorUuid());
    assertEquals(before.getPaEndpointUuid(), restored.getPaEndpointUuid());
    assertEquals(
        Map.of("max_concurrent_uploads", "8"), restored.getPrimaryCluster().userIntent.ybcFlags);
    assertEquals(
        Map.of("max_concurrent_uploads", "6"),
        restored.getClusterByUuid(readReplicaUuid).userIntent.ybcFlags);
  }

  private static UniverseDefinitionTaskParams paramsWithClusters(
      UUID primaryUuid, UUID readReplicaUuid, String nodePrefix) {
    UniverseDefinitionTaskParams params = new UniverseDefinitionTaskParams();
    params.nodePrefix = nodePrefix;
    Cluster primary = new Cluster(ClusterType.PRIMARY, new UserIntent());
    primary.uuid = primaryUuid;
    Cluster readReplica = new Cluster(ClusterType.ASYNC, new UserIntent());
    readReplica.uuid = readReplicaUuid;
    params.clusters.add(primary);
    params.clusters.add(readReplica);
    return params;
  }
}
