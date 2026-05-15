// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.CustomerConfKeys;
import com.yugabyte.yw.common.config.ProviderConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.AdditionalServicesStateData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import play.mvc.Http;

@Slf4j
public class UpdateOOMServiceState extends UniverseDefinitionTaskBase {

  @Inject
  protected UpdateOOMServiceState(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected void createPrecheckTasks(Universe universe) {
    Set<String> nodesWithoutNA =
        universe.getUniverseDetails().nodeDetailsSet.stream()
            .map(n -> new Pair<>(n, nodeUniverseManager.maybeUpgradeAndGetNodeAgent(universe, n)))
            .filter(p -> p.getSecond().isEmpty())
            .map(p -> p.getFirst().nodeName)
            .collect(Collectors.toSet());
    if (!nodesWithoutNA.isEmpty()) {
      throw new PlatformServiceException(
          Http.Status.BAD_REQUEST, "Found nodes that cannot be updated: " + nodesWithoutNA);
    }
  }

  @Override
  public void run() {
    log.info("Started {} task for univ uuid={}", getName(), taskParams().getUniverseUUID());
    Universe universe = getUniverse();
    try {
      // Lock the universe but don't freeze it because this task doesn't perform critical updates to
      // universe metadata.
      universe = lockUniverse(-1 /* expectedUniverseVersion */);
      AdditionalServicesStateData additionalServicesStateData =
          taskParams().additionalServicesStateData;
      if (additionalServicesStateData.getEarlyoomConfig() == null) {
        if (universe.getUniverseDetails().additionalServicesStateData != null
            && universe.getUniverseDetails().additionalServicesStateData.getEarlyoomConfig()
                != null) {
          additionalServicesStateData.setEarlyoomConfig(
              universe.getUniverseDetails().additionalServicesStateData.getEarlyoomConfig());
        } else {
          log.debug("No earlyoom config provided, using default settings");
          Provider provider =
              Provider.getOrBadRequest(
                  UUID.fromString(
                      universe.getUniverseDetails().getPrimaryCluster().userIntent.provider));
          String earlyoomArgs =
              confGetter.getConfForScope(provider, ProviderConfKeys.earlyoomDefaultArgs);
          additionalServicesStateData.setEarlyoomConfig(
              AdditionalServicesStateData.fromArgs(earlyoomArgs, true));
        }
      }

      createConfigureOOMServiceSubtasks(
          additionalServicesStateData, universe.getUniverseDetails().nodeDetailsSet);

      createUpdateUniverseFieldsTask(
          u -> {
            if (u.getUniverseDetails().additionalServicesStateData == null) {
              u.getUniverseDetails().additionalServicesStateData =
                  new AdditionalServicesStateData();
            }
            u.getUniverseDetails()
                .additionalServicesStateData
                .setEarlyoomConfig(additionalServicesStateData.getEarlyoomConfig());
            u.getUniverseDetails()
                .additionalServicesStateData
                .setEarlyoomEnabled(additionalServicesStateData.isEarlyoomEnabled());
          });

      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      getRunnableTask().runSubTasks();
    } catch (Exception e) {
      log.error("Task Errored out with: " + e);
      throw new RuntimeException(e);
    } finally {
      unlockUniverseForUpdate(universe.getUniverseUUID());
    }
  }

  public static boolean isEarlyoomInstallationPossible(
      RuntimeConfGetter confGetter, UniverseDefinitionTaskParams taskParams, Customer customer) {
    boolean enableEarlyoomFeature =
        confGetter.getConfForScope(customer, CustomerConfKeys.enableEarlyoomFeature);

    return enableEarlyoomFeature
        && !Util.isOnPremManualProvisioning(taskParams)
        && !Util.isKubernetesBasedUniverse(taskParams);
  }
}
