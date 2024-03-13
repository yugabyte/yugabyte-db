// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.forms.ProxyConfigUpdateParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntentOverrides;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.models.helpers.ProxyConfig;
import java.util.Collections;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.inject.Inject;

public class UpdateProxyConfig extends UpgradeTaskBase {

  @Inject
  protected UpdateProxyConfig(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected ProxyConfigUpdateParams taskParams() {
    return (ProxyConfigUpdateParams) taskParams;
  }

  @Override
  public SubTaskGroupType getTaskSubGroupType() {
    return SubTaskGroupType.UpdateProxyConfig;
  }

  @Override
  public NodeState getNodeState() {
    return NodeState.Live;
  }

  @Override
  public void validateParams(boolean isFirstTry) {
    super.validateParams(isFirstTry);
    taskParams().verifyParams(getUniverse(), isFirstTry);
  }

  @Override
  protected MastersAndTservers calculateNodesToBeRestarted() {
    return new MastersAndTservers(Collections.emptyList(), Collections.emptyList());
  }

  // Should add Node Precheck tasks later.

  @Override
  public void run() {
    // Keeping the Universe updater in the task itself for now. Will put it in a subtask later.
    UniverseUpdater universeUpdater =
        new UniverseUpdater() {
          @Override
          public void run(Universe universe) {
            UniverseDefinitionTaskParams uDParams = universe.getUniverseDetails();
            for (Cluster cluster : uDParams.clusters) {
              UserIntent curIntent = cluster.userIntent;
              UserIntent newIntent = taskParams().getClusterByUuid(cluster.uuid).userIntent;
              // Update default proxy config
              curIntent.setProxyConfig(newIntent.getProxyConfig());

              // Update Proxy overrides
              UserIntentOverrides newIntentOverrides = newIntent.getUserIntentOverrides();
              Map<UUID, ProxyConfig> proxyConfigOverrides =
                  newIntentOverrides != null ? newIntentOverrides.getAZProxyConfigMap() : null;
              curIntent.updateUserIntentOverrides(
                  intentOverrides -> {
                    if (proxyConfigOverrides != null) {
                      proxyConfigOverrides.entrySet().stream()
                          .forEach(
                              e -> {
                                intentOverrides.updateAZOverride(
                                    e.getKey(), azo -> azo.setProxyConfig(e.getValue()));
                              });
                    }
                    if (intentOverrides.getAzOverrides() != null) {
                      Set<UUID> keysToUnset =
                          intentOverrides.getAzOverrides().entrySet().stream()
                              .map(e -> e.getKey())
                              .filter(
                                  k ->
                                      (proxyConfigOverrides == null)
                                          || !proxyConfigOverrides.containsKey(k))
                              .collect(Collectors.toSet());
                      keysToUnset.stream()
                          .forEach(
                              k ->
                                  intentOverrides.updateAZOverride(
                                      k, azo -> azo.setProxyConfig(null)));
                    }
                  });
            }
            universe.setUniverseDetails(uDParams);
          }
        };
    saveUniverseDetails(universeUpdater);
  }
}
