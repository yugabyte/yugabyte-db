// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models.helpers;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.DeltaEvaluator;
import com.yugabyte.yw.common.DeltaEvaluator.DeltaRetainLatest;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UniverseTaskParams;
import java.lang.reflect.Field;
import java.util.Arrays;
import java.util.Collections;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import java.util.stream.Stream;
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
  public void applyBeforeToPreservesFieldsChangedAfterFreeze() {
    UUID primaryUuid = UUID.randomUUID();
    UUID readOnlyUuid = UUID.randomUUID();
    UniverseDefinitionTaskParams before = new UniverseDefinitionTaskParams();
    before.clusters.add(cluster(primaryUuid, ClusterType.PRIMARY, "2"));
    before.clusters.add(cluster(readOnlyUuid, ClusterType.ASYNC, "2"));
    before.nodePrefix = "before-prefix";

    // The task removes the read replica.
    UniverseDefinitionTaskParams target = new UniverseDefinitionTaskParams();
    target.clusters.add(cluster(primaryUuid, ClusterType.PRIMARY, "2"));
    target.nodePrefix = "after-prefix";

    JsonNode delta = DeltaEvaluator.buildDeltaJsonTree(before, target);
    StateTransitionDetails details = new StateTransitionDetails(true /* rollbackSafe */, delta);

    UUID paCollectorUuid = UUID.randomUUID();
    UUID paEndpointUuid = UUID.randomUUID();
    UniverseDefinitionTaskParams current = new UniverseDefinitionTaskParams();
    current.clusters.add(cluster(primaryUuid, ClusterType.PRIMARY, "8"));
    current.nodePrefix = "after-prefix";
    current.installNodeAgent = true;
    current.nodeAgentMissing = true;
    current.setPaCollectorUuid(paCollectorUuid);
    current.setPaEndpointUuid(paEndpointUuid);

    UniverseDefinitionTaskParams restored = details.applyBeforeTo(current);

    assertEquals("before-prefix", restored.nodePrefix);
    assertEquals(ybcFlags("8"), restored.getClusterByUuid(primaryUuid).userIntent.ybcFlags);
    // Not in current, so it keeps the restored flags.
    assertEquals(ybcFlags("2"), restored.getClusterByUuid(readOnlyUuid).userIntent.ybcFlags);
    assertTrue(restored.installNodeAgent);
    assertTrue(restored.nodeAgentMissing);
    assertEquals(paCollectorUuid, restored.getPaCollectorUuid());
    assertEquals(paEndpointUuid, restored.getPaEndpointUuid());
  }

  @Test
  public void applyBeforeToPreservesCurrentOverDeltaChange() {
    UUID primaryUuid = UUID.randomUUID();
    UniverseDefinitionTaskParams before = new UniverseDefinitionTaskParams();
    before.clusters.add(cluster(primaryUuid, ClusterType.PRIMARY, "2"));
    before.nodePrefix = "before-prefix";
    before.sequenceNumber = 2;
    before.updateInProgress = false;

    // Stale task params make the delta record changes to the annotated fields too.
    UniverseDefinitionTaskParams target = new UniverseDefinitionTaskParams();
    target.clusters.add(cluster(primaryUuid, ClusterType.PRIMARY, "5"));
    target.nodePrefix = "after-prefix";
    target.sequenceNumber = 3;
    target.updateInProgress = true;

    JsonNode delta = DeltaEvaluator.buildDeltaJsonTree(before, target);
    StateTransitionDetails details = new StateTransitionDetails(true /* rollbackSafe */, delta);

    UniverseDefinitionTaskParams current = new UniverseDefinitionTaskParams();
    current.clusters.add(cluster(primaryUuid, ClusterType.PRIMARY, "8"));
    current.nodePrefix = "after-prefix";
    current.sequenceNumber = 4;
    current.updateInProgress = true;

    UniverseDefinitionTaskParams restored = details.applyBeforeTo(current);

    assertEquals("before-prefix", restored.nodePrefix);
    assertEquals(ybcFlags("8"), restored.getClusterByUuid(primaryUuid).userIntent.ybcFlags);
    assertEquals(4, restored.sequenceNumber);
    assertTrue(restored.updateInProgress);
  }

  // A field added to or removed from this set changes what a rollback keeps from current.
  @Test
  public void retainLatestFieldsOfUniverseDetails() {
    Set<String> fields =
        Stream.of(UniverseTaskParams.class, UniverseDefinitionTaskParams.class, UserIntent.class)
            .flatMap(c -> Arrays.stream(c.getDeclaredFields()))
            .filter(f -> f.isAnnotationPresent(DeltaRetainLatest.class))
            .map(Field::getName)
            .collect(Collectors.toSet());
    assertEquals(
        Set.of(
            "updateInProgress",
            "updatingTask",
            "updatingTaskUUID",
            "updateSucceeded",
            "placementModificationTaskUuid",
            "sequenceNumber",
            "installNodeAgent",
            "nodeAgentMissing",
            "paCollectorUuid",
            "paEndpointUuid",
            "enableYbc",
            "ybcSoftwareVersion",
            "ybcInstalled",
            "ybcFlags"),
        fields);
  }

  @Test
  public void getBeforeUniverseDetailsPutsPrimaryFirst() {
    UUID primaryUuid = UUID.randomUUID();
    UUID readReplicaUuid = UUID.randomUUID();
    UniverseDefinitionTaskParams before = new UniverseDefinitionTaskParams();
    before.clusters.add(cluster(primaryUuid, ClusterType.PRIMARY, "2"));
    before.clusters.add(cluster(readReplicaUuid, ClusterType.ASYNC, "2"));
    UniverseDefinitionTaskParams target = new UniverseDefinitionTaskParams();
    target.clusters.add(cluster(primaryUuid, ClusterType.PRIMARY, "2"));
    target.clusters.add(cluster(readReplicaUuid, ClusterType.ASYNC, "2"));
    target.getClusterByUuid(readReplicaUuid).userIntent.numNodes = 2;
    // An older read-replica-only edit wrote its target with the primary last.
    Collections.reverse(target.clusters);
    StateTransitionDetails details =
        new StateTransitionDetails(
            true /* rollbackSafe */, DeltaEvaluator.buildDeltaJsonTree(before, target));

    UniverseDefinitionTaskParams restored = details.getBeforeUniverseDetails();

    assertEquals(primaryUuid, restored.clusters.get(0).uuid);
    assertEquals(readReplicaUuid, restored.clusters.get(1).uuid);
    assertEquals(0, restored.getClusterByUuid(readReplicaUuid).userIntent.numNodes);
  }

  private static Cluster cluster(UUID uuid, ClusterType clusterType, String maxUploads) {
    UserIntent userIntent = new UserIntent();
    userIntent.ybcFlags = ybcFlags(maxUploads);
    Cluster cluster = new Cluster(clusterType, userIntent);
    cluster.uuid = uuid;
    return cluster;
  }

  private static Map<String, String> ybcFlags(String maxUploads) {
    return Map.of("max_concurrent_uploads", maxUploads);
  }
}
