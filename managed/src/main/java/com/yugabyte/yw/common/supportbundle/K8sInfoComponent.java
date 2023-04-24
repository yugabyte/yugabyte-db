/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * https://github.com/YugaByte/yugabyte-db/blob/master/licenses/
 * POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.common.supportbundle;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.KubernetesManager;
import com.yugabyte.yw.common.KubernetesManagerFactory;
import com.yugabyte.yw.common.KubernetesUtil;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.SupportBundleUtil;
import com.yugabyte.yw.common.SupportBundleUtil.KubernetesCluster;
import com.yugabyte.yw.common.SupportBundleUtil.KubernetesResourceType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.io.IOException;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Date;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Singleton
class K8sInfoComponent implements SupportBundleComponent {

  protected final Config config;
  private final SupportBundleUtil supportBundleUtil;
  private final KubernetesManagerFactory kubernetesManagerFactory;

  @Inject
  K8sInfoComponent(
      Config config,
      SupportBundleUtil supportBundleUtil,
      KubernetesManagerFactory kubernetesManagerFactory) {
    this.config = config;
    this.supportBundleUtil = supportBundleUtil;
    this.kubernetesManagerFactory = kubernetesManagerFactory;
  }

  /**
   * Run all the required kubectl commands on all the db namespaces for a given k8s cluster.
   *
   * @param universeCluster the universe level cluster (primary, read replica/s).
   * @param kubernetesCluster the k8s cluster to run the kubectl commands on.
   * @param nodePrefix the prefix of all node name in the universe.
   * @param kubernetesClusterDir the local cluster directory path to save the commands outputs to.
   * @throws IOException if not able to create / access the files properly.
   */
  public void runCommandsOnDbNamespaces(
      Cluster universeCluster,
      KubernetesCluster kubernetesCluster,
      String nodePrefix,
      String kubernetesClusterDir,
      boolean newNamingStyle)
      throws IOException {
    // Get pods, configmaps, services, statefulsets, persistant volume claims with full output.
    List<KubernetesResourceType> k8sResourcesWithOutput =
        Arrays.asList(
            KubernetesResourceType.PODS,
            KubernetesResourceType.CONFIGMAPS,
            KubernetesResourceType.SERVICES,
            KubernetesResourceType.STATEFULSETS,
            KubernetesResourceType.PERSISTENTVOLUMECLAIMS);
    for (Map.Entry<String, String> namespaceToAzName :
        kubernetesCluster.namespaceToAzNameMap.entrySet()) {
      // Create the az namespace directory.
      String dbNamespaceDirPath = kubernetesClusterDir + "/" + namespaceToAzName.getKey();
      supportBundleUtil.createDirectories(dbNamespaceDirPath);

      // Run the kubectl get resource commands to the respective output files.
      for (KubernetesResourceType kubernetesResourceType : k8sResourcesWithOutput) {
        String localFilePath =
            String.format(
                "%s/get_%s.%s",
                dbNamespaceDirPath,
                kubernetesResourceType.toString().toLowerCase(),
                SupportBundleUtil.kubectlOutputFormat);
        try {

          String resourceOutput =
              kubernetesManagerFactory
                  .getManager()
                  .getK8sResource(
                      kubernetesCluster.config,
                      kubernetesResourceType.toString().toLowerCase(),
                      namespaceToAzName.getKey(),
                      SupportBundleUtil.kubectlOutputFormat);
          supportBundleUtil.writeStringToFile(resourceOutput, localFilePath);
        } catch (Exception e) {
          supportBundleUtil.logK8sError(
              String.format(
                  "Error when getting kubectl resource type '%s' on namespace '%s : ",
                  kubernetesResourceType.toString().toLowerCase(), namespaceToAzName.getKey()),
              e,
              localFilePath);
        }
      }

      String localFilePath =
          dbNamespaceDirPath
              + String.format(
                  "/get_%s.txt", KubernetesResourceType.SECRETS.toString().toLowerCase());

      // Get just secrets names without specifying output format.
      try {

        String resourceOutput =
            kubernetesManagerFactory
                .getManager()
                .getK8sResource(
                    kubernetesCluster.config,
                    KubernetesResourceType.SECRETS.toString().toLowerCase(),
                    namespaceToAzName.getKey(),
                    null);
        supportBundleUtil.writeStringToFile(resourceOutput, localFilePath);
      } catch (Exception e) {
        supportBundleUtil.logK8sError(
            String.format(
                "Error when getting kubectl resource type '%s' on namespace '%s : ",
                KubernetesResourceType.SECRETS.toString().toLowerCase(),
                namespaceToAzName.getKey()),
            e,
            localFilePath);
      }

      // Get all events with filtered output.
      try {
        localFilePath = dbNamespaceDirPath + "/get_events.txt";
        kubernetesManagerFactory
            .getManager()
            .getEvents(kubernetesCluster.config, namespaceToAzName.getKey(), localFilePath);
      } catch (Exception e) {
        supportBundleUtil.logK8sError(
            String.format(
                "Error when getting kubectl resource type '%s' on namespace '%s : ",
                KubernetesResourceType.EVENTS.toString().toLowerCase(), namespaceToAzName.getKey()),
            e,
            localFilePath);
      }

      // Get helm values
      Provider provider =
          Provider.getOrBadRequest(UUID.fromString(universeCluster.userIntent.provider));
      boolean isMultiAz = PlacementInfoUtil.isMultiAZ(provider);
      boolean isReadOnlyUniverseCluster = universeCluster.clusterType == ClusterType.ASYNC;
      String helmReleaseName =
          KubernetesUtil.getHelmReleaseName(
              isMultiAz,
              nodePrefix,
              universeCluster.userIntent.universeName,
              namespaceToAzName.getValue(),
              isReadOnlyUniverseCluster,
              newNamingStyle);
      try {
        localFilePath =
            String.format(
                "%s/get_helm_values.%s", dbNamespaceDirPath, SupportBundleUtil.kubectlOutputFormat);
        String resourceOutput =
            kubernetesManagerFactory
                .getManager()
                .getHelmValues(
                    kubernetesCluster.config,
                    namespaceToAzName.getKey(),
                    helmReleaseName,
                    SupportBundleUtil.kubectlOutputFormat);
        supportBundleUtil.writeStringToFile(resourceOutput, localFilePath);
      } catch (Exception e) {
        supportBundleUtil.logK8sError(
            String.format(
                "Error when getting helm values on namespace '%s' : ", namespaceToAzName.getKey()),
            e,
            localFilePath);
      }
      log.debug("Finished running commands on the db namespace: " + namespaceToAzName.getKey());
    }
  }

