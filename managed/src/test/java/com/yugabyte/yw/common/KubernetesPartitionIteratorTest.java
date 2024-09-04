// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;

import com.yugabyte.yw.commissioner.tasks.KubernetesTaskBase;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.forms.RollMaxBatchSize;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.Iterator;
import org.junit.Test;

public class KubernetesPartitionIteratorTest {
  private int iteration;
  private UniverseTaskBase.ServerType serverType;

  @Test
  public void testNoBatchesOnlyMastersChanged() {
    Iterator<KubernetesPartitions.KubernetesPartition> iterator =
        getIterator(3, UniverseTaskBase.ServerType.MASTER, true, false, false, 3, 3, 1);
    assertValues(iterator, 2, 0, 2);
    assertValues(iterator, 1, 0, 1);
    assertValues(iterator, 0, 0, 0);
    assertFalse(iterator.hasNext());
  }

  @Test
  public void testNoBatchesOnlyTserversChanged() {
    Iterator<KubernetesPartitions.KubernetesPartition> iterator =
        getIterator(3, UniverseTaskBase.ServerType.TSERVER, true, false, false, 3, 3, 1);
    assertValues(iterator, 3, 2, 2);
    assertValues(iterator, 3, 1, 1);
    assertValues(iterator, 3, 0, 0);
    assertFalse(iterator.hasNext());
  }

  @Test
  public void testNoBatchesBothChanged() {
    boolean masterChanged = true;
    boolean tserverChanged = true;
    int numMasters = 3;
    int numTservers = 4;

    // First masters
    Iterator<KubernetesPartitions.KubernetesPartition> iterator =
        getIterator(
            numMasters,
            UniverseTaskBase.ServerType.MASTER,
            masterChanged,
            tserverChanged,
            false,
            numMasters,
            numTservers,
            1);
    assertValues(iterator, 2, 4, 2);
    assertValues(iterator, 1, 4, 1);
    assertValues(iterator, 0, 4, 0);
    assertFalse(iterator.hasNext());
    // Now tservers
    iterator =
        getIterator(
            numTservers,
            UniverseTaskBase.ServerType.TSERVER,
            masterChanged,
            tserverChanged,
            false,
            numMasters,
            numTservers,
            1);
    assertValues(iterator, 3, 3, 3);
    assertValues(iterator, 3, 2, 2);
    assertValues(iterator, 3, 1, 1);
    assertValues(iterator, 3, 0, 0);
    assertFalse(iterator.hasNext());
  }

  @Test
  public void testBatch2MastersChanged() {
    Iterator<KubernetesPartitions.KubernetesPartition> iterator =
        getIterator(3, UniverseTaskBase.ServerType.MASTER, true, false, false, 3, 3, 2);
    assertValues(iterator, 2, 0, 2);
    assertValues(iterator, 1, 0, 1);
    assertValues(iterator, 0, 0, 0);
    assertFalse(iterator.hasNext());
  }

  @Test
  public void testBatch2BothChanged() {
    boolean masterChanged = true;
    boolean tserverChanged = true;
    int numMasters = 3;
    int numTservers = 4;

    // First masters
    Iterator<KubernetesPartitions.KubernetesPartition> iterator =
        getIterator(
            numMasters,
            UniverseTaskBase.ServerType.MASTER,
            masterChanged,
            tserverChanged,
            false,
            numMasters,
            numTservers,
            2);
    assertValues(iterator, 2, 4, 2);
    assertValues(iterator, 1, 4, 1);
    assertValues(iterator, 0, 4, 0);
    assertFalse(iterator.hasNext());
    // Now tservers
    iterator =
        getIterator(
            numTservers,
            UniverseTaskBase.ServerType.TSERVER,
            masterChanged,
            tserverChanged,
            false,
            numMasters,
            numTservers,
            2);
    assertValues(iterator, 3, 2, 2, 3);
    assertValues(iterator, 3, 0, 0, 1);
    assertFalse(iterator.hasNext());
  }

