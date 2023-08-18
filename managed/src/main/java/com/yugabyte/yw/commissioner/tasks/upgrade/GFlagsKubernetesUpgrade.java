// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.KubernetesUpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.common.gflags.GFlagsValidation;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.forms.GFlagsUpgradeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class GFlagsKubernetesUpgrade extends KubernetesUpgradeTaskBase {

  private final GFlagsValidation gFlagsValidation;
  private final XClusterUniverseService xClusterUniverseService;

  @Inject
  protected GFlagsKubernetesUpgrade(
      BaseTaskDependencies baseTaskDependencies,
      GFlagsValidation gFlagsValidation,
      XClusterUniverseService xClusterUniverseService) {
    super(baseTaskDependencies);
    this.gFlagsValidation = gFlagsValidation;
    this.xClusterUniverseService = xClusterUniverseService;
  }

  @Override
  protected GFlagsUpgradeParams taskParams() {
    return (GFlagsUpgradeParams) taskParams;
  }

  @Override
  public SubTaskGroupType getTaskSubGroupType() {
    return SubTaskGroupType.UpdatingGFlags;
  }

  public SpecificGFlags getPrimaryClusterSpecificGFlags() {
    for (Cluster incomingCluster : taskParams().clusters) {
      if (incomingCluster.clusterType == ClusterType.PRIMARY) {
        UserIntent incomingUserIntent = incomingCluster.userIntent;
        return incomingUserIntent.specificGFlags;
      }
    }
    return null;
  }

  public boolean areGflagsModified(ServerType serverType) {
    List<Cluster> incomingClusters =
        new ArrayList<>(taskParams().getNewVersionsOfClusters(getUniverse()).values());
    for (Cluster incomingCluster : incomingClusters) {
      UserIntent incomingUserIntent = incomingCluster.userIntent;
      Cluster sourceCluster =
          getUniverse().getUniverseDetails().getClusterByUuid(incomingCluster.uuid);
      Map<String, String> sourceGflagsHashMap =
          GFlagsUtil.getBaseGFlags(
              serverType, sourceCluster, getUniverse().getUniverseDetails().clusters);

      Map<String, String> targetGflagsHashMap =
          GFlagsUtil.getBaseGFlags(serverType, incomingCluster, incomingClusters);
      if (!sourceGflagsHashMap.equals(targetGflagsHashMap)) {
        // found a different flag, modified true;
        return true;
      }
    }
    // Did not find anything that was different, return false no flags modified.
    return false;
  }

  @Override
  public void run() {
    runUpgrade(
        () -> {
          // TODO: support specific gflags
          Cluster cluster = getUniverse().getUniverseDetails().getPrimaryCluster();
          UserIntent userIntent = cluster.userIntent;
          Universe universe = getUniverse();
          // Verify the request params and fail if invalid
          taskParams().verifyParams(universe);
          if (CommonUtils.isAutoFlagSupported(cluster.userIntent.ybSoftwareVersion)) {
            // Verify auto flags compatibility.
            taskParams()
                .checkXClusterAutoFlags(universe, gFlagsValidation, xClusterUniverseService);
          }
          // Update the list of parameter key/values in the universe with the new ones.
          updateGFlagsPersistTasks(
                  cluster,
                  taskParams().masterGFlags,
                  taskParams().tserverGFlags,
                  getPrimaryClusterSpecificGFlags())
              .setSubTaskGroupType(getTaskSubGroupType());

          boolean updateMaster = areGflagsModified(ServerType.MASTER);
          boolean updateTserver = areGflagsModified(ServerType.TSERVER);
          log.info("Update Master {} ; Update Tserver{} ", updateMaster, updateTserver);

          createUpgradeTask(
              getUniverse(),
              userIntent.ybSoftwareVersion,
              updateMaster,
              updateTserver,
              universe.isYbcEnabled(),
              universe.getUniverseDetails().getYbcSoftwareVersion());
        });
  }
}
