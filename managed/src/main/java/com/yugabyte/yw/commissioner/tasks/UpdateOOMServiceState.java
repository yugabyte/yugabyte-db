// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.CustomerConfKeys;
import com.yugabyte.yw.common.config.ProviderConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.AdditionalServicesStateData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import java.util.HashSet;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.Data;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
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
          Customer customer = Customer.get(universe.getCustomerId());
          EarlyoomEnablementState enablementState =
              getEarlyoomEnablementState(confGetter, universe.getUniverseDetails(), customer);
          additionalServicesStateData.setEarlyoomConfig(enablementState.getConfig());
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

  @Data
  public static class EarlyoomEnablementState {
    private boolean installationPossible;
    private boolean enableByDefault;
    private boolean enableOnUpgrade;
    private AdditionalServicesStateData.EarlyoomConfig config =
        new AdditionalServicesStateData.EarlyoomConfig();
  }

  /**
   * Resolves earlyoom enablement for a universe from customer and provider runtime configs.
   *
   * <p>Installation is not possible when the customer earlyoom feature flag is off, or when any
   * cluster provider is Kubernetes or manually provisioned on-prem. Otherwise {@code
   * installationPossible} is set.
   *
   * <p>{@code enableByDefault} is set only when every provider that configures {@code
   * enableEarlyoomByDefaultForProvider} agrees on {@code true}. {@code enableOnUpgrade} is set only
   * when {@code enableByDefault} is true and every provider that configures {@code
   * enableEarlyoomOnOSUpgrade} agrees on {@code true}. A single shared earlyoom config is taken
   * from provider {@code earlyoomDefaultArgs} when all providers agree on the same settings.
   *
   * @param confGetter runtime config getter used for customer and provider scopes
   * @param taskParams universe definition whose clusters determine the providers to consult
   * @param customer customer whose earlyoom feature flag is checked
   * @return aggregated earlyoom enablement state for the universe
   */
  public static EarlyoomEnablementState getEarlyoomEnablementState(
      RuntimeConfGetter confGetter, UniverseDefinitionTaskParams taskParams, Customer customer) {
    EarlyoomEnablementState state = new EarlyoomEnablementState();
    boolean enableEarlyoomFeature =
        confGetter.getConfForScope(customer, CustomerConfKeys.enableEarlyoomFeature);
    if (!enableEarlyoomFeature) {
      return state; // Disabled.
    }
    Set<UUID> providerUUIDs = new HashSet<>();
    for (UniverseDefinitionTaskParams.Cluster cluster : taskParams.clusters) {
      providerUUIDs.addAll(cluster.userIntent.getAllProviderUUIDs());
    }
    Set<Boolean> enabledByDefault = new HashSet<>();
    Set<Boolean> enableOnUpgrade = new HashSet<>();
    Set<AdditionalServicesStateData.EarlyoomConfig> configs = new HashSet<>();
    for (UUID providerUUID : providerUUIDs) {
      Provider provider = Provider.getOrBadRequest(providerUUID);
      if (provider.getCloudCode() == Common.CloudType.kubernetes || provider.isManualOnprem()) {
        log.debug(
            "Universe contains provider {}, installation is not possible", provider.getCloudCode());
        return state;
      }
      Boolean enableEarlyoomByDefault =
          confGetter.getConfForScope(provider, ProviderConfKeys.enableEarlyoomByDefaultForProvider);
      if (enableEarlyoomByDefault != null) {
        enabledByDefault.add(enableEarlyoomByDefault);
      }
      String earlyoomArgs =
          confGetter.getConfForScope(provider, ProviderConfKeys.earlyoomDefaultArgs);
      if (StringUtils.isNoneBlank(earlyoomArgs)) {
        configs.add(AdditionalServicesStateData.fromArgs(earlyoomArgs, true));
      }

      Boolean enableEarlyoomOnUpgrade =
          confGetter.getConfForScope(provider, ProviderConfKeys.enableEarlyoomOnOSUpgrade);
      if (enableEarlyoomOnUpgrade != null) {
        enableOnUpgrade.add(enableEarlyoomOnUpgrade);
      }
    }
    state.installationPossible = true;
    // For all the providers it should be enabled and should not have different args.
    if (enabledByDefault.size() == 1 && enabledByDefault.iterator().next() == Boolean.TRUE) {
      state.enableByDefault = true;
    }
    // Allowing to enable by OS upgrade only if it is enabled by default.
    if (state.enableByDefault) {
      if (enableOnUpgrade.size() == 1 && enableOnUpgrade.iterator().next() == Boolean.TRUE) {
        state.enableOnUpgrade = true;
      }
    }
    if (configs.size() > 1) {
      log.error(
          "Cannot pick single earlyoom settings between providers,"
              + " found different settings: "
              + configs);
    }
    if (configs.size() == 1) {
      state.config = configs.iterator().next();
    }

    return state;
  }
}
