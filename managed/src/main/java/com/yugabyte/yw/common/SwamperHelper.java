/*
 * Copyright 2019 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common;

import static com.yugabyte.yw.common.utils.FileUtils.writeFile;
import static com.yugabyte.yw.common.utils.FileUtils.writeJsonFile;
import static com.yugabyte.yw.common.utils.FileUtils.writeJsonFileAtomically;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.annotations.VisibleForTesting;
import com.google.common.io.PatternFilenameFilter;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.alerts.AlertRuleTemplateSubstitutor;
import com.yugabyte.yw.common.alerts.impl.AlertTemplateService;
import com.yugabyte.yw.common.alerts.impl.AlertTemplateService.AlertTemplateDescription;
import com.yugabyte.yw.common.audit.otel.OtelCollectorUtil;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.models.*;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import com.yugabyte.yw.models.helpers.MetricCollectionLevel;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import io.fabric8.kubernetes.api.model.Pod;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.*;
import java.util.concurrent.ConcurrentHashMap;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.Getter;
import org.apache.commons.io.FilenameUtils;
import org.apache.commons.io.IOUtils;
import org.apache.commons.lang3.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.Environment;
import play.libs.Json;

@Singleton
public class SwamperHelper {
  public static final Logger LOG = LoggerFactory.getLogger(SwamperHelper.class);

  // One lock per universe for its node file.
  private final Map<UUID, Object> nodeFileLocks = new ConcurrentHashMap<>();

  private static final String UUID_PATTERN =
      "[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}";

  @VisibleForTesting static final String ALERT_CONFIG_FILE_PREFIX = "yugaware.ad.";
  @VisibleForTesting static final String ALERT_CONFIG_FILE_PREFIX_PATTERN = "yugaware\\.ad\\.";
  @VisibleForTesting static final String RECORDING_RULES_FILE = "yugaware.recording-rules.yml";
  private static final Pattern ALERT_CONFIG_FILE_PATTERN =
      Pattern.compile("^yugaware\\.ad\\." + UUID_PATTERN + "\\.yml$");

  @VisibleForTesting static final String TARGET_FILE_NODE_PREFIX = "node.";
  @VisibleForTesting static final String TARGET_FILE_YUGABYTE_PREFIX = "yugabyte.";
  @VisibleForTesting static final String TARGET_FILE_OTEL_PREFIX = "otel.";
  @VisibleForTesting static final String TARGET_FILE_PREFIX_PATTERN = "(node|yugabyte)\\.";
  @VisibleForTesting static final String TARGET_FILE_NODE_AGENT_PREFIX = "node-agent.";
  @VisibleForTesting static final String TARGET_FILE_NODE_AGENT_PREFIX_PATTERN = "node-agent\\.";
  private static final Pattern TARGET_FILE_PATTERN =
      Pattern.compile("^(node|yugabyte)\\." + UUID_PATTERN + ".json$");
  private static final Pattern TARGET_FILE_NODE_AGENT_PATTERN =
      Pattern.compile("^node-agent\\." + UUID_PATTERN + ".json$");

  private static final String TARGET_PATH_PARAM = "yb.swamper.targetPath";
  private static final String RULES_PATH_PARAM = "yb.swamper.rulesPath";
  public static final String COLLECTION_LEVEL_PARAM = "yb.metrics.collection_level";
  public static final String SCRAPE_INTERVAL_PARAM = "yb.metrics.scrape_interval";
  public static final String RANGE_PLACEHOLDER = "\\{\\{ range \\}\\}";

  private static final String PARAMETER_LABEL_PREFIX = "__param_";

  private static final int IRATE_SCRAPE_PERIODS = 5;

  private static final String SCHEME_LABEL = "__scheme__";
  private static final String HTTP = "http";
  private static final String HTTPS = "https";

  /*
     Sample targets file
    [
      {
        "targets": [
          '10.150.0.64:9300', '10.150.0.49:9300', '10.150.0.62:9300',
          '10.150.0.64:7000', '10.150.0.49:7000', '10.150.0.62:7000',
          '10.150.0.64:9000', '10.150.0.49:9000', '10.150.0.62:9000',
          '10.150.0.64:11000', '10.150.0.49:11000', '10.150.0.62:11000',
          '10.150.0.64:12000', '10.150.0.49:12000', '10.150.0.62:12000'
        ],
        "labels": {
          "node_prefix": "1-a-b"
        }
      }
    ]
  */

  private final RuntimeConfigFactory runtimeConfigFactory;
  private final Environment environment;
  private final RuntimeConfGetter confGetter;

  private final AlertTemplateService alertTemplateService;
  private final KubernetesManagerFactory kubernetesManagerFactory;

  @Inject
  public SwamperHelper(
      RuntimeConfigFactory runtimeConfigFactory,
      Environment environment,
      RuntimeConfGetter confGetter,
      AlertTemplateService alertTemplateService,
      KubernetesManagerFactory kubernetesManagerFactory) {
    this.runtimeConfigFactory = runtimeConfigFactory;
    this.environment = environment;
    this.confGetter = confGetter;
    this.alertTemplateService = alertTemplateService;
    this.kubernetesManagerFactory = kubernetesManagerFactory;
  }

  @Getter
  public enum TargetType {
    INVALID_EXPORT(false, false),
    NODE_EXPORT(false, true),
    MASTER_EXPORT(true, false),
    TSERVER_EXPORT(true, false),
    REDIS_EXPORT(true, false),
    CQL_EXPORT(true, false),
    YSQL_EXPORT(true, false),
    OTEL_EXPORT(false, true);

    private final boolean collectionLevelSupported;
    private final boolean exportedInstanceLabelNeeded;

    TargetType(boolean collectionLevelSupported, boolean exportedInstanceLabelNeeded) {
      this.collectionLevelSupported = collectionLevelSupported;
      this.exportedInstanceLabelNeeded = exportedInstanceLabelNeeded;
    }

    public int getPort(NodeDetails nodeDetails) {
      switch (this) {
        case INVALID_EXPORT:
          return 0;
        case NODE_EXPORT:
          return nodeDetails.nodeExporterPort;
        case MASTER_EXPORT:
          return nodeDetails.masterHttpPort;
        case TSERVER_EXPORT:
          return nodeDetails.tserverHttpPort;
        case REDIS_EXPORT:
          return nodeDetails.redisServerHttpPort;
        case CQL_EXPORT:
          return nodeDetails.yqlServerHttpPort;
        case YSQL_EXPORT:
          return nodeDetails.ysqlServerHttpPort;
        case OTEL_EXPORT:
          return nodeDetails.otelCollectorMetricsPort;
        default:
          return 0;
      }
    }
  }

  public enum LabelType {
    NODE_PREFIX,
    EXPORT_TYPE,
    EXPORTED_INSTANCE,
    NODE_NAME,
    NODE_ADDRESS,
    NODE_IDENTIFIER,
    NODE_REGION,
    NODE_AZ,
    NODE_CLUSTER_TYPE,
    UNIVERSE_UUID;

    public String getLabelName() {
      return name().toLowerCase();
    }
  }

  private ObjectNode getIndividualConfig(Universe universe, TargetType t, NodeDetails nodeDetails) {
    ObjectNode target = Json.newObject();
    ArrayNode targetNodes = Json.newArray();
    int port = t.getPort(nodeDetails);
    if (processSwamperForNode(nodeDetails) && port > 0) {
      targetNodes.add(nodeDetails.cloudInfo.private_ip + ":" + port);
    }

    ObjectNode labels = Json.newObject();
    labels.put(LabelType.UNIVERSE_UUID.getLabelName(), universe.getUniverseUUID().toString());
    labels.put(LabelType.NODE_PREFIX.getLabelName(), universe.getUniverseDetails().nodePrefix);
    labels.put(LabelType.EXPORT_TYPE.getLabelName(), t.toString().toLowerCase());
    if (nodeDetails.nodeName != null) {
      // exported_instance is a special name that we should not use as a custom label.
      // You get metrics with exported_ prefix if the collected data already have one of
      // the predefined labels (such as 'job' and 'instance').
      // As a result - our 'up' metrics does not have this exported_instance label (seem like some
      // internal prometheus logic we can't change), while other metrics have one.
      // So better just not use that. But have to leave for backward compatibility anyway.

      // We are now removing this label from DB exported metrics, since it gets confusing for
      // customers when they see both exported_instance and exported_exported_instance labels for
      // metrics.
      // But for node exporter and otel collector, this is still needed since those 2 don't have
      // their own exported_instance label.
      // Update this for any new target types added accordingly whether it's required or not.
      if (t != null && t.exportedInstanceLabelNeeded) {
        labels.put(LabelType.EXPORTED_INSTANCE.getLabelName(), nodeDetails.nodeName);
      }
      labels.put(LabelType.NODE_NAME.getLabelName(), nodeDetails.nodeName);
    }
    if (nodeDetails.cloudInfo != null) {
      if (nodeDetails.cloudInfo.private_ip != null) {
        labels.put(LabelType.NODE_ADDRESS.getLabelName(), nodeDetails.cloudInfo.private_ip);
      }
      if (nodeDetails.cloudInfo.region != null) {
        labels.put(LabelType.NODE_REGION.getLabelName(), nodeDetails.cloudInfo.region);
      }
      if (nodeDetails.cloudInfo.az != null) {
        labels.put(LabelType.NODE_AZ.getLabelName(), nodeDetails.cloudInfo.az);
      }
      if (CloudType.onprem.name().equals(nodeDetails.cloudInfo.cloud)) {
        NodeInstance.maybeGet(nodeDetails.nodeUuid)
            .ifPresent(
                nodeInstance -> {
                  if (StringUtils.isNotEmpty(nodeInstance.getDetails().instanceName)) {
                    labels.put(
                        LabelType.NODE_IDENTIFIER.getLabelName(),
                        nodeInstance.getDetails().instanceName);
                  }
                });
      }
    }
    if (nodeDetails.placementUuid != null) {
      labels.put(
          LabelType.NODE_CLUSTER_TYPE.getLabelName(),
          universe.getCluster(nodeDetails.placementUuid).clusterType.toString());
    }
    if (t.isCollectionLevelSupported()) {
      MetricCollectionLevel level = getLevel(universe);
      appendCollectionLevelLabels(level, labels);
    }
    target.set("targets", targetNodes);
    target.set("labels", labels);
    return target;
  }

  private File getSwamperTargetDirectory() {
    return getOrCreateDirectory(TARGET_PATH_PARAM);
  }

  private String getSwamperFile(UUID targetUUID, String prefix) {
    File swamperTargetDirectory = getSwamperTargetDirectory();

    if (swamperTargetDirectory != null) {
      return String.format("%s/%s%s.json", swamperTargetDirectory, prefix, targetUUID.toString());
    }
    return null;
  }

  public void writeUniverseTargetJson(UUID universeUUID) {
    writeUniverseTargetJson(universeUUID, false /* readPods */);
  }

  public void writeUniverseTargetJson(UUID universeUUID, boolean readPods) {
    Universe universe = Universe.getOrBadRequest(universeUUID);
    writeUniverseTargetJson(universe, readPods);
  }

  public void writeUniverseTargetJson(Universe universe) {
    writeUniverseTargetJson(universe, false /* readPods */);
  }

  /**
   * Writes the universe's target files.
   *
   * @param readPods read each K8s pod's sidecar scheme even when the previous file has every node
   */
  public void writeUniverseTargetJson(Universe universe, boolean readPods) {
    MetricCollectionLevel level = getLevel(universe);

    if (level.isDisableCollection()) {
      removeUniverseTargetJson(universe.getUniverseUUID());
      return;
    }

    // Write out the node specific file.
    ArrayNode nodeTargets = Json.newArray();
    String swamperFile = getSwamperFile(universe.getUniverseUUID(), TARGET_FILE_NODE_PREFIX);
    if (swamperFile == null) {
      return;
    }
    // K8s pods are scraped only while the node_exporter sidecar is enabled.
    boolean scrapeKubernetesPods =
        KubernetesUtil.isNodeExporterSidecarEnabled(universe, confGetter);
    List<NodeDetails> nodeExporterNodes = new ArrayList<>();
    universe
        .getNodes()
        .forEach(
            (node) -> {
              if (universe.getNodeDeploymentMode(node).equals(CloudType.kubernetes)
                  && !scrapeKubernetesPods) {
                return;
              }
              nodeExporterNodes.add(node);
            });
    // The schemes carry over from the previous file, so a universe's read and write run alone.
    synchronized (nodeFileLocks.computeIfAbsent(universe.getUniverseUUID(), k -> new Object())) {
      Map<String, String> schemes = getNodeExporterSchemes(universe, nodeExporterNodes, readPods);
      for (NodeDetails node : nodeExporterNodes) {
        ObjectNode target = getIndividualConfig(universe, TargetType.NODE_EXPORT, node);
        String scheme = schemes.get(node.nodeName);
        if (scheme != null) {
          ((ObjectNode) target.get("labels")).put(SCHEME_LABEL, scheme);
        }
        nodeTargets.add(target);
      }
      // Atomic, as Prometheus may re-read the file at any time.
      writeJsonFileAtomically(swamperFile, nodeTargets, "rw-r--r--");
    }

    // Write out the otel specific file.
    if (universe.getUniverseDetails().otelCollectorEnabled) {
      ArrayNode otelTargets = Json.newArray();
      swamperFile = getSwamperFile(universe.getUniverseUUID(), TARGET_FILE_OTEL_PREFIX);
      // On K8s the collector is a sidecar: always injected into yb-tserver pods, and injected into
      // yb-master pods only when the chart was told to do so. Resolve that once per universe (it
      // reads the export telemetry config) rather than per node, and only for K8s universes.
      boolean otelOnK8sMasterPods =
          Util.isKubernetesBasedUniverse(universe)
              && OtelCollectorUtil.isOtelSidecarNeededOnK8sMasterPods(
                  OtelCollectorUtil.getCurrentTelemetryConfig(universe));
      universe
          .getNodes()
          .forEach(
              (node) -> {
                if (!processSwamperForNode(node)) {
                  // Skip nodes that are not active (for example removed).
                  return;
                }
                if (!node.isMaster && !node.isTserver) {
                  // The collector always lives alongside a DB process.
                  return;
                }
                if (universe.getNodeDeploymentMode(node).equals(CloudType.kubernetes)
                    && !node.isTserver
                    && !otelOnK8sMasterPods) {
                  // Master and TServer pods are modelled as separate nodes on K8s, so a
                  // master-only node here means a yb-master pod, which only has a sidecar when
                  // the chart was told to inject one. On VMs there is no such gating:
                  // ManageOtelCollector runs on every master and tserver node, dedicated master
                  // nodes included (that is where master log export reads yb-master glog from).
                  return;
                }
                otelTargets.add(getIndividualConfig(universe, TargetType.OTEL_EXPORT, node));
              });
      writeJsonFile(swamperFile, otelTargets);
    }

    // Write out the yugabyte specific file.
    ArrayNode ybTargets = Json.newArray();
    swamperFile = getSwamperFile(universe.getUniverseUUID(), TARGET_FILE_YUGABYTE_PREFIX);
    universe
        .getNodes()
        .forEach(
            (node) -> {
              // Since some nodes might not be active (for example removed),
              // we do not want to add them to the swamper targets.
              if (!processSwamperForNode(node)) {
                return;
              }

              for (TargetType t : TargetType.values()) {
                if (t == TargetType.NODE_EXPORT
                    || t == TargetType.OTEL_EXPORT
                    || t == TargetType.INVALID_EXPORT) {
                  continue;
                }

                if (!node.isMaster && t.equals(TargetType.MASTER_EXPORT)) {
                  continue;
                }

                if (node.isTserver) {
                  if (!node.isYsqlServer && t.equals(TargetType.YSQL_EXPORT)) {
                    continue;
                  }
                  if (!node.isYqlServer && t.equals(TargetType.CQL_EXPORT)) {
                    continue;
                  }
                  if (!node.isRedisServer && t.equals(TargetType.REDIS_EXPORT)) {
                    continue;
                  }
                } else {
                  if (t.equals(TargetType.TSERVER_EXPORT)
                      || t.equals(TargetType.CQL_EXPORT)
                      || t.equals(TargetType.REDIS_EXPORT)
                      || t.equals(TargetType.YSQL_EXPORT)) {
                    continue;
                  }
                }

                ybTargets.add(getIndividualConfig(universe, t, node));
              }
            });

    writeJsonFile(swamperFile, ybTargets);
  }

  /**
   * The __scheme__ label of each node_exporter target by node name; a target without one uses the
   * "node" job's HTTP.
   */
  private Map<String, String> getNodeExporterSchemes(
      Universe universe, List<NodeDetails> nodes, boolean readPods) {
    if (Util.isKubernetesBasedUniverse(universe)) {
      return getKubernetesNodeExporterSchemes(universe, nodes, readPods);
    }
    if (!universe.isNodeExporterHttps()) {
      return Collections.emptyMap();
    }
    Map<String, String> schemes = new HashMap<>();
    nodes.forEach(node -> schemes.put(node.nodeName, HTTPS));
    return schemes;
  }

  /**
   * The node_exporter scheme of each K8s node's target, keyed by node name. Pods are read when
   * asked to or when the previous node file lacks any node, and then every node takes its pod's
   * scheme; a pod that can't be read keeps its previous scheme, else YBA's wanted one.
   */
  private Map<String, String> getKubernetesNodeExporterSchemes(
      Universe universe, List<NodeDetails> nodes, boolean readPods) {
    if (nodes.isEmpty()) {
      return Collections.emptyMap();
    }
    String intent =
        KubernetesUtil.isNodeExporterSidecarTlsEnabled(universe, confGetter) ? HTTPS : HTTP;
    Map<String, String> previous = readPreviousNodeExporterSchemes(universe.getUniverseUUID());
    if (!readPods && nodes.stream().allMatch(node -> previous.containsKey(node.nodeName))) {
      return previous;
    }
    Map<UUID, Map<String, Pod>> podsPerAZ = listPodsPerAZ(universe);
    Map<String, String> schemes = new HashMap<>();
    for (NodeDetails node : nodes) {
      Pod pod = podsPerAZ.getOrDefault(node.azUuid, Collections.emptyMap()).get(podKey(node));
      String scheme;
      if (pod != null) {
        scheme = KubernetesUtil.isNodeExporterSidecarTlsRendered(pod) ? HTTPS : HTTP;
      } else {
        scheme = previous.getOrDefault(node.nodeName, intent);
      }
      schemes.put(node.nodeName, scheme);
    }
    return schemes;
  }

  // The node_name to __scheme__ of the K8s entries in the node file; empty if it can't be read.
  private Map<String, String> readPreviousNodeExporterSchemes(UUID universeUUID) {
    Map<String, String> schemes = new HashMap<>();
    try {
      File file = new File(getSwamperFile(universeUUID, TARGET_FILE_NODE_PREFIX));
      if (!file.exists()) {
        return schemes;
      }
      JsonNode entries = Json.parse(Files.readString(file.toPath()));
      for (JsonNode entry : entries) {
        JsonNode labels = entry.path("labels");
        if (labels.hasNonNull(LabelType.NODE_NAME.getLabelName())
            && labels.hasNonNull(SCHEME_LABEL)) {
          schemes.put(
              labels.get(LabelType.NODE_NAME.getLabelName()).asText(),
              labels.get(SCHEME_LABEL).asText());
        }
      }
    } catch (Exception e) {
      LOG.warn(
          "Unable to read the previous node_exporter targets of universe {}: {}",
          universeUUID,
          e.getMessage());
      schemes.clear();
    }
    return schemes;
  }

  // Each AZ's pods by namespace/name; an AZ or cluster whose listing fails gets no pods.
  private Map<UUID, Map<String, Pod>> listPodsPerAZ(Universe universe) {
    UniverseDefinitionTaskParams details = universe.getUniverseDetails();
    Map<UUID, Map<String, Pod>> podsPerAZ = new HashMap<>();
    List<String> failures = new ArrayList<>();
    for (Cluster cluster : details.clusters) {
      boolean isReadOnlyCluster = cluster.clusterType == ClusterType.ASYNC;
      boolean isMultiAZ;
      Map<UUID, Map<String, String>> azToConfig;
      try {
        isMultiAZ = PlacementInfoUtil.isMultiAZ(Util.getSingleProvider(cluster));
        azToConfig = KubernetesUtil.getConfigPerAZ(cluster.placementInfo);
      } catch (Exception e) {
        failures.add("cluster " + cluster.uuid + ": " + e.getMessage());
        continue;
      }
      for (Map.Entry<UUID, Map<String, String>> entry : azToConfig.entrySet()) {
        try {
          String azName =
              isMultiAZ ? AvailabilityZone.getOrBadRequest(entry.getKey()).getCode() : null;
          Map<String, Pod> pods = podsPerAZ.computeIfAbsent(entry.getKey(), k -> new HashMap<>());
          for (Pod pod :
              kubernetesManagerFactory
                  .getManager()
                  .getPodInfos(
                      entry.getValue(),
                      KubernetesUtil.getHelmReleaseName(
                          isMultiAZ,
                          details.nodePrefix,
                          universe.getName(),
                          azName,
                          isReadOnlyCluster,
                          details.useNewHelmNamingStyle),
                      KubernetesUtil.getKubernetesNamespace(
                          isMultiAZ,
                          details.nodePrefix,
                          azName,
                          entry.getValue(),
                          details.useNewHelmNamingStyle,
                          isReadOnlyCluster))) {
            pods.put(pod.getMetadata().getNamespace() + "/" + pod.getMetadata().getName(), pod);
          }
        } catch (Exception e) {
          failures.add("zone " + entry.getKey() + ": " + e.getMessage());
        }
      }
    }
    if (!failures.isEmpty()) {
      LOG.warn(
          "Unable to list pods of universe {}; their nodes keep their previous node_exporter"
              + " schemes, else YBA's: {}",
          universe.getUniverseUUID(),
          failures);
    }
    return podsPerAZ;
  }

  // Namespace and pod name; null when the node lacks either and has no address to infer them from.
  private static String podKey(NodeDetails node) {
    if (StringUtils.isBlank(node.cloudInfo.private_ip)
        && (StringUtils.isBlank(node.cloudInfo.kubernetesPodName)
            || StringUtils.isBlank(node.cloudInfo.kubernetesNamespace))) {
      return null;
    }
    return node.getK8sNamespace() + "/" + node.getK8sPodName();
  }

  public void writeNodeAgentTargetJson(NodeAgent nodeAgent) {
    String swamperFile = getSwamperFile(nodeAgent.getUuid(), TARGET_FILE_NODE_AGENT_PREFIX);
    if (swamperFile == null || new File(swamperFile).exists()) {
      return;
    }
    ArrayNode targets = Json.newArray();
    ObjectNode target = Json.newObject();
    ArrayNode targetNodes = Json.newArray();
    ObjectNode labels = Json.newObject();
    targetNodes.add(String.format("%s:%d", nodeAgent.getIp(), nodeAgent.getPort()));
    labels.put(KnownAlertLabels.CUSTOMER_UUID.labelName(), nodeAgent.getCustomerUuid().toString());
    labels.put(KnownAlertLabels.NODE_AGENT_UUID.labelName(), nodeAgent.getUuid().toString());
    target.set("targets", targetNodes);
    target.set("labels", labels);
    targets.add(target);
    writeJsonFile(swamperFile, targets);
  }

  private void removeTargetJson(UUID targetUUID, String prefix) {
    String swamperFile = getSwamperFile(targetUUID, prefix);
    if (swamperFile != null) {
      File file = new File(swamperFile);

      if (file.exists()) {
        LOG.info("Deleting Swamper Target file: {}", swamperFile);
        file.delete();
      }
    }
  }

  public void removeUniverseTargetJson(UUID universeUUID) {
    // TODO: make these constants / enums.
    removeTargetJson(universeUUID, TARGET_FILE_NODE_PREFIX);
    removeTargetJson(universeUUID, TARGET_FILE_OTEL_PREFIX);
    removeTargetJson(universeUUID, TARGET_FILE_YUGABYTE_PREFIX);
  }

  public void removeNodeAgentTargetJson(UUID nodeAgentUUID) {
    removeTargetJson(nodeAgentUUID, TARGET_FILE_NODE_AGENT_PREFIX);
  }

  private File getSwamperRuleDirectory() {
    return getOrCreateDirectory(RULES_PATH_PARAM);
  }

  private String getAlertRuleFile(UUID ruleUUID) {
    return getRulesFile(String.format("%s%s.yml", ALERT_CONFIG_FILE_PREFIX, ruleUUID.toString()));
  }

  private String getRulesFile(String filename) {
    File swamperRulesDirectory = getSwamperRuleDirectory();
    if (swamperRulesDirectory != null) {
      return String.format("%s/%s", swamperRulesDirectory, filename);
    }
    return null;
  }

  public void writeRecordingRules() {
    String rulesFile = getRulesFile(RECORDING_RULES_FILE);
    if (rulesFile == null) {
      return;
    }

    String fileContent;
    try (InputStream templateStream = environment.resourceAsStream("metric/recording_rules.yml")) {
      fileContent = IOUtils.toString(templateStream, StandardCharsets.UTF_8);
      long scrapeInterval = getScrapeIntervalSeconds(runtimeConfigFactory.staticApplicationConf());
      fileContent =
          fileContent.replaceAll(
              RANGE_PLACEHOLDER, String.format("%ds", (scrapeInterval * IRATE_SCRAPE_PERIODS)));
    } catch (IOException e) {
      throw new RuntimeException("Failed to read alert definition header template", e);
    }

    writeFile(rulesFile, fileContent);
  }

  public void writeAlertDefinition(
      AlertConfiguration configuration,
      AlertDefinition definition,
      AlertTemplateSettings templateSettings) {
    String swamperFile = getAlertRuleFile(definition.getUuid());
    if (swamperFile == null) {
      return;
    }

    AlertTemplateDescription alertTemplateDescription =
        alertTemplateService.getTemplateDescription(configuration.getTemplate());

    String fileContent;
    try (InputStream templateStream =
        environment.resourceAsStream("alert/alert_definition_header.yml")) {
      fileContent = IOUtils.toString(templateStream, StandardCharsets.UTF_8);
    } catch (IOException e) {
      throw new RuntimeException("Failed to read alert definition header template", e);
    }

    String template;
    try (InputStream templateStream =
        environment.resourceAsStream("alert/alert_definition_rule.yml")) {
      template = IOUtils.toString(templateStream, StandardCharsets.UTF_8);
    } catch (IOException e) {
      throw new RuntimeException("Failed to read alert definition rule template", e);
    }

    fileContent +=
        configuration.getThresholds().keySet().stream()
            .map(
                severity -> {
                  AlertRuleTemplateSubstitutor substitutor =
                      new AlertRuleTemplateSubstitutor(
                          confGetter,
                          alertTemplateDescription,
                          configuration,
                          definition,
                          severity,
                          templateSettings);
                  return substitutor.replace(template);
                })
            .collect(Collectors.joining());

    writeFile(swamperFile, fileContent);
  }

  public void removeAlertDefinition(UUID definitionUUID) {
    String swamperFile = getAlertRuleFile(definitionUUID);
    if (swamperFile != null) {
      File file = new File(swamperFile);

      if (file.exists()) {
        file.delete();
        LOG.info("Swamper Rules file deleted: {}", swamperFile);
      }
    }
  }

  public List<UUID> getAlertDefinitionConfigUuids() {
    return extractUuids(
        getSwamperRuleDirectory(), ALERT_CONFIG_FILE_PATTERN, ALERT_CONFIG_FILE_PREFIX_PATTERN);
  }

  public List<UUID> getTargetUniverseUuids() {
    return extractUuids(
        getSwamperTargetDirectory(), TARGET_FILE_PATTERN, TARGET_FILE_PREFIX_PATTERN);
  }

  public List<UUID> getTargetNodeAgentUuids() {
    return extractUuids(
        getSwamperTargetDirectory(),
        TARGET_FILE_NODE_AGENT_PATTERN,
        TARGET_FILE_NODE_AGENT_PREFIX_PATTERN);
  }

  private List<UUID> extractUuids(File directory, Pattern pattern, String prefix) {
    if (directory == null) {
      return Collections.emptyList();
    }
    String[] targetFiles = directory.list(new PatternFilenameFilter(pattern));
    if (targetFiles == null) {
      throw new RuntimeException("Failed to list files in " + directory);
    }
    return Arrays.stream(targetFiles)
        .map(FilenameUtils::removeExtension)
        .map(filename -> filename.replaceAll(prefix, ""))
        .map(UUID::fromString)
        .collect(Collectors.toList());
  }

  private File getOrCreateDirectory(String configParam) {
    String directoryPath = runtimeConfigFactory.staticApplicationConf().getString(configParam);
    if (StringUtils.isEmpty(directoryPath)) {
      return null;
    }
    File directory = new File(directoryPath);

    if (directory.isDirectory() || directory.mkdirs()) {
      return directory;
    }
    return null;
  }

  private MetricCollectionLevel getLevel(Universe universe) {
    return MetricCollectionLevel.fromString(
        confGetter.getConfForScope(universe, UniverseConfKeys.metricsCollectionLevel));
  }

  private void appendCollectionLevelLabels(MetricCollectionLevel level, ObjectNode labels) {
    Map<String, String> params = level.parseParamsFile(environment);
    for (Map.Entry<String, String> entry : params.entrySet()) {
      labels.put(PARAMETER_LABEL_PREFIX + entry.getKey(), entry.getValue());
    }
  }

  public static long getScrapeIntervalSeconds(Config config) {
    return getScrapeIntervalSeconds(config.getString(SCRAPE_INTERVAL_PARAM));
  }

  public static long getScrapeIntervalSeconds(String scrapeIntervalStr) {
    return Util.goDurationToJava(scrapeIntervalStr).getSeconds();
  }

  public boolean processSwamperForNode(NodeDetails node) {
    return node.isActive() || node.state.equals(NodeState.ToBeRemoved);
  }
}
