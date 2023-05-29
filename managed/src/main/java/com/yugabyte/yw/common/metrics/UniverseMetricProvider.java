/*
 * Copyright 2021 YugaByte, Inc. and Contributors
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
import com.yugabyte.yw.common.AccessKeyRotationUtil;
import com.yugabyte.yw.common.AccessManager;
import com.yugabyte.yw.common.KubernetesUtil;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.kms.util.KeyProvider;
import com.yugabyte.yw.common.kms.util.hashicorpvault.HashicorpVaultConfigParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.AccessKeyId;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.InstanceTypeKey;
import com.yugabyte.yw.models.KmsConfig;
import com.yugabyte.yw.models.KmsHistory;
import com.yugabyte.yw.models.KmsHistoryId.TargetType;
import com.yugabyte.yw.models.Metric;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.filters.MetricFilter;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlatformMetrics;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.TimeUnit;
import java.util.function.Function;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;

@Singleton
@Slf4j
public class UniverseMetricProvider implements MetricsProvider {

  @Inject AccessKeyRotationUtil accessKeyRotationUtil;

  @Inject MetricService metricService;

  @Inject AccessManager accessManager;

  @Inject Config config;

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
          PlatformMetrics.UNIVERSE_NODE_PROVISIONED_THROUGHPUT);

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

    Map<String, InstanceType> mapInstanceTypes = new HashMap<String, InstanceType>();
    for (Customer customer : Customer.getAll()) {
      /*
      To prevent excessive memory usage when dealing with multiple providers
      and a large instanceType table, we load only a small subset of instanceTypes
      with each iteration & clear the map during next iteration.
      */
      mapInstanceTypes.clear();
      // Get all providers for the customer
      List<Provider> providers = Provider.getAll(customer.getUuid());
      // Build instanceTypeMap.
      for (Provider provider : providers) {
        for (InstanceType instanceType : InstanceType.findByProvider(provider, config)) {
          mapInstanceTypes.put(instanceType.getIdKey().toString(), instanceType);
        }
      }
      for (Universe universe : Universe.getAllWithoutResources(customer)) {
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
          Double encryptionKeyExpiryDays =
              getEncryptionKeyExpiryDays(
                  activeEncryptionKeys.get(universe.getUniverseUUID()), kmsConfigMap);
          if (encryptionKeyExpiryDays != null) {
            universeGroup.metric(
                createUniverseMetric(
                    customer,
                    universe,
                    PlatformMetrics.UNIVERSE_ENCRYPTION_KEY_EXPIRY_DAY,
                    encryptionKeyExpiryDays));
          }
          universeGroup.metric(
              createUniverseMetric(
                  customer,
                  universe,
                  PlatformMetrics.UNIVERSE_REPLICATION_FACTOR,
                  universe.getUniverseDetails().getPrimaryCluster().userIntent.replicationFactor));
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

          if (universe.getUniverseDetails().nodeDetailsSet != null) {
            for (NodeDetails nodeDetails : universe.getUniverseDetails().nodeDetailsSet) {
              if (nodeDetails.cloudInfo == null || nodeDetails.cloudInfo.private_ip == null) {
                // Node IP is missing - node is being created
                continue;
              }
              String ipAddress = nodeDetails.cloudInfo.private_ip;
              universeGroup.metric(
                  createNodeMetric(
                      customer,
                      universe,
                      PlatformMetrics.UNIVERSE_NODE_FUNCTION,
                      ipAddress,
                      nodeDetails.masterHttpPort,
                      "master_export",
                      statusValue(nodeDetails.isMaster)));
              universeGroup.metric(
                  createNodeMetric(
                      customer,
                      universe,
                      PlatformMetrics.UNIVERSE_NODE_PROCESS_STATUS,
                      ipAddress,
                      nodeDetails.masterHttpPort,
                      "master_export",
                      statusValue(nodeDetails.isMaster && nodeDetails.isActive())));
              universeGroup.metric(
                  createNodeMetric(
                      customer,
                      universe,
                      PlatformMetrics.UNIVERSE_NODE_FUNCTION,
                      ipAddress,
                      nodeDetails.tserverHttpPort,
                      "tserver_export",
                      statusValue(nodeDetails.isTserver)));
              universeGroup.metric(
                  createNodeMetric(
                      customer,
                      universe,
                      PlatformMetrics.UNIVERSE_NODE_PROCESS_STATUS,
                      ipAddress,
                      nodeDetails.tserverHttpPort,
                      "tserver_export",
                      statusValue(nodeDetails.isTserver && nodeDetails.isActive())));
              universeGroup.metric(
                  createNodeMetric(
                      customer,
                      universe,
                      PlatformMetrics.UNIVERSE_NODE_FUNCTION,
                      ipAddress,
                      nodeDetails.ysqlServerHttpPort,
                      "ysql_export",
                      statusValue(nodeDetails.isYsqlServer)));
              universeGroup.metric(
                  createNodeMetric(
                      customer,
                      universe,
                      PlatformMetrics.UNIVERSE_NODE_FUNCTION,
                      ipAddress,
                      nodeDetails.yqlServerHttpPort,
                      "cql_export",
                      statusValue(nodeDetails.isYqlServer)));
              universeGroup.metric(
                  createNodeMetric(
                      customer,
                      universe,
                      PlatformMetrics.UNIVERSE_NODE_FUNCTION,
                      ipAddress,
                      nodeDetails.redisServerHttpPort,
                      "redis_export",
                      statusValue(nodeDetails.isRedisServer)));
              boolean isK8SUniverse =
                  CloudType.kubernetes.equals(universe.getNodeDeploymentMode(nodeDetails));
              universeGroup.metric(
                  createNodeMetric(
                      customer,
                      universe,
                      PlatformMetrics.UNIVERSE_NODE_FUNCTION,
                      ipAddress,
                      nodeDetails.nodeExporterPort,
                      "node_export",
                      statusValue(!isK8SUniverse)));
              universeGroup.metric(
                  createNodeMetric(
                      customer,
                      universe,
                      PlatformMetrics.UNIVERSE_NODE_PROCESS_STATUS,
                      ipAddress,
                      nodeDetails.nodeExporterPort,
                      "node_export",
                      statusValue(!isK8SUniverse && nodeDetails.isActive())));
              // Add provisioned disk iops and throughput metrics.
              if (nodeDetails.placementUuid != null) {
                UniverseDefinitionTaskParams.Cluster cluster =
                    universe.getCluster(nodeDetails.placementUuid);
                if (cluster != null && cluster.userIntent.deviceInfo != null) {
                  Integer iops = cluster.userIntent.deviceInfo.diskIops;
                  Integer throughput = cluster.userIntent.deviceInfo.throughput;
                  if (iops != null) {
                    universeGroup.metric(
                        createNodeMetric(
                            customer,
                            universe,
                            PlatformMetrics.UNIVERSE_NODE_PROVISIONED_IOPS,
                            nodeDetails.cloudInfo.private_ip,
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
                            nodeDetails.cloudInfo.private_ip,
                            nodeDetails.nodeExporterPort,
                            "node_export",
                            throughput));
                  }
                  if (isK8SUniverse) {
                    // Cluster cluster = universe.getCluster(nodeDetails.placementUuid);
                    InstanceType instanceType =
                        mapInstanceTypes.get(
                            InstanceTypeKey.create(
                                    cluster.userIntent.instanceType,
                                    UUID.fromString(cluster.userIntent.provider))
                                .toString());
                    if (instanceType == null) {
                      log.warn(
                          "Matching instance type is not found for cluster:  ",
                          cluster.uuid,
                          cluster.userIntent.instanceType,
                          cluster.userIntent.provider);
                      continue;
                    } else {
                      // After PLAT-1584 is completed, read core count from user intent.
                      universeGroup.metric(
                          createContainerMetric(
                              customer,
                              universe,
                              PlatformMetrics.CONTAINER_RESOURCE_REQUESTS_CPU_CORES,
                              ipAddress,
                              nodeDetails.getK8sNamespace(),
                              nodeDetails.getK8sPodName(),
                              nodeDetails.isTserver,
                              KubernetesUtil.getCoreCountFromInstanceType(
                                  instanceType, nodeDetails.isMaster)));
                    }
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
      String ipAddress,
      int port,
      String exportType,
      double value) {
    String nodePrefix = universe.getUniverseDetails().nodePrefix;
    return buildMetricTemplate(metric, customer, universe)
        .setKeyLabel(KnownAlertLabels.NODE_PREFIX, nodePrefix)
        .setKeyLabel(KnownAlertLabels.INSTANCE, ipAddress + ":" + port)
        .setLabel(KnownAlertLabels.EXPORT_TYPE, exportType)
        .setValue(value);
  }

  // Create k8s node metric.
  private Metric createContainerMetric(
      Customer customer,
      Universe universe,
      PlatformMetrics metric,
      String ipAddress,
      String namespace,
      String podName,
      boolean isTserver,
      double value) {
    String nodePrefix = universe.getUniverseDetails().nodePrefix;
    return buildMetricTemplate(metric, customer, universe)
        .setKeyLabel(KnownAlertLabels.NODE_PREFIX, nodePrefix)
        .setKeyLabel(KnownAlertLabels.INSTANCE, ipAddress)
        .setKeyLabel(KnownAlertLabels.NAMESPACE, namespace)
        .setKeyLabel(KnownAlertLabels.POD_NAME, podName)
        .setKeyLabel(KnownAlertLabels.CONTAINER_NAME, isTserver ? "yb-tserver" : "yb-master")
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
