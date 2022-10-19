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
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.AccessKeyRotationUtil;
import com.yugabyte.yw.common.kms.util.KeyProvider;
import com.yugabyte.yw.common.kms.util.hashicorpvault.HashicorpVaultConfigParams;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.AccessKeyId;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.KmsConfig;
import com.yugabyte.yw.models.KmsHistory;
import com.yugabyte.yw.models.KmsHistoryId.TargetType;
import com.yugabyte.yw.models.Metric;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.filters.MetricFilter;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlatformMetrics;
import java.util.ArrayList;
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

  private static final List<PlatformMetrics> UNIVERSE_METRICS =
      ImmutableList.of(
          PlatformMetrics.UNIVERSE_EXISTS,
          PlatformMetrics.UNIVERSE_PAUSED,
          PlatformMetrics.UNIVERSE_UPDATE_IN_PROGRESS,
          PlatformMetrics.UNIVERSE_BACKUP_IN_PROGRESS,
          PlatformMetrics.UNIVERSE_NODE_FUNCTION,
          PlatformMetrics.UNIVERSE_ENCRYPTION_KEY_EXPIRY_DAY,
          PlatformMetrics.UNIVERSE_SSH_KEY_EXPIRY_DAY,
          PlatformMetrics.UNIVERSE_REPLICATION_FACTOR);

  @Override
  public List<MetricSaveGroup> getMetricGroups() throws Exception {
    List<MetricSaveGroup> metricSaveGroups = new ArrayList<>();
    Map<UUID, KmsHistory> activeEncryptionKeys =
        KmsHistory.getAllActiveHistory(TargetType.UNIVERSE_KEY)
            .stream()
            .collect(Collectors.toMap(key -> key.uuid.targetUuid, Function.identity()));
    Map<UUID, KmsConfig> kmsConfigMap =
        KmsConfig.listAllKMSConfigs()
            .stream()
            .collect(Collectors.toMap(config -> config.configUUID, Function.identity()));
    Map<AccessKeyId, AccessKey> allAccessKeys = accessKeyRotationUtil.createAllAccessKeysMap();
    for (Customer customer : Customer.getAll()) {
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
          universeGroup.metric(
              createUniverseMetric(
                  customer,
                  universe,
                  PlatformMetrics.UNIVERSE_BACKUP_IN_PROGRESS,
                  statusValue(universe.getUniverseDetails().backupInProgress)));
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
          universeGroup.metric(
              createUniverseMetric(
                  customer,
                  universe,
                  PlatformMetrics.UNIVERSE_REPLICATION_FACTOR,
                  universe.getUniverseDetails().getPrimaryCluster().userIntent.replicationFactor));

          if (universe.getUniverseDetails().nodeDetailsSet != null) {
            for (NodeDetails nodeDetails : universe.getUniverseDetails().nodeDetailsSet) {
              if (!nodeDetails.isActive()) {
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
                      PlatformMetrics.UNIVERSE_NODE_FUNCTION,
                      ipAddress,
                      nodeDetails.tserverHttpPort,
                      "tserver_export",
                      statusValue(nodeDetails.isTserver)));
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
              boolean hasNodeExporter =
                  !CloudType.kubernetes.equals(universe.getNodeDeploymentMode(nodeDetails));
              universeGroup.metric(
                  createNodeMetric(
                      customer,
                      universe,
                      PlatformMetrics.UNIVERSE_NODE_FUNCTION,
                      ipAddress,
                      nodeDetails.nodeExporterPort,
                      "node_export",
                      statusValue(hasNodeExporter)));
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

  private Double getEncryptionKeyExpiryDays(KmsHistory activeKey, Map<UUID, KmsConfig> configMap) {
    if (activeKey == null) {
      return null;
    }
    KmsConfig kmsConfig = configMap.get(activeKey.configUuid);
    if (kmsConfig == null) {
      log.warn(
          "Active universe {} key config {} is missing",
          activeKey.uuid.targetUuid,
          activeKey.configUuid);
      return null;
    }
    if (kmsConfig.keyProvider != KeyProvider.HASHICORP) {
      // For now only Hashicorp config expires.
      return null;
    }
    ObjectNode credentials = kmsConfig.authConfig;
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