  /**
   * Run all the required kubectl commands on the platform namespace in the k8s cluster.
   *
   * @param platformNamespace the namespace of the platform to run commands.
   * @param destDir the local directory path to save the commands outputs to.
   * @throws IOException if not able to create / access the files properly.
   */
  public void runCommandsOnPlatformNamespace(String platformNamespace, String destDir)
      throws IOException {
    // Create the platform namespace directory
    String platformNamespaceDirPath = destDir + "/" + "platform_namespace_" + platformNamespace;
    supportBundleUtil.createDirectories(platformNamespaceDirPath);

    // Get pods, events, configmaps, services with full output to the platform namespace directory
    List<KubernetesResourceType> k8sResourcesWithOutput =
        Arrays.asList(
            KubernetesResourceType.PODS,
            KubernetesResourceType.EVENTS,
            KubernetesResourceType.CONFIGMAPS,
            KubernetesResourceType.SERVICES);
    for (KubernetesResourceType kubernetesResourceType : k8sResourcesWithOutput) {
      String localFilePath =
          String.format(
              "%s/get_%s.%s",
              platformNamespaceDirPath,
              kubernetesResourceType.toString().toLowerCase(),
              SupportBundleUtil.kubectlOutputFormat);
      try {

        String resourceOutput =
            kubernetesManagerFactory
                .getManager()
                .getK8sResource(
                    null,
                    kubernetesResourceType.toString().toLowerCase(),
                    platformNamespace,
                    SupportBundleUtil.kubectlOutputFormat);
        supportBundleUtil.writeStringToFile(resourceOutput, localFilePath);
      } catch (Exception e) {
        supportBundleUtil.logK8sError(
            String.format(
                "Error when getting kubectl resource type '%s' on platform namespace '%s : ",
                kubernetesResourceType.toString().toLowerCase(), platformNamespace),
            e,
            localFilePath);
      }
    }
    log.debug("Finished running commands on the default platform namespace.");
  }

