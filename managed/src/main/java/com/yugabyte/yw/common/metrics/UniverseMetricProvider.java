/*
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.common.metrics;

import static com.yugabyte.yw.common.metrics.MetricService.STATUS_OK;
import static com.yugabyte.yw.common.metrics.MetricService.buildMetricTemplate;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableList;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.AccessKeyRotationUtil;
import com.yugabyte.yw.common.AccessManager;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.ImageBundleUtil;
import com.yugabyte.yw.common.KubernetesUtil;
import com.yugabyte.yw.common.ReleaseContainer;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.kms.util.KeyProvider;
import com.yugabyte.yw.common.kms.util.hashicorpvault.HashicorpVaultConfigParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.AccessKeyId;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.ImageBundle;
import com.yugabyte.yw.models.ImageBundle.ImageBundleType;
import com.yugabyte.yw.models.KmsConfig;
import com.yugabyte.yw.models.KmsHistory;
import com.yugabyte.yw.models.KmsHistoryId.TargetType;
import com.yugabyte.yw.models.Metric;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.filters.MetricFilter;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlatformMetrics;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.TimeUnit;
import java.util.function.Function;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import play.Environment;

@Singleton
@Slf4j
public class UniverseMetricProvider implements MetricsProvider {

  @Inject AccessKeyRotationUtil accessKeyRotationUtil;

  @Inject MetricService metricService;

  @Inject AccessManager accessManager;

  @Inject ImageBundleUtil imageBundleUtil;

  @Inject Environment environment;

  @Inject Config config;

  @Inject RuntimeConfGetter confGetter;

  @Inject ReleaseManager releaseManager;

  private static final List<PlatformMetrics> UNIVERSE_METRICS =
      ImmutableList.of(
          PlatformMetrics.UNIVERSE_EXISTS,
          PlatformMetrics.UNIVERSE_PAUSED,
          PlatformMetrics.UNIVERSE_UPDATE_IN_PROGRESS,
          PlatformMetrics.UNIVERSE_BACKUP_IN_PROGRESS,
          PlatformMetrics.UNIVERSE_NODE_FUNCTION,
          PlatformMetrics.UNIVERSE_NODE_PROCESS_STATUS,
          PlatformMetrics.UNIVERSE_ENCRYPTION_KEY_EXPIRY_DAY,
          PlatformMetrics.UNIVERSE_SSH_KEY_EXPIRY_DAY,
          PlatformMetrics.UNIVERSE_REPLICATION_FACTOR,
          PlatformMetrics.UNIVERSE_NODE_PROVISIONED_IOPS,
          PlatformMetrics.UNIVERSE_NODE_PROVISIONED_THROUGHPUT,
          PlatformMetrics.UNIVERSE_RELEASE_FILES_STATUS,
          PlatformMetrics.UNIVERSE_ACTIVE_TASK_CODE);

  @Override
  public List<MetricSaveGroup> getMetricGroups() throws Exception {
    List<MetricSaveGroup> metricSaveGroups = new ArrayList<>();
    Map<UUID, KmsHistory> activeEncryptionKeys =
        KmsHistory.getAllActiveHistory(TargetType.UNIVERSE_KEY).stream()
            .collect(Collectors.toMap(key -> key.getUuid().targetUuid, Function.identity()));
    Map<UUID, KmsConfig> kmsConfigMap =
        KmsConfig.listAllKMSConfigs().stream()
            .collect(Collectors.toMap(config -> config.getConfigUUID(), Function.identity()));
    Map<AccessKeyId, AccessKey> allAccessKeys = accessKeyRotationUtil.createAllAccessKeysMap();
    boolean vmOsPatchingEnabled = confGetter.getGlobalConf(GlobalConfKeys.enableVMOSPatching);
    Map<UUID, ImageBundle> imageBundleMap = null;
    if (vmOsPatchingEnabled) {
      imageBundleMap = imageBundleUtil.collectUniversesImageBundles();
    }

    String ybaVersion = ConfigHelper.getCurrentVersion(environment);
    Map<String, ReleaseContainer> releaseContainers =
        releaseManager.getAllReleaseContainersByVersion();
    for (Customer customer : Customer.getAll()) {
      Map<UUID, NodeInstance> nodeInstances =
          NodeInstance.listByCustomer(customer.getUuid()).stream()
              .collect(Collectors.toMap(NodeInstance::getNodeUuid, Function.identity()));
      Set<Universe> universes = Universe.getAllWithoutResources(customer);
      for (Universe universe : universes) {
        try {
          MetricSaveGroup.MetricSaveGroupBuilder universeGroup = MetricSaveGroup.builder();
          universeGroup.metric(
              createUniverseMetric(customer, universe, PlatformMetrics.UNIVERSE_EXISTS, STATUS_OK));
          universeGroup.metric(
              createUniverseMetric(
                  customer,
                  universe,
                  PlatformMetrics.UNIVERSE_PAUSED,
                  statusValue(universe.getUniverseDetails().universePaused)));
          universeGroup.metric(
              createUniverseMetric(
                  customer,
                  universe,
                  PlatformMetrics.UNIVERSE_UPDATE_IN_PROGRESS,
                  statusValue(universe.getUniverseDetails().updateInProgress)));
          TaskType taskType =
              universe.getUniverseDetails().updateInProgress
                  ? universe.getUniverseDetails().updatingTask
                  : null;
          String dbVersion =
              universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
          Metric activeTaskCodeMetric =
              createUniverseMetric(
                      customer,
                      universe,
                      PlatformMetrics.UNIVERSE_ACTIVE_TASK_CODE,
                      taskType != null ? taskType.getCode() : 0)
                  .setLabel(KnownAlertLabels.YBA_VERSION, ybaVersion)
                  .setLabel(KnownAlertLabels.DB_VERSION, dbVersion);
          if (activeTaskCodeMetric.getValue() > 0) {
            UUID taskUuid = universe.getUniverseDetails().updatingTaskUUID;
            activeTaskCodeMetric.setKeyLabel(KnownAlertLabels.TASK_UUID, taskUuid.toString());
          }
          universeGroup.metric(activeTaskCodeMetric);
          universeGroup.metric(
              createUniverseMetric(
                  customer,
                  universe,
                  PlatformMetrics.UNIVERSE_IS_SYSTEMD,
                  statusValue(
                      universe.getUniverseDetails().getPrimaryCluster().userIntent.useSystemd)));
          Double encryptionKeyExpiryDays =
              getEncryptionKeyExpiryDays(
                  activeEncryptionKeys.get(universe.getUniverseUUID()), kmsConfigMap);
          if (encryptionKeyExpiryDays != null) {
            KmsHistory kmsHistory = activeEncryptionKeys.get(universe.getUniverseUUID());
            KmsConfig kmsConfig = kmsConfigMap.get(kmsHistory.getConfigUuid());
            Metric encryptionKeyMetric =
                createUniverseMetric(
                    customer,
                    universe,
                    PlatformMetrics.UNIVERSE_ENCRYPTION_KEY_EXPIRY_DAY,
                    encryptionKeyExpiryDays);
            encryptionKeyMetric.setLabel(KnownAlertLabels.KMS_CONFIG_NAME, kmsConfig.getName());
            universeGroup.metric(encryptionKeyMetric);
          }
          universeGroup.metric(
              createUniverseMetric(
                  customer,
                  universe,
                  PlatformMetrics.UNIVERSE_REPLICATION_FACTOR,
                  universe.getUniverseDetails().getPrimaryCluster().userIntent.replicationFactor));
          universeGroup.metric(
              createUniverseMetric(
                  customer,
                  universe,
                  PlatformMetrics.UNIVERSE_CONNECTION_POOLING_STATUS,
                  statusValue(
                      universe
                          .getUniverseDetails()
                          .getPrimaryCluster()
                          .userIntent
                          .enableConnectionPooling)));
          if (!Util.isKubernetesBasedUniverse(universe)) {
            boolean validPermission =
                accessManager.checkAccessKeyPermissionsValidity(universe, allAccessKeys);
            universeGroup.metric(
                createUniverseMetric(
                    customer,
                    universe,
                    PlatformMetrics.UNIVERSE_PRIVATE_ACCESS_KEY_STATUS,
                    statusValue(validPermission)));
            Double sshKeyExpiryDays =
                accessKeyRotationUtil.getSSHKeyExpiryDays(universe, allAccessKeys);
            if (sshKeyExpiryDays != null) {
              universeGroup.metric(
                  createUniverseMetric(
                      customer,
                      universe,
                      PlatformMetrics.UNIVERSE_SSH_KEY_EXPIRY_DAY,
                      sshKeyExpiryDays));
            }
          }
          if (vmOsPatchingEnabled) {
            // Assumption both the primary & rr cluster uses the same provider.
            UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
            if (userIntent != null) {
              UUID imageBundleUUID = userIntent.imageBundleUUID;
              int universeOSUpdateRequired = 0;
              if (imageBundleMap != null && imageBundleMap.containsKey(imageBundleUUID)) {
                ImageBundle bundle = ImageBundle.get(imageBundleUUID);
                if (bundle != null
                    && bundle.getMetadata() != null
                    && bundle.getMetadata().getType() != null
                    && bundle.getMetadata().getType() == ImageBundleType.YBA_DEPRECATED) {
                  universeOSUpdateRequired = 1;
                }
              }

              if (universeOSUpdateRequired > 0) {
                universeGroup.metric(
                    createUniverseMetric(
                        customer,
                        universe,
                        PlatformMetrics.UNIVERSE_OS_UPDATE_REQUIRED,
                        universeOSUpdateRequired));
              }
            }
          }
          ReleaseContainer releaseContainer = releaseContainers.get(dbVersion);
          if (releaseContainer != null) {
            int universeFilepathMetric =
                UniverseTaskBase.validateLocalFilepath(universe, releaseContainer) ? 1 : 0;
            universeGroup.metric(
                createUniverseMetric(
                    customer,
                    universe,
                    PlatformMetrics.UNIVERSE_RELEASE_FILES_STATUS,
                    universeFilepathMetric));
          }
          if (universe.getUniverseDetails().nodeDetailsSet != null) {
            UserIntent primaryUserIntent =
                universe.getUniverseDetails().getPrimaryCluster().userIntent;
            boolean isK8SUniverse = CloudType.kubernetes.equals(primaryUserIntent.providerType);
            Map<UUID, Map<String, Object>> finalUniverseOverridesAZMap =
                isK8SUniverse
                    ? KubernetesUtil.getFinalOverrides(
                        universe.getUniverseDetails().getPrimaryCluster(),
                        primaryUserIntent.universeOverrides,
                        primaryUserIntent.azOverrides)
                    : null;
            for (NodeDetails nodeDetails : universe.getUniverseDetails().nodeDetailsSet) {
              if (nodeDetails.cloudInfo == null || nodeDetails.cloudInfo.private_ip == null) {
                // Node IP is missing - node is being created
                continue;
              }
              NodeInstance nodeInstance = nodeInstances.get(nodeDetails.nodeUuid);
              universeGroup.metric(
                  createNodeMetric(
                      customer,
                      universe,
                      PlatformMetrics.UNIVERSE_NODE_FUNCTION,
                      nodeDetails,
                      nodeInstance,
                      nodeDetails.masterHttpPort,
                      "master_export",
                      statusValue(nodeDetails.isMaster)));
              universeGroup.metric(
                  createNodeMetric(
                      customer,
                      universe,
                      PlatformMetrics.UNIVERSE_NODE_PROCESS_STATUS,
                      nodeDetails,
                      nodeInstance,
                      nodeDetails.masterHttpPort,
                      "master_export",
                      statusValue(nodeDetails.isMaster && nodeDetails.isActive())));
              universeGroup.metric(
                  createNodeMetric(
                      customer,
                      universe,
                      PlatformMetrics.UNIVERSE_NODE_FUNCTION,
                      nodeDetails,
                      nodeInstance,
                      nodeDetails.tserverHttpPort,
                      "tserver_export",
                      statusValue(nodeDetails.isTserver)));
              universeGroup.metric(
                  createNodeMetric(
                      customer,
                      universe,
                      PlatformMetrics.UNIVERSE_NODE_PROCESS_STATUS,
                      nodeDetails,
                      nodeInstance,
                      nodeDetails.tserverHttpPort,
                      "tserver_export",
                      statusValue(nodeDetails.isTserver && nodeDetails.isActive())));
              universeGroup.metric(
                  createNodeMetric(
                      customer,
                      universe,
                      PlatformMetrics.UNIVERSE_NODE_FUNCTION,
                      nodeDetails,
                      nodeInstance,
                      nodeDetails.ysqlServerHttpPort,
                      "ysql_export",
                      statusValue(nodeDetails.isYsqlServer)));
              universeGroup.metric(
                  createNodeMetric(
                      customer,
                      universe,
                      PlatformMetrics.UNIVERSE_NODE_FUNCTION,
                      nodeDetails,
                      nodeInstance,
                      nodeDetails.yqlServerHttpPort,
                      "cql_export",
                      statusValue(nodeDetails.isYqlServer)));
              universeGroup.metric(
                  createNodeMetric(
                      customer,
                      universe,
                      PlatformMetrics.UNIVERSE_NODE_FUNCTION,
                      nodeDetails,
                      nodeInstance,
                      nodeDetails.redisServerHttpPort,
                      "redis_export",
                      statusValue(nodeDetails.isRedisServer)));
              universeGroup.metric(
                  createNodeMetric(
                      customer,
                      universe,
                      PlatformMetrics.UNIVERSE_NODE_FUNCTION,
                      nodeDetails,
                      nodeInstance,
                      nodeDetails.nodeExporterPort,
                      "node_export",
                      statusValue(!isK8SUniverse)));
              universeGroup.metric(
                  createNodeMetric(
                      customer,
                      universe,
                      PlatformMetrics.UNIVERSE_NODE_PROCESS_STATUS,
                      nodeDetails,
                      nodeInstance,
                      nodeDetails.nodeExporterPort,
                      "node_export",
                      statusValue(!isK8SUniverse && nodeDetails.isActive())));
              universeGroup.metric(
                  createNodeMetric(
                      customer,
                      universe,
                      PlatformMetrics.UNIVERSE_NODE_CRON_STATUS,
                      nodeDetails,
                      nodeInstance,
                      nodeDetails.nodeExporterPort,
                      "node_export",
                      statusValue(nodeDetails.cronsActive)));
              // Add provisioned disk iops and throughput metrics.
              if (nodeDetails.placementUuid != null) {
                UniverseDefinitionTaskParams.Cluster cluster =
                    universe.getCluster(nodeDetails.placementUuid);
                if (cluster != null && cluster.userIntent != null) {
                  DeviceInfo deviceInfo = cluster.userIntent.getDeviceInfoForNode(nodeDetails);
                  Integer iops = deviceInfo.diskIops;
                  Integer throughput = deviceInfo.throughput;
                  if (iops != null) {
                    universeGroup.metric(
                        createNodeMetric(
                            customer,
                            universe,
                            PlatformMetrics.UNIVERSE_NODE_PROVISIONED_IOPS,
                            nodeDetails,
                            nodeInstance,
                            nodeDetails.nodeExporterPort,
                            "node_export",
                            iops));
                  }
                  if (throughput != null) {
                    universeGroup.metric(
                        createNodeMetric(
                            customer,
                            universe,
                            PlatformMetrics.UNIVERSE_NODE_PROVISIONED_THROUGHPUT,
                            nodeDetails,
                            nodeInstance,
                            nodeDetails.nodeExporterPort,
                            "node_export",
                            throughput));
                  }
                  if (isK8SUniverse) {
                    double cpuCoreCount =
                        KubernetesUtil.getCoreCountForUniverseForServer(
                            cluster.userIntent, finalUniverseOverridesAZMap, nodeDetails);
                    universeGroup.metric(
                        createContainerMetric(
                            customer,
                            universe,
                            PlatformMetrics.CONTAINER_RESOURCE_REQUESTS_CPU_CORES,
                            nodeDetails,
                            cpuCoreCount));
                  }
                }
              }
            }
          }
          universeGroup.cleanMetricFilter(
              MetricFilter.builder()
                  .metricNames(UNIVERSE_METRICS)
                  .sourceUuid(universe.getUniverseUUID())
                  .build());
          metricSaveGroups.add(universeGroup.build());
          metricService.setOkStatusMetric(
              buildMetricTemplate(PlatformMetrics.UNIVERSE_METRIC_COLLECTION_STATUS, universe));
        } catch (Exception e) {
          log.warn(
              "Metric collection failed for universe {} with ",
              universe.getUniverseUUID().toString(),
              e);
          metricService.setFailureStatusMetric(
              buildMetricTemplate(PlatformMetrics.UNIVERSE_METRIC_COLLECTION_STATUS, universe));
        }
      }
    }
    return metricSaveGroups;
  }

  private Metric createUniverseMetric(
      Customer customer, Universe universe, PlatformMetrics metric, double value) {
    String nodePrefix = universe.getUniverseDetails().nodePrefix;
    return buildMetricTemplate(metric, customer, universe)
        .setLabel(KnownAlertLabels.NODE_PREFIX, nodePrefix)
        .setValue(value);
  }

  private Metric createNodeMetric(
      Customer customer,
      Universe universe,
      PlatformMetrics metric,
      NodeDetails nodeDetails,
      NodeInstance nodeInstance,
      int port,
      String exportType,
      double value) {
    String nodePrefix = universe.getUniverseDetails().nodePrefix;
    Metric template =
        buildMetricTemplate(metric, customer, universe)
            .setKeyLabel(KnownAlertLabels.NODE_PREFIX, nodePrefix)
            .setKeyLabel(KnownAlertLabels.INSTANCE, nodeDetails.cloudInfo.private_ip + ":" + port)
            .setLabel(KnownAlertLabels.EXPORT_TYPE, exportType)
            .setLabel(KnownAlertLabels.NODE_NAME, nodeDetails.nodeName)
            .setLabel(KnownAlertLabels.NODE_ADDRESS, nodeDetails.cloudInfo.private_ip)
            .setLabel(KnownAlertLabels.NODE_REGION, nodeDetails.cloudInfo.region)
            .setLabel(
                KnownAlertLabels.NODE_CLUSTER_TYPE,
                universe.getCluster(nodeDetails.placementUuid).clusterType.name())
            .setValue(value);
    if (nodeInstance != null && StringUtils.isNotEmpty(nodeInstance.getDetails().instanceName)) {
      template.setLabel(KnownAlertLabels.NODE_IDENTIFIER, nodeInstance.getDetails().instanceName);
    }
    return template;
  }

  // Create k8s node metric.
  private Metric createContainerMetric(
      Customer customer,
      Universe universe,
      PlatformMetrics metric,
      NodeDetails nodeDetails,
      double value) {
    String nodePrefix = universe.getUniverseDetails().nodePrefix;
    return buildMetricTemplate(metric, customer, universe)
        .setKeyLabel(KnownAlertLabels.NODE_PREFIX, nodePrefix)
        .setKeyLabel(KnownAlertLabels.INSTANCE, nodeDetails.cloudInfo.private_ip)
        .setKeyLabel(KnownAlertLabels.NAMESPACE, nodeDetails.getK8sNamespace())
        .setKeyLabel(KnownAlertLabels.POD_NAME, nodeDetails.getK8sPodName())
        .setLabel(KnownAlertLabels.NODE_NAME, nodeDetails.nodeName)
        .setLabel("az_name", nodeDetails.cloudInfo.az)
        .setKeyLabel(
            KnownAlertLabels.CONTAINER_NAME, nodeDetails.isTserver ? "yb-tserver" : "yb-master")
        .setValue(value);
  }

  private Double getEncryptionKeyExpiryDays(KmsHistory activeKey, Map<UUID, KmsConfig> configMap) {
    if (activeKey == null) {
      return null;
    }
    KmsConfig kmsConfig = configMap.get(activeKey.getConfigUuid());
    if (kmsConfig == null) {
      log.warn(
          "Active universe {} key config {} is missing",
          activeKey.getUuid().targetUuid,
          activeKey.getConfigUuid());
      return null;
    }
    if (kmsConfig.getKeyProvider() != KeyProvider.HASHICORP) {
      // For now only Hashicorp config expires.
      return null;
    }
    ObjectNode credentials = kmsConfig.getAuthConfig();
    JsonNode keyTtlNode = credentials.get(HashicorpVaultConfigParams.HC_VAULT_TTL);
    if (keyTtlNode == null || keyTtlNode.asLong() == 0) {
      return null;
    }
    JsonNode keyTtlExpiryNode = credentials.get(HashicorpVaultConfigParams.HC_VAULT_TTL_EXPIRY);
    if (keyTtlExpiryNode == null) {
      return null;
    }
    return (double)
        TimeUnit.MILLISECONDS.toDays(keyTtlExpiryNode.asLong() - System.currentTimeMillis());
  }

  @Override
  public String getName() {
    return "Universe metrics";
  }
}
