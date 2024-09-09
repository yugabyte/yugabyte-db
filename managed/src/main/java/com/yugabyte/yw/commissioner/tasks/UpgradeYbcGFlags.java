// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleConfigureServers;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.common.backuprestore.ybc.YbcManager;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeTaskSubType;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeTaskType;
import com.yugabyte.yw.forms.YbcGflagsTaskParams;
import com.yugabyte.yw.forms.ybc.YbcGflags;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class UpgradeYbcGFlags extends KubernetesTaskBase {

  private final ReleaseManager releaseManager;
  private final YbcManager ybcManager;

  @Override
  protected YbcGflagsTaskParams taskParams() {
    return (YbcGflagsTaskParams) taskParams;
  }

  @Inject
  protected UpgradeYbcGFlags(
      BaseTaskDependencies baseTaskDependencies,
      ReleaseManager releaseManager,
      YbcManager ybcManager) {
    super(baseTaskDependencies);
    this.releaseManager = releaseManager;
    this.ybcManager = ybcManager;
  }

  @Override
  public void run() {
    String errorString = null;
    try {
      Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
      UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();

      if (!universeDetails.isYbcInstalled()) {
        throw new RuntimeException(
            "Ybc is not installed on the universe: " + universe.getUniverseUUID());
      }

      lockUniverse(-1 /* expectedUniverseVersion */);
      Map<String, String> ybcGflagsMap = convertToMap(taskParams().ybcGflags);
      if (universeDetails
          .getPrimaryCluster()
          .userIntent
          .providerType
          .equals(Common.CloudType.kubernetes)) {
        Set<NodeDetails> nodeDetailSet =
            new HashSet<>(universe.getRunningTserversInPrimaryCluster());

        installYbcOnThePods(
            universe.getName(),
            nodeDetailSet,
            false,
            universeDetails.getYbcSoftwareVersion(),
            ybcGflagsMap);
        performYbcAction(nodeDetailSet, false, "stop");
        createWaitForYbcServerTask(nodeDetailSet)
            .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

        List<Cluster> readOnlyClusters = universeDetails.getReadOnlyClusters();
        if (!readOnlyClusters.isEmpty()) {
          nodeDetailSet = universeDetails.getTserverNodesInCluster(readOnlyClusters.get(0).uuid);
          installYbcOnThePods(
              universe.getName(),
              nodeDetailSet,
              true,
              universeDetails.getYbcSoftwareVersion(),
              ybcGflagsMap);
          performYbcAction(nodeDetailSet, true, "stop");
          createWaitForYbcServerTask(nodeDetailSet)
              .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
        }
      } else {
        for (NodeDetails node : universe.getNodes()) {
          AnsibleConfigureServers.Params params =
              ybcManager.getAnsibleConfigureYbcServerTaskParams(
                  universe,
                  node,
                  ybcGflagsMap,
                  UpgradeTaskType.YbcGFlags,
                  UpgradeTaskSubType.YbcGflagsUpdate);
          getAnsibleConfigureYbcServerTasks(params, universe)
              .setSubTaskGroupType(SubTaskGroupType.UpdatingYbcGFlags);
        }
        List<NodeDetails> nodeDetailsList = new ArrayList<>(universe.getNodes());
        createServerControlTasks(nodeDetailsList, ServerType.CONTROLLER, "stop")
            .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);

        createServerControlTasks(nodeDetailsList, ServerType.CONTROLLER, "start")
            .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);
        createWaitForYbcServerTask(nodeDetailsList)
            .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      }
      createUpdateYbcGFlagInTheUniverseDetailsTask(ybcGflagsMap)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      getRunnableTask().runSubTasks();
    } catch (IllegalAccessException e) {
      throw new RuntimeException(e);
    } catch (Throwable t) {
      errorString = t.getMessage();
      log.error("Error executing task {} with error='{}'.", getName(), t.getMessage(), t);
      throw t;
    } finally {
      unlockUniverseForUpdate(errorString);
    }
    log.info("Finished {} task.", getName());
  }

  private Map<String, String> convertToMap(YbcGflags ybcGflags) throws IllegalAccessException {
    Map<String, String> map = new HashMap<>();

    Class<?> clazz = ybcGflags.getClass();
    for (Field field : clazz.getDeclaredFields()) {
      if (field.getName().equalsIgnoreCase("ybcGflagsMetadata")) {
        continue;
      }
      field.setAccessible(true);
      String fieldName = field.getName();
      Object value = field.get(ybcGflags);
      log.info("Field: {}, {}", fieldName, value);

      if (value != null) {
        map.put(fieldName, value.toString());
      }
    }
    return map;
  }
}
