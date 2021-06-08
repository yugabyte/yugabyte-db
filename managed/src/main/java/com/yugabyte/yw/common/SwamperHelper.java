/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.annotations.VisibleForTesting;
import com.google.common.io.PatternFilenameFilter;
import com.yugabyte.yw.common.alerts.AlertRuleTemplateSubstitutor;
import com.yugabyte.yw.models.AlertDefinition;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import org.apache.commons.io.FilenameUtils;
import org.apache.commons.io.IOUtils;
import org.apache.commons.lang.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.Configuration;
import play.Environment;
import play.libs.Json;

import javax.inject.Inject;
import javax.inject.Singleton;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.*;
import java.util.regex.Pattern;
import java.util.stream.Collectors;

import static com.yugabyte.yw.common.Util.writeFile;
import static com.yugabyte.yw.common.Util.writeJsonFile;

@Singleton
public class SwamperHelper {
  public static final Logger LOG = LoggerFactory.getLogger(SwamperHelper.class);

  @VisibleForTesting static final String ALERT_CONFIG_FILE_PREFIX = "yugaware.ad.";
  private static final Pattern ALERT_CONFIG_FILE_PATTERN =
      Pattern.compile(
          "^yugaware\\.ad\\.[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}"
              + "-[0-9a-fA-F]{12}\\.yml$");

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

  private final play.Configuration appConfig;
  private final Environment environment;

  @Inject
  public SwamperHelper(Configuration appConfig, Environment environment) {
    this.appConfig = appConfig;
    this.environment = environment;
  }

  public enum TargetType {
    INVALID_EXPORT,
    NODE_EXPORT,
    MASTER_EXPORT,
    TSERVER_EXPORT,
    REDIS_EXPORT,
    CQL_EXPORT,
    YSQL_EXPORT;

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
        default:
          return 0;
      }
    }
  }

  public enum LabelType {
    NODE_PREFIX,
    EXPORT_TYPE,
    EXPORTED_INSTANCE
  }

  private ObjectNode getIndividualConfig(
      Universe universe, TargetType t, Collection<NodeDetails> nodes, String exportedInstance) {
    ObjectNode target = Json.newObject();
    ArrayNode targetNodes = Json.newArray();
    nodes.forEach(
        (node) -> {
          if (node.isActive()) {
            targetNodes.add(node.cloudInfo.private_ip + ":" + t.getPort(node));
          }
        });

    ObjectNode labels = Json.newObject();
    labels.put(
        LabelType.NODE_PREFIX.toString().toLowerCase(), universe.getUniverseDetails().nodePrefix);
    labels.put(LabelType.EXPORT_TYPE.toString().toLowerCase(), t.toString().toLowerCase());
    if (exportedInstance != null) {
      labels.put(LabelType.EXPORTED_INSTANCE.toString().toLowerCase(), exportedInstance);
    }

    target.set("targets", targetNodes);
    target.set("labels", labels);
    return target;
  }

  private String getSwamperFile(UUID universeUUID, String prefix) {
    String swamperFile = appConfig.getString("yb.swamper.targetPath");
    if (StringUtils.isEmpty(swamperFile)) {
      return null;
    }
    File swamperTargetDirectory = new File(swamperFile);

    if (swamperTargetDirectory.isDirectory() || swamperTargetDirectory.mkdirs()) {
      return String.format(
          "%s/%s.%s.json", swamperTargetDirectory, prefix, universeUUID.toString());
    }
    return null;
  }

  public void writeUniverseTargetJson(UUID universeUUID) {
    Universe universe = Universe.getOrBadRequest(universeUUID);

    // Write out the node specific file.
    ArrayNode nodeTargets = Json.newArray();
    String swamperFile = getSwamperFile(universeUUID, "node");
    if (swamperFile == null) {
      return;
    }
    universe
        .getNodes()
        .forEach(
            (node) -> {
              nodeTargets.add(
                  getIndividualConfig(
                      universe,
                      TargetType.NODE_EXPORT,
                      Collections.singletonList(node),
                      node.nodeName));
            });
    writeJsonFile(swamperFile, nodeTargets);

    // Write out the yugabyte specific file.
    ArrayNode ybTargets = Json.newArray();
    swamperFile = getSwamperFile(universeUUID, "yugabyte");
    for (TargetType t : TargetType.values()) {
      if (t != TargetType.NODE_EXPORT && t != TargetType.INVALID_EXPORT) {
        universe
            .getNodes()
            .forEach(
                (node) -> {
                  // Since some nodes might not be active (for example removed),
                  // we do not want to add them to the swamper targets.
                  if (node.isActive()) {
                    ybTargets.add(
                        getIndividualConfig(
                            universe, t, Collections.singletonList(node), node.nodeName));
                  }
                });
      }
    }
    writeJsonFile(swamperFile, ybTargets);
  }

  private void removeUniverseTargetJson(UUID universeUUID, String prefix) {
    String swamperFile = getSwamperFile(universeUUID, prefix);
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
    removeUniverseTargetJson(universeUUID, "node");
    removeUniverseTargetJson(universeUUID, "yugabyte");
  }

  private File getSwamperRuleDirectory() {
    String rulesFile = appConfig.getString("yb.swamper.rulesPath");
    if (StringUtils.isEmpty(rulesFile)) {
      return null;
    }
    File swamperRulesDirectory = new File(rulesFile);

    if (swamperRulesDirectory.isDirectory() || swamperRulesDirectory.mkdirs()) {
      return swamperRulesDirectory;
    }
    return null;
  }

  private String getSwamperRuleFile(UUID ruleUUID) {
    File swamperRulesDirectory = getSwamperRuleDirectory();
    if (swamperRulesDirectory != null) {
      return String.format(
          "%s/%s%s.yml", swamperRulesDirectory, ALERT_CONFIG_FILE_PREFIX, ruleUUID.toString());
    }
    return null;
  }

  public void writeAlertDefinition(AlertDefinition definition) {
    String swamperFile = getSwamperRuleFile(definition.getUuid());
    if (swamperFile == null) {
      return;
    }

    String template;
    try (InputStream templateStream = environment.resourceAsStream("alert/alert_definition.yml")) {
      template = IOUtils.toString(templateStream, StandardCharsets.UTF_8);
    } catch (IOException e) {
      throw new RuntimeException("Failed to read alert definition template", e);
    }

    AlertRuleTemplateSubstitutor substitutor = new AlertRuleTemplateSubstitutor(definition);
    String ruleDefinition = substitutor.replace(template);
    writeFile(swamperFile, ruleDefinition);
  }

  public void removeAlertDefinition(UUID definitionUUID) {
    String swamperFile = getSwamperRuleFile(definitionUUID);
    if (swamperFile != null) {
      File file = new File(swamperFile);

      if (file.exists()) {
        file.delete();
        LOG.info("Swamper Rules file deleted: {}", swamperFile);
      }
    }
  }

  public List<UUID> getAlertDefinitionConfigUuids() {
    File swamperRulesDir = getSwamperRuleDirectory();
    if (swamperRulesDir == null) {
      return Collections.emptyList();
    }
    String[] configFiles =
        swamperRulesDir.list(new PatternFilenameFilter(ALERT_CONFIG_FILE_PATTERN));
    if (configFiles == null) {
      throw new RuntimeException("Failed to list alert rules config files");
    }
    return Arrays.stream(configFiles)
        .map(FilenameUtils::removeExtension)
        .map(filename -> filename.replaceAll(ALERT_CONFIG_FILE_PREFIX, ""))
        .map(UUID::fromString)
        .collect(Collectors.toList());
  }
}
