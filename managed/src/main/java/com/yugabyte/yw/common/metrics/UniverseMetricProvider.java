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
import com.yugabyte.yw.common.*;
import com.yugabyte.yw.common.kms.util.KeyProvider;
import com.yugabyte.yw.common.kms.util.hashicorpvault.HashicorpVaultConfigParams;
import com.yugabyte.yw.models.*;
import com.yugabyte.yw.models.KmsHistoryId.TargetType;
import com.yugabyte.yw.models.filters.MetricFilter;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlatformMetrics;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.*;
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

  @Inject Environment environment;

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
          PlatformMetrics.UNIVERSE_REPLICATION_FACTOR);

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
    String ybaVersion = ConfigHelper.getCurrentVersion(environment);
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
          universeGroup.metric(
              createUniverseMetric(
                      customer,
                      universe,
                      PlatformMetrics.UNIVERSE_ACTIVE_TASK_CODE,
                      taskType != null ? taskType.getCode() : 0)
                  .setLabel(KnownAlertLabels.YBA_VERSION, ybaVersion)
                  .setLabel(KnownAlertLabels.DB_VERSION, dbVersion));
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
              boolean hasNodeExporter =
                  !CloudType.kubernetes.equals(universe.getNodeDeploymentMode(nodeDetails));
              universeGroup.metric(
                  createNodeMetric(
                      customer,
                      universe,
                      PlatformMetrics.UNIVERSE_NODE_FUNCTION,
                      nodeDetails,
                      nodeInstance,
                      nodeDetails.nodeExporterPort,
                      "node_export",
                      statusValue(hasNodeExporter)));
              universeGroup.metric(
                  createNodeMetric(
                      customer,
                      universe,
                      PlatformMetrics.UNIVERSE_NODE_PROCESS_STATUS,
                      nodeDetails,
                      nodeInstance,
                      nodeDetails.nodeExporterPort,
                      "node_export",
                      statusValue(hasNodeExporter && nodeDetails.isActive())));
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
            .setValue(value);
    if (nodeInstance != null && StringUtils.isNotEmpty(nodeInstance.getDetails().instanceName)) {
      template.setLabel(KnownAlertLabels.NODE_IDENTIFIER, nodeInstance.getDetails().instanceName);
    }
    return template;
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