  @Test
  public void testBatch2and5TserversChanged() {
    Iterator<KubernetesPartitions.KubernetesPartition> iterator =
        getIterator(5, UniverseTaskBase.ServerType.TSERVER, true, false, false, 3, 5, 2);
    assertValues(iterator, 3, 3, 3, 4);
    assertValues(iterator, 3, 1, 1, 2);
    assertValues(iterator, 3, 0, 0);
    assertFalse(iterator.hasNext());
  }

  @Test
  public void testBatch3and10TserversChanged() {
    Iterator<KubernetesPartitions.KubernetesPartition> iterator =
        getIterator(10, UniverseTaskBase.ServerType.TSERVER, true, false, false, 3, 10, 3);
    assertValues(iterator, 3, 7, 7, 8, 9);
    assertValues(iterator, 3, 4, 4, 5, 6);
    assertValues(iterator, 3, 1, 1, 2, 3);
    assertValues(iterator, 3, 0, 0);
    assertFalse(iterator.hasNext());
  }

  @Test
  public void testBatch4and3TserversChanged() {
    Iterator<KubernetesPartitions.KubernetesPartition> iterator =
        getIterator(3, UniverseTaskBase.ServerType.TSERVER, false, true, false, 2, 3, 4);
    assertValues(iterator, 0, 0, 0, 1, 2);
    assertFalse(iterator.hasNext());
  }

  @Test
  public void testBatch2ReadOnlyTserversChanged() {
    Iterator<KubernetesPartitions.KubernetesPartition> iterator =
        getIterator(4, UniverseTaskBase.ServerType.TSERVER, false, true, true, 0, 4, 2);
    assertValues(iterator, 0, 2, 2, 3);
    assertValues(iterator, 0, 0, 0, 1);
    assertFalse(iterator.hasNext());
  }

  private void assertValues(
      Iterator<KubernetesPartitions.KubernetesPartition> iterator,
      int masterPartition,
      int tserverPartition,
      Integer... podIndexes) {
    String key = "Iteration " + ++iteration + " " + serverType;
    KubernetesPartitions.KubernetesPartition partition = iterator.next();
    assertEquals(key, masterPartition, partition.masterPartition);
    assertEquals(key, tserverPartition, partition.tserverPartition);
    assertEquals(key, podIndexes.length, partition.podNames.size());
    assertEquals(key, podIndexes.length, partition.nodeList.size());
    for (int i = 0; i < podIndexes.length; i++) {
      assertEquals(key, getPodName(podIndexes[i]), partition.podNames.get(i));
      assertEquals(key, getNodeDetails(podIndexes[i]), partition.nodeList.get(i));
    }
  }

  private Iterator<KubernetesPartitions.KubernetesPartition> getIterator(
      int numPods,
      UniverseTaskBase.ServerType serverType,
      boolean masterChanged,
      boolean tserverChanged,
      boolean isReadonlyCluster,
      int numMasters,
      int numTservers,
      int bachSize) {
    iteration = 0;
    this.serverType = serverType;
    KubernetesTaskBase.PodUpgradeParams params =
        KubernetesTaskBase.PodUpgradeParams.builder()
            .rollMaxBatchSize(RollMaxBatchSize.of(bachSize, bachSize))
            .build();

    return KubernetesPartitions.iterable(
            numPods,
            serverType,
            masterChanged,
            tserverChanged,
            isReadonlyCluster,
            numMasters,
            numTservers,
            params,
            this::getPodName,
            this::getNodeDetails)
        .iterator();
  }

  private NodeDetails getNodeDetails(int part) {
    NodeDetails nodeDetails = new NodeDetails();
    nodeDetails.nodeName = "node" + part;
    return nodeDetails;
  }

  private String getPodName(int part) {
    return "pod" + part;
  }
}
