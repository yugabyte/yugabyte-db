/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/
 *     POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.KubernetesTaskBase.KubernetesPlacement;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.KubernetesManager;
import com.yugabyte.yw.common.KubernetesManagerFactory;
import com.yugabyte.yw.common.KubernetesUtil;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import java.io.IOException;
import java.util.HashMap;
import java.util.Map;
import java.util.Map.Entry;
import java.util.Set;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class HandleKubernetesNamespacedServices extends UniverseTaskBase {

  private final KubernetesManagerFactory kubernetesManagerFactory;

  @Inject
  protected HandleKubernetesNamespacedServices(
      BaseTaskDependencies baseTaskDependencies,
      KubernetesManagerFactory kubernetesManagerFactory) {
    super(baseTaskDependencies);
    this.kubernetesManagerFactory = kubernetesManagerFactory;
  }

  // Params to handle namespace scope services
  public static class Params extends UniverseTaskParams {
    public boolean handleOwnershipChanges = false;
    public boolean deleteReadReplica = false;
    public UniverseDefinitionTaskParams universeParams = null;
  }

  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    try {
      KubernetesManager k8sManager = kubernetesManagerFactory.getManager();
      Universe universe = getUniverse();
      if (!KubernetesUtil.shouldConfigureNamespacedService(
          universe.getUniverseDetails(), universe.getConfig())) {
        log.info(
            "Skipping 'HandleKubernetesNamespacedServices' task since Universe configuration does"
                + " not support Namespaced services");
        return;
      }

      // Handle ownership changes
      if (taskParams().handleOwnershipChanges) {
        Map<UUID, UUID> servicesChangeOwnership = new HashMap<>();
        for (Cluster cluster : universe.getUniverseDetails().clusters) {
          if (cluster.userIntent.providerType != CloudType.kubernetes) {
            continue;
          }
          try {
            servicesChangeOwnership =
                KubernetesUtil.getNSScopeServicesChangeOwnership(
                    taskParams().universeParams,
                    universe.getUniverseDetails(),
                    universe.getConfig(),
                    cluster.clusterType);
          } catch (IOException e) {
            throw new RuntimeException("Error while parsing kubernetes overrides", e.getCause());
          }
          PlacementInfo pi = cluster.placementInfo;
          boolean isReadOnlyCluster = cluster.clusterType == ClusterType.ASYNC;
          KubernetesPlacement placement = new KubernetesPlacement(pi, isReadOnlyCluster);
          Provider provider =
              Provider.getOrBadRequest(UUID.fromString(cluster.userIntent.provider));
          boolean isMultiAZ = PlacementInfoUtil.isMultiAZ(provider);
          for (Entry<UUID, Map<String, String>> entry : placement.configs.entrySet()) {
            UUID azUUID = entry.getKey();
            if (!servicesChangeOwnership.containsKey(azUUID)) {
              continue;
            }
            String azName = AvailabilityZone.get(azUUID).getCode();
            Map<String, String> config = entry.getValue();
            String namespace =
                KubernetesUtil.getKubernetesNamespace(
                    isMultiAZ,
                    universe.getUniverseDetails().nodePrefix,
                    azName,
                    config,
                    universe.getUniverseDetails().useNewHelmNamingStyle,
                    isReadOnlyCluster);
            UUID newOwnerAzUUID = servicesChangeOwnership.get(azUUID);
            String newOwnerAzName = AvailabilityZone.get(newOwnerAzUUID).getCode();
            String newOwnerReleaseName =
                KubernetesUtil.getHelmReleaseName(
                    isMultiAZ,
                    universe.getUniverseDetails().nodePrefix,
                    universe.getName(),
                    newOwnerAzName,
                    isReadOnlyCluster,
                    universe.getUniverseDetails().useNewHelmNamingStyle);
            k8sManager.updateNamespacedServiceOwnership(
                config, namespace, universe.getName(), newOwnerReleaseName);
          }
        }
      }

      // Handle services to remove
      Map<UUID, Set<String>> servicesToRemove = new HashMap<>();
      for (Cluster cluster : universe.getUniverseDetails().clusters) {
        if (cluster.userIntent.providerType != CloudType.kubernetes) {
          continue;
        }
        boolean isReadOnlyCluster = cluster.clusterType == ClusterType.ASYNC;
        if (taskParams().deleteReadReplica && !isReadOnlyCluster) {
          // Skip primary cluster for read-replica cluster delete case
          continue;
        }
        try {
          servicesToRemove =
              KubernetesUtil.getExtraNSScopeServicesToRemove(
                  taskParams().universeParams,
                  universe.getUniverseDetails(),
                  universe.getConfig(),
                  cluster.clusterType,
                  taskParams().deleteReadReplica);
        } catch (IOException e) {
          throw new RuntimeException("Error while parsing kubernetes overrides", e.getCause());
        }
        PlacementInfo pi = cluster.placementInfo;
        KubernetesPlacement placement = new KubernetesPlacement(pi, isReadOnlyCluster);
        Provider provider = Provider.getOrBadRequest(UUID.fromString(cluster.userIntent.provider));
        boolean isMultiAZ = PlacementInfoUtil.isMultiAZ(provider);
        for (Entry<UUID, Map<String, String>> entry : placement.configs.entrySet()) {
          UUID azUUID = entry.getKey();
          if (!servicesToRemove.containsKey(azUUID)) {
            continue;
          }
          String azName = AvailabilityZone.get(azUUID).getCode();
          Map<String, String> config = entry.getValue();
          String namespace =
              KubernetesUtil.getKubernetesNamespace(
                  isMultiAZ,
                  universe.getUniverseDetails().nodePrefix,
                  azName,
                  config,
                  universe.getUniverseDetails().useNewHelmNamingStyle,
                  isReadOnlyCluster);
          k8sManager.deleteNamespacedService(
              config, namespace, universe.getName(), servicesToRemove.get(azUUID));
        }
      }
    } catch (Exception e) {
      String msg = getName() + " failed with exception " + e.getMessage();
      log.error(msg, e);
      throw new RuntimeException(msg, e);
    }
  }
}