  /**
   * Reorganizes all the namespaces according to the cluster name (aka current-context). Algorithm:
   * From the map of all zones, get the current-context from each config. The current-context is
   * used as an identifier for the kubernetes cluster like a name. If the current-context is never
   * seen before, We make a new cluster object and store it. If the current-context matches any
   * previously seen cluster, then we just add the namespace (derived from the az) to that cluster
   * object.
   *
   * @param azToConfig mapping of all {zone : config} across the universe cluster.
   * @param isMultiAz if the universe cluster has multiple AZs.
   * @param nodePrefix the prefix of all node name in the universe.
   * @param universeDetails the UniverseDefinitionTaskParams object for that universe.
   * @param isReadOnlyUniverseCluster if the universe cluster is a read replica.
   * @return the list of all k8s clusters reaarranged with the cluster name and all its namespaces.
   */
  public List<KubernetesCluster> dedupKubernetesClusters(
      Map<UUID, Map<String, String>> azToConfig,
      boolean isMultiAz,
      String nodePrefix,
      UniverseDefinitionTaskParams universeDetails,
      boolean isReadOnlyUniverseCluster) {
    List<KubernetesCluster> kubernetesClusters = new ArrayList<KubernetesCluster>();
    for (Map.Entry<UUID, Map<String, String>> entry : azToConfig.entrySet()) {
      UUID azUuid = entry.getKey();
      Map<String, String> azConfig = entry.getValue();
      String azName = AvailabilityZone.getOrBadRequest(azUuid).getName();
      String kubernetesClusterName =
          kubernetesManagerFactory.getManager().getCurrentContext(azConfig);
      String namespace =
          KubernetesUtil.getKubernetesNamespace(
              isMultiAz,
              nodePrefix,
              azName,
              azConfig,
              universeDetails.useNewHelmNamingStyle,
              isReadOnlyUniverseCluster);
      if (!KubernetesCluster.listContainsClusterName(kubernetesClusters, kubernetesClusterName)) {
        Map<String, String> namespaceToAzNameMap = new HashMap<String, String>();
        namespaceToAzNameMap.put(namespace, azName);
        KubernetesCluster kubernetesCluster =
            new KubernetesCluster(kubernetesClusterName, azConfig, namespaceToAzNameMap);
        kubernetesClusters.add(kubernetesCluster);
      } else {
        KubernetesCluster.addNamespaceToKubernetesClusterInList(
            kubernetesClusters, kubernetesClusterName, namespace, azName);
      }
    }
    return kubernetesClusters;
  }

