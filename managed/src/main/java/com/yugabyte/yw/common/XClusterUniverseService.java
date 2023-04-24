// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.gflags.GFlagsValidation;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.helpers.CommonUtils;
import java.io.IOException;
import java.util.HashSet;
import java.util.LinkedList;
import java.util.List;
import java.util.Queue;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import org.apache.commons.collections4.CollectionUtils;

@Singleton
public class XClusterUniverseService {

  private final GFlagsValidation gFlagsValidation;
  private final RuntimeConfGetter confGetter;

  @Inject
  public XClusterUniverseService(GFlagsValidation gFlagsValidation, RuntimeConfGetter confGetter) {
    this.gFlagsValidation = gFlagsValidation;
    this.confGetter = confGetter;
  }

  public Set<UUID> getActiveXClusterSourceAndTargetUniverseSet(UUID universeUUID) {
    return getActiveXClusterSourceAndTargetUniverseSet(
        universeUUID, new HashSet<>() /* excludeXClusterConfigSet */);
  }

  /**
   * Get the set of universes UUID which are connected to the input universe either as source or
   * target universe through a running xCluster config.
   *
   * @param universeUUID the universe on which search needs to be performed.
   * @param excludeXClusterConfigSet set of universe which will be ignored.
   * @return the set of universe uuid which are connected to the input universe.
   */
  public Set<UUID> getActiveXClusterSourceAndTargetUniverseSet(
      UUID universeUUID, Set<UUID> excludeXClusterConfigSet) {
    List<XClusterConfig> xClusterConfigs =
        XClusterConfig.getByUniverseUuid(universeUUID).stream()
            .filter(
                xClusterConfig ->
                    !xClusterConfig
                        .getStatus()
                        .equals(XClusterConfig.XClusterConfigStatusType.DeletedUniverse))
            .filter(xClusterConfig -> !excludeXClusterConfigSet.contains(xClusterConfig.getUuid()))
            .collect(Collectors.toList());
    return xClusterConfigs.stream()
        .map(
            config -> {
              if (config.getSourceUniverseUUID().equals(universeUUID)) {
                return config.getTargetUniverseUUID();
              } else {
                return config.getSourceUniverseUUID();
              }
            })
        .collect(Collectors.toSet());
  }

  public Set<Universe> getXClusterConnectedUniverses(Universe initialUniverse) {
    return getXClusterConnectedUniverses(
        initialUniverse, new HashSet<>() /* excludeXClusterConfigSet */);
  }

  /**
   * Returns the set of universes of xCluster connected universes.
   *
   * @param initialUniverse the initial point of the xCluster nexus.
   * @param excludeXClusterConfigSet set of universe which will be ignored.
   * @return universe set containing xCluster connected universes.
   */
  public Set<Universe> getXClusterConnectedUniverses(
      Universe initialUniverse, Set<UUID> excludeXClusterConfigSet) {
    Set<Universe> universeSet = new HashSet<>();
    Queue<Universe> universeQueue = new LinkedList<>();
    Set<UUID> visitedUniverse = new HashSet<>();
    universeQueue.add(initialUniverse);
    visitedUniverse.add(initialUniverse.getUniverseUUID());
    while (universeQueue.size() > 0) {
      Universe universe = universeQueue.remove();
      universeSet.add(universe);
      Set<UUID> xClusterUniverses =
          getActiveXClusterSourceAndTargetUniverseSet(
              universe.getUniverseUUID(), excludeXClusterConfigSet);
      if (!CollectionUtils.isEmpty(xClusterUniverses)) {
        for (UUID univUUID : xClusterUniverses) {
          if (!visitedUniverse.contains(univUUID)) {
            universeQueue.add(Universe.getOrBadRequest(univUUID));
            visitedUniverse.add(univUUID);
          }
        }
      }
    }
    return universeSet;
  }

  /**
   * Checks if we can perform promote auto flags on the provided universe set. All universes need to
   * be auto flags compatible and should be supporting same list of auto flags.
   *
   * @param universeSet
   * @param univUpgradeInProgress
   * @return true if auto flags can be promoted on all universes.
   * @throws IOException
   * @throws PlatformServiceException
   */
  public boolean canPromoteAutoFlags(Set<Universe> universeSet, Universe univUpgradeInProgress)
      throws IOException, PlatformServiceException {
    String univSoftwareVersion =
        univUpgradeInProgress.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
    GFlagsValidation.AutoFlagsPerServer masterAutoFlags =
        gFlagsValidation.extractAutoFlags(univSoftwareVersion, "yb-master");
    GFlagsValidation.AutoFlagsPerServer tserverAutoFlags =
        gFlagsValidation.extractAutoFlags(univSoftwareVersion, "yb-tserver");
    // Compare auto flags json for each universe.
    for (Universe univ : universeSet) {
      if (univ.getUniverseUUID().equals(univUpgradeInProgress.getUniverseUUID())) {
        continue;
      }
      if (!confGetter.getConfForScope(univ, UniverseConfKeys.promoteAutoFlag)) {
        return false;
      }
      String softwareVersion =
          univ.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
      if (!CommonUtils.isAutoFlagSupported(softwareVersion)) {
        return false;
      }
      GFlagsValidation.AutoFlagsPerServer univMasterAutoFlags =
          gFlagsValidation.extractAutoFlags(softwareVersion, "yb-master");
      GFlagsValidation.AutoFlagsPerServer univTServerAutoFlags =
          gFlagsValidation.extractAutoFlags(softwareVersion, "yb-tserver");
      if (!(compareAutoFlagPerServerListByName(
              masterAutoFlags.autoFlagDetails, univMasterAutoFlags.autoFlagDetails)
          && compareAutoFlagPerServerListByName(
              tserverAutoFlags.autoFlagDetails, univTServerAutoFlags.autoFlagDetails))) {
        return false;
      }
    }
    return true;
  }

  /**
   * Compare list of auto flags details on the basis of name irrespective of their order.
   *
   * @param x first auto flag details.
   * @param y second auto flag details.
   * @return true if both lists of auto flag details are same.
   */
  public boolean compareAutoFlagPerServerListByName(
      List<GFlagsValidation.AutoFlagDetails> x, List<GFlagsValidation.AutoFlagDetails> y) {
    List<String> xFlagsNameList = x.stream().map(flag -> flag.name).collect(Collectors.toList());
    List<String> yFlagsNameList = y.stream().map(flag -> flag.name).collect(Collectors.toList());
    return CommonUtils.isEqualIgnoringOrder(xFlagsNameList, yFlagsNameList);
  }

  /**
   * Fetches the multiple set of xCluster connected universe.
   *
   * @param universeSet
   * @return
   */
  public Set<Set<Universe>> getMultipleXClusterConnectedUniverseSet(
      Set<UUID> universeSet, Set<UUID> excludeXClusterConfigSet) {
    Set<Set<Universe>> multipleXClusterConnectedUniverseSet = new HashSet<>();
    Set<UUID> visitedUniverseSet = new HashSet<>();
    for (UUID universeUUID : universeSet) {
      if (visitedUniverseSet.contains(universeUUID)) {
        continue;
      }
      Universe universe = Universe.getOrBadRequest(universeUUID);
      Set<Universe> xClusterConnectedUniverses =
          getXClusterConnectedUniverses(universe, excludeXClusterConfigSet);
      multipleXClusterConnectedUniverseSet.add(xClusterConnectedUniverses);
      visitedUniverseSet.addAll(
          xClusterConnectedUniverses.stream()
              .map(Universe::getUniverseUUID)
              .collect(Collectors.toList()));
    }
    return multipleXClusterConnectedUniverseSet;
  }
}
