// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks.subtasks.xcluster;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.common.KubernetesManager;
import com.yugabyte.yw.common.KubernetesManagerFactory;
import com.yugabyte.yw.common.KubernetesUtil;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;

/**
 * In Kubernetes multi-cluster setups, it is possible that the target and source universes are in
 * different Kubernetes clusters. Such environments are usually connected using Multi-Cluster
 * Services (MCS) APIs. In order to access services from target in source and vise versa, you need
 * to expose the services. If a service from source cluster's namespace is exposed, the target
 * cluster needs to have this namespace present, otherwise exposed service won't work. For xCluster
 * we need two way connectivity, so we create namespaces of source universe in target universe's
 * providers, and vice versa. OpenShift is one such case where this is required. i.e. In OpenShift +
 * MCS enabled + k8s universes + xCluster, this is needed.
 */
@Slf4j
public class ReplicateNamespaces extends XClusterConfigTaskBase {

  private final KubernetesManagerFactory kubernetesManagerFactory;

  @Inject
  protected ReplicateNamespaces(
      BaseTaskDependencies baseTaskDependencies,
      KubernetesManagerFactory kubernetesManagerFactory,
      XClusterUniverseService xClusterUniverseService) {
    super(baseTaskDependencies, xClusterUniverseService);
    this.kubernetesManagerFactory = kubernetesManagerFactory;
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    XClusterConfig xClusterConfig = getXClusterConfigFromTaskParams();
    Universe sourceUniverse = Universe.getOrBadRequest(xClusterConfig.getSourceUniverseUUID());
    Universe targetUniverse = Universe.getOrBadRequest(xClusterConfig.getTargetUniverseUUID());
    try {
      Set<String> sourceNamespaces = KubernetesUtil.getUniverseNamespaces(sourceUniverse);
      log.debug("Namespaces from source universe: {}", sourceNamespaces);
      Set<String> targetNamespaces = KubernetesUtil.getUniverseNamespaces(targetUniverse);
      log.debug("Namespaces from target universe: {}", targetNamespaces);
      Set<String> targetNamespacesNotExistOnSourceUniverse =
          targetNamespaces.stream()
              .filter(namespace -> !sourceNamespaces.contains(namespace))
              .collect(Collectors.toSet());
      Set<String> sourceNamespacesNotExistOnTargetUniverse =
          sourceNamespaces.stream()
              .filter(namespace -> !targetNamespaces.contains(namespace))
              .collect(Collectors.toSet());
      if (!targetNamespacesNotExistOnSourceUniverse.isEmpty()) {
        createNamespaces(targetNamespacesNotExistOnSourceUniverse, sourceUniverse);
      }
      if (!sourceNamespacesNotExistOnTargetUniverse.isEmpty()) {
        createNamespaces(sourceNamespacesNotExistOnTargetUniverse, targetUniverse);
      }
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
    }

    log.info("Completed {}", getName());
  }

  private void createNamespaces(Set<String> namespaces, Universe universe) {
    log.info("Creating namespaces: {} on universe: {}", namespaces, universe);
    Set<UUID> visitedProviderUUIDS = new HashSet<>();
    KubernetesManager kubernetesManager = kubernetesManagerFactory.getManager();
    for (Cluster cluster : universe.getUniverseDetails().clusters) {
      UUID providerUUID = UUID.fromString(cluster.userIntent.provider);
      if (visitedProviderUUIDS.contains(providerUUID)) {
        continue;
      }
      visitedProviderUUIDS.add(providerUUID);
      Provider provider = Provider.getOrBadRequest(providerUUID);
      if (provider.getCloudCode().equals(Common.CloudType.kubernetes)) {
        Map<UUID, String> azKubeConfigMap = KubernetesUtil.getKubeConfigPerAZ(provider);
        List<Region> regions = Region.getByProvider(provider.getUuid());
        for (Region region : regions) {
          List<AvailabilityZone> zones = AvailabilityZone.getAZsForRegion(region.getUuid());
          for (AvailabilityZone zone : zones) {
            String kubeConfig = azKubeConfigMap.getOrDefault(zone.getUuid(), null);
            if (kubeConfig != null) {
              Map<String, String> config = new HashMap<>();
              config.put("KUBECONFIG", kubeConfig);
              for (String namespace : namespaces) {
                // TODO: Add annotations for created namespaces.
                boolean namespaceExists = kubernetesManager.namespaceExists(config, namespace);
                if (namespaceExists) {
                  log.debug(
                      "Namespace {} exists, kubeconfig: {}, skipping namespace creation",
                      namespace,
                      kubeConfig);
                } else {
                  log.debug("Creating namespace: {}, kubeconfig: {}", namespace, kubeConfig);
                  kubernetesManager.createNamespace(config, namespace);
                }
              }
            }
          }
        }
      }
    }
  }
}
