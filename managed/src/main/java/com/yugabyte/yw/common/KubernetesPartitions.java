// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.yugabyte.yw.commissioner.tasks.KubernetesTaskBase;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Iterator;
import java.util.List;
import java.util.function.Function;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class KubernetesPartitions {

  public static class KubernetesPartition {
    public final int masterPartition;
    public final int tserverPartition;
    public final List<String> podNames;
    public final List<NodeDetails> nodeList;

    private KubernetesPartition(
        int masterPartition,
        int tserverPartition,
        List<String> podNames,
        List<NodeDetails> nodeList) {
      this.masterPartition = masterPartition;
      this.tserverPartition = tserverPartition;
      this.podNames = podNames;
      this.nodeList = nodeList;
    }
  }

  public static Iterable<KubernetesPartition> iterable(
      int numPods,
      UniverseTaskBase.ServerType serverType,
      boolean masterChanged,
      boolean tserverChanged,
      boolean isReadOnlyCluster,
      int numMasters,
      int numTservers,
      KubernetesTaskBase.PodUpgradeParams podUpgradeParams,
      Function<Integer, String> podNameFunction,
      Function<Integer, NodeDetails> nodeFunction) {
    return () ->
        new KubernetesPartitionsIterator(
            numPods,
            serverType,
            masterChanged,
            tserverChanged,
            isReadOnlyCluster,
            numMasters,
            numTservers,
            podUpgradeParams,
            podNameFunction,
            nodeFunction);
  }

  public static class KubernetesPartitionsIterator
      implements Iterator<KubernetesPartitions.KubernetesPartition> {
    private final int numPods;
    private final UniverseTaskBase.ServerType serverType;
    private final boolean masterChanged;
    private final boolean tserverChanged;
    private final int numMasters;
    private final int numTservers;
    private final Function<Integer, String> podNameFunction;
    private final Function<Integer, NodeDetails> nodeFunction;

    private int index = 0;
    private final int maxIndex;
    private int rollStep = 1;

    public KubernetesPartitionsIterator(
        int numPods,
        UniverseTaskBase.ServerType serverType,
        boolean masterChanged,
        boolean tserverChanged,
        boolean isReadOnlyCluster,
        int numMasters,
        int numTservers,
        KubernetesTaskBase.PodUpgradeParams podUpgradeParams,
        Function<Integer, String> podNameFunction,
        Function<Integer, NodeDetails> nodeFunction) {
      this.numPods = numPods;
      this.serverType = serverType;
      this.masterChanged = masterChanged;
      this.tserverChanged = tserverChanged;
      this.numMasters = numMasters;
      this.numTservers = numTservers;
      this.podNameFunction = podNameFunction;
      this.nodeFunction = nodeFunction;

      if (podUpgradeParams.rollMaxBatchSize != null
          && serverType == UniverseTaskBase.ServerType.TSERVER) {
        rollStep =
            isReadOnlyCluster
                ? podUpgradeParams.rollMaxBatchSize.getReadReplicaBatchSize()
                : podUpgradeParams.rollMaxBatchSize.getReadReplicaBatchSize();
        rollStep = Math.min(numPods, rollStep);
        if (rollStep > 1) {
          log.debug("Using roll step of {}", rollStep);
        }
      }
      maxIndex = (int) Math.ceil((double) numPods / rollStep);
    }

    @Override
    public boolean hasNext() {
      return index < maxIndex;
    }

    @Override
    public KubernetesPartition next() {
      if (!hasNext()) {
        throw new IndexOutOfBoundsException();
      }
      int partition = Math.max(0, numPods - (index + 1) * rollStep);
      // Upgrade the master pods individually for each deployment.
      // Possible scenarios:
      // 1) Upgrading masters.
      //    a) The tservers have no changes. In that case, the tserver partition should
      //       be 0.
      //    b) The tservers have changes. In the case of an edit, the value should be
      //       the old number of tservers (since the new will already have the updated values).
      //       Otherwise, the value should be the number of existing pods (since we don't
      //       want any pods to be rolled.)
      // 2) Upgrading the tserver. In this case, the masters will always have already rolled.
      //    So it is safe to assume for all current supported operations via upgrade,
      //    this will be a no-op. But for future proofing, we try to model it the same way
      //    as the tservers.
      int masterPartition = masterChanged ? numMasters : 0;
      int tserverPartition = tserverChanged ? numTservers : 0;
      masterPartition =
          serverType == UniverseTaskBase.ServerType.MASTER ? partition : masterPartition;
      tserverPartition =
          serverType == UniverseTaskBase.ServerType.TSERVER ? partition : tserverPartition;

      int prevPartition = numPods - index * rollStep;
      List<String> podNames = new ArrayList<>();
      List<NodeDetails> nodeList = new ArrayList<>();
      for (int j = 0; j < prevPartition - partition; j++) {
        NodeDetails node = nodeFunction.apply(partition + j);
        nodeList.add(node);
        String podName = podNameFunction.apply(partition + j);
        podNames.add(podName);
      }
      KubernetesPartition kubernetesPartition =
          new KubernetesPartition(
              masterPartition,
              tserverPartition,
              Collections.unmodifiableList(podNames),
              Collections.unmodifiableList(nodeList));
      index++;
      return kubernetesPartition;
    }
  }
}