  @Override
  public void downloadComponent(
      Customer customer, Universe universe, Path bundlePath, NodeDetails node) throws Exception {
    try {
      log.debug("Starting downloadComponent() on K8sInfoComponent");

      KubernetesManager kubernetesManager = kubernetesManagerFactory.getManager();

      // Create the component directory in the support bundle.
      String destDir = bundlePath.toString() + "/" + "k8s_info";
      supportBundleUtil.createDirectories(destDir);

      // Get all clusters in the universe (primary, read replicas).
      UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
      List<Cluster> universeClusters = universeDetails.clusters;
      String nodePrefix = universe.getUniverseDetails().nodePrefix;
      String platformNamespace = kubernetesManager.getPlatformNamespace();

      // For each universe cluster,
      //     Get all the kubernetes clusters associated with the AZs from the universe provider.
      //     Run the kubectl commands and get the output to a file.
      for (Cluster universeCluster : universeClusters) {
        Map<UUID, Map<String, String>> azToConfig =
            KubernetesUtil.getConfigPerAZ(universeCluster.placementInfo);
        Provider provider =
            Provider.getOrBadRequest(UUID.fromString(universeCluster.userIntent.provider));
        boolean isMultiAz = PlacementInfoUtil.isMultiAZ(provider);
        boolean isReadOnlyUniverseCluster = universeCluster.clusterType == ClusterType.ASYNC;

        // Reorganize the k8s clusters with the all namespaces for each k8s cluster.
        List<KubernetesCluster> kubernetesClusters =
            dedupKubernetesClusters(
                azToConfig, isMultiAz, nodePrefix, universeDetails, isReadOnlyUniverseCluster);

        log.debug(
            "Rearranged all k8s clusters from universe cluster UUID '{}' into: {}",
            universeCluster.uuid,
            kubernetesClusters);

        // Get the kubectl command outputs for the platform namespace
        runCommandsOnPlatformNamespace(platformNamespace, destDir);

        // For each kubernetes cluster, get the kubectl command outputs on all the db namespaces in
        // that cluster
        for (KubernetesCluster kubernetesCluster : kubernetesClusters) {
          String kubernetesClusterDir = destDir + "/" + kubernetesCluster.clusterName;
          supportBundleUtil.createDirectories(kubernetesClusterDir);

          // Get the kubectl version output to a file
          String localFilePath =
              String.format(
                  "%s/kubectl_version.%s",
                  kubernetesClusterDir, SupportBundleUtil.kubectlOutputFormat);
          try {

            String resourceOutput =
                kubernetesManager.getK8sVersion(
                    kubernetesCluster.config, SupportBundleUtil.kubectlOutputFormat);
            supportBundleUtil.writeStringToFile(resourceOutput, localFilePath);
          } catch (Exception e) {
            supportBundleUtil.logK8sError(
                String.format(
                    "Error when getting kubectl version on universe (%s, %s) : ",
                    universe.getUniverseUUID().toString(), universe.getName()),
                e,
                localFilePath);
          }

          // Get the service account permissions on the cluster (with the service account name from
          // the provider config) to a file.
          String serviceAccountName =
              supportBundleUtil.getServiceAccountName(
                  provider, kubernetesManager, kubernetesCluster.config);
          String serviceAccountDir = kubernetesClusterDir + "/service_account_info";
          supportBundleUtil.createDirectories(serviceAccountDir);

          supportBundleUtil.getServiceAccountPermissionsToFile(
              kubernetesManager,
              kubernetesCluster.config,
              serviceAccountName,
              serviceAccountDir,
              universe.getUniverseUUID(),
              universe.getName());
          log.debug("Finished getting service account permissions.");

          runCommandsOnDbNamespaces(
              universeCluster,
              kubernetesCluster,
              nodePrefix,
              kubernetesClusterDir,
              universe.getUniverseDetails().useNewHelmNamingStyle);

          // Get the storage class info for that cluster
          Set<String> allStorageClassNames =
              supportBundleUtil.getAllStorageClassNames(
                  universe.getName(),
                  kubernetesManager,
                  kubernetesCluster,
                  isMultiAz,
                  nodePrefix,
                  isReadOnlyUniverseCluster,
                  universe.getUniverseDetails().useNewHelmNamingStyle);
          for (String storageClassName : allStorageClassNames) {
            String storageClassFilePath =
                String.format(
                    "%s/storageclass_%s.%s",
                    kubernetesClusterDir, storageClassName, SupportBundleUtil.kubectlOutputFormat);

            localFilePath = storageClassFilePath;
            try {
              String resourceOutput =
                  kubernetesManager.getStorageClass(
                      kubernetesCluster.config,
                      storageClassName,
                      null,
                      SupportBundleUtil.kubectlOutputFormat);
              supportBundleUtil.writeStringToFile(resourceOutput, localFilePath);
            } catch (Exception e) {
              supportBundleUtil.logK8sError(
                  String.format(
                      "Error when getting storageclass info for "
                          + "storageclass '%s' on universe (%s, %s) : ",
                      storageClassName, universe.getUniverseUUID().toString(), universe.getName()),
                  e,
                  localFilePath);
            }
          }
        }
      }
      log.debug("Finished downloadComponent() on K8sInfoComponent");
    } catch (Exception e) {
      log.error(
          String.format(
              "Error when downloading K8sInfoComponent on universe (%s, %s) : ",
              universe.getUniverseUUID().toString(), universe.getName()),
          e);
    }
  }

  @Override
  public void downloadComponentBetweenDates(
      Customer customer,
      Universe universe,
      Path bundlePath,
      Date startDate,
      Date endDate,
      NodeDetails node)
      throws Exception {
    this.downloadComponent(customer, universe, bundlePath, node);
  }
}
