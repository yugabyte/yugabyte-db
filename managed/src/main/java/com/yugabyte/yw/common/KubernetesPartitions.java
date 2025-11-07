// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import com.yugabyte.yw.commissioner.tasks.KubernetesTaskBase;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.ArrayList;
import java.util.Arrays;
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
        boolean isReadOnlyCluster,
        int numMasters,
        int numTservers,
        KubernetesTaskBase.PodUpgradeParams podUpgradeParams,
        Function<Integer, String> podNameFunction,
        Function<Integer, NodeDetails> nodeFunction) {
      this.numPods = numPods;
      this.serverType = serverType;
      this.numMasters = numMasters;
      this.numTservers = numTservers;
      this.podNameFunction = podNameFunction;
      this.nodeFunction = nodeFunction;

      if (podUpgradeParams.rollMaxBatchSize != null
          && serverType == UniverseTaskBase.ServerType.TSERVER) {
        rollStep =
            isReadOnlyCluster
                ? podUpgradeParams.rollMaxBatchSize.getReadReplicaBatchSize()
                : podUpgradeParams.rollMaxBatchSize.getPrimaryBatchSize();
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
      int masterPartition =
          Arrays.asList(UniverseTaskBase.ServerType.MASTER, UniverseTaskBase.ServerType.EITHER)
                  .contains(serverType)
              ? partition
              : numMasters;
      int tserverPartition =
          Arrays.asList(UniverseTaskBase.ServerType.EITHER, UniverseTaskBase.ServerType.TSERVER)
                  .contains(serverType)
              ? partition
              : numTservers;

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
