// Copyright (c) YugabyteDB, Inc.
package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.ServerSubTaskParams;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;

@Slf4j
public class CheckTabletsMovementAvailable extends BaseTabletsMovementCheck {

  @Inject
  protected CheckTabletsMovementAvailable(BaseTaskDependencies baskTaskDependencies) {
    super(baskTaskDependencies);
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  public static class Params extends ServerSubTaskParams {
    public UUID clusterUUID;
    public List<UniverseDefinitionTaskParams.PartitionInfo> targetGeoPartitions;
    public List<NodeDetails> removedNodes;
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    UniverseDefinitionTaskParams.Cluster cluster = universe.getCluster(taskParams().clusterUUID);
    if (!cluster.userIntent.enableYSQL) {
      log.warn("YSQL is not enabled, ignoring");
      return;
    }
    if (CollectionUtils.isEmpty(taskParams().removedNodes)) {
      return;
    }
    boolean autoTablespaceUpdate =
        confGetter.getGlobalConf(GlobalConfKeys.automaticTablespaceUpdate);
    Set<String> tablespacesToIgnore = new HashSet<>();
    if (autoTablespaceUpdate) {
      for (UniverseDefinitionTaskParams.PartitionInfo geoPartition : cluster.getPartitions()) {
        tablespacesToIgnore.add(geoPartition.getTablespaceName());
      }
    }
    log.debug("Tablespaces to ignore: {}", tablespacesToIgnore);

    PlacementInfo targetPlacementInfo = new PlacementInfo();
    for (UniverseDefinitionTaskParams.PartitionInfo targetGeoPartition :
        taskParams().targetGeoPartitions) {
      targetGeoPartition
          .getPlacement()
          .azStream()
          .forEach(
              az -> {
                PlacementInfoUtil.addPlacementZone(
                    az.uuid, targetPlacementInfo, az.replicationFactor, az.numNodesInAZ);
              });
    }
    checkNodesRemoval(
        taskParams().removedNodes, universe, targetPlacementInfo, tablespacesToIgnore, false);
  }

  @Override
  protected String getErrorMessage() {
    return "Cannot modify universe";
  }
}
