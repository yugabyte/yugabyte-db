// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo.PlacementAZ;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.List;
import java.util.function.BiFunction;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class KubernetesFullMoveReplicas {

  public static class KubernetesFullMoveReplica {
    public final List<String> oldPodNames;
    public final List<NodeDetails> oldNodeList;
    public final List<String> newPodNames;
    public final List<NodeDetails> newNodeList;
    public final int prevOldReplicas;
    public final int oldReplicas;
    public final int newReplicas;

    private KubernetesFullMoveReplica(
        List<String> oldPodNames,
        List<NodeDetails> oldNodeList,
        List<String> newPodNames,
        List<NodeDetails> newNodeList,
        int prevOldReplicas,
        int oldReplicas,
        int newReplicas) {
      this.oldPodNames = oldPodNames;
      this.oldNodeList = oldNodeList;
      this.newPodNames = newPodNames;
      this.newNodeList = newNodeList;
      this.prevOldReplicas = prevOldReplicas;
      this.oldReplicas = oldReplicas;
      this.newReplicas = newReplicas;
    }
  }

  public static Iterable<KubernetesFullMoveReplica> iterable(
      int batchSize,
      PlacementAZ currPlacementAZ,
      PlacementAZ newPlacementAZ,
      ServerType serverType,
      BiFunction<Integer, Integer, String> podNameFunction,
      BiFunction<Integer, Integer, NodeDetails> nodeFunction) {
    return () ->
        new KubernetesFullMoveReplicasIterator(
            batchSize, currPlacementAZ, newPlacementAZ, serverType, podNameFunction, nodeFunction);
  }

  public static class KubernetesFullMoveReplicasIterator
      implements Iterator<KubernetesFullMoveReplica> {
    private int batchSize;
    private PlacementAZ currPlacementAZ;
    private PlacementAZ newPlacementAZ;
    private ServerType serverType;
    private BiFunction<Integer, Integer, String> podNameFunction;
    private BiFunction<Integer, Integer, NodeDetails> nodeFunction;
    private int newReplicas = 0, oldReplicas = 0, preOldReplicas = 0, preNewReplicas = 0;
    private int newNumNodes;

    public KubernetesFullMoveReplicasIterator(
        int batchSize,
        PlacementAZ currPlacementAZ,
        PlacementAZ newPlacementAZ,
        ServerType serverType,
        BiFunction<Integer, Integer, String> podNameFunction,
        BiFunction<Integer, Integer, NodeDetails> nodeFunction) {
      this.batchSize = batchSize;
      this.currPlacementAZ = currPlacementAZ;
      this.newPlacementAZ = newPlacementAZ;
      this.serverType = serverType;
      this.podNameFunction = podNameFunction;
      this.nodeFunction = nodeFunction;
      this.newNumNodes =
          serverType == ServerType.TSERVER
              ? newPlacementAZ.numNodesInAZ
              : newPlacementAZ.replicationFactor;
      this.oldReplicas =
          serverType == ServerType.TSERVER
              ? currPlacementAZ.numNodesInAZ
              : currPlacementAZ.replicationFactor;
    }

    @Override
    public boolean hasNext() {
      return oldReplicas != 0 || newReplicas < newNumNodes;
    }

    @Override
    public KubernetesFullMoveReplica next() {
      if (!hasNext()) {
        throw new IndexOutOfBoundsException();
      }
      List<String> oldPodNames = new ArrayList<>(), newPodNames = new ArrayList<>();
      List<NodeDetails> oldNodeList = new ArrayList<>(), newNodeList = new ArrayList<>();
      preOldReplicas = oldReplicas;
      preNewReplicas = newReplicas;
      oldReplicas = batchSize > 0 ? Math.max(0, preOldReplicas - batchSize) : 0;
      newReplicas = batchSize > 0 ? Math.min(newNumNodes, preNewReplicas + batchSize) : newNumNodes;
      for (int j = preOldReplicas - 1; j >= oldReplicas; j--) {
        int stsIndex =
            serverType == ServerType.TSERVER
                ? currPlacementAZ.tsStsIndex
                : currPlacementAZ.masterStsIndex;
        NodeDetails node = nodeFunction.apply(j, stsIndex);
        oldNodeList.add(node);
        String podName = podNameFunction.apply(j, stsIndex);
        oldPodNames.add(podName);
      }
      for (int j = preNewReplicas; j < newReplicas; j++) {
        int stsIndex =
            serverType == ServerType.TSERVER
                ? newPlacementAZ.tsStsIndex
                : newPlacementAZ.masterStsIndex;
        NodeDetails node = nodeFunction.apply(j, stsIndex);
        newNodeList.add(node);
        String podName = podNameFunction.apply(j, stsIndex);
        newPodNames.add(podName);
      }
      return new KubernetesFullMoveReplica(
          oldPodNames,
          oldNodeList,
          newPodNames,
          newNodeList,
          preOldReplicas,
          oldReplicas,
          newReplicas);
    }
  }
}
