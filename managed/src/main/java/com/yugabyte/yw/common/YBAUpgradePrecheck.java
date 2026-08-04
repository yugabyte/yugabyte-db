// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.annotation.JsonInclude.Include;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.SerializationFeature;
import com.google.common.collect.ImmutableSet;
import com.typesafe.config.Config;
import java.nio.charset.StandardCharsets;
import java.nio.file.Path;
import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Iterator;
import java.util.Map;
import java.util.Set;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import play.libs.Json;

@Slf4j
@Singleton
public class YBAUpgradePrecheck {
  private static final String DATABASE_DRIVER_PARAM = "db.default.driver";
  private static final String DATABASE_CONNECT_URL_PARAM = "db.default.url";
  private static final String DATABASE_USERNAME_PARAM = "db.default.username";
  private static final String DATABASE_PASSWORD_PARAM = "db.default.password";
  private static final Set<String> ELIGIBLE_PROVIDERS =
      ImmutableSet.of("onprem", "aws", "gcp", "azu");

  private final Config config;

  private final ObjectMapper mapper =
      new ObjectMapper()
          .configure(SerializationFeature.FAIL_ON_EMPTY_BEANS, false)
          .setSerializationInclusion(Include.ALWAYS);

  // This is the output to be serialized and dumped to a file.
  static class PrecheckOutput {
    @JsonProperty public boolean passed = true;
    @JsonProperty boolean nodeAgentClientDisabled = false;
    @JsonProperty Map<String, NodeInstanceConfig> nodeInstanceConfigs = new HashMap<>();
    @JsonProperty Map<String, UniverseConfig> universeConfigs = new HashMap<>();
  }

  static class NodeInstanceConfig {
    @JsonProperty String ip;
    @JsonProperty String instanceName;
    @JsonProperty String provider;
  }

  static class UniverseConfig {
    @JsonProperty Set<String> nodeIps = new HashSet<>();
    @JsonProperty boolean systemdEnabled;
  }

  @Inject
  public YBAUpgradePrecheck(Config config) {
    this.config = config;
  }

  private Map<String, String> getNodeAgentClientRuntimeConfigs(Connection conn)
      throws SQLException {
    Map<String, String> runtimeValues = new HashMap<>();
    try (ResultSet resultSet =
        conn.createStatement()
            .executeQuery(
                "SELECT scope_uuid, value AS value FROM runtime_config_entry WHERE"
                    + " path = 'yb.node_agent.client.enabled'")) {
      while (resultSet.next()) {
        runtimeValues.put(
            resultSet.getString("scope_uuid"),
            new String(resultSet.getBytes("value"), StandardCharsets.UTF_8));
      }
    }
    return runtimeValues;
  }

  private Map<String, UniverseConfig> getUniverseConfigs(Connection conn) throws SQLException {
    Map<String, UniverseConfig> univConfigs = new HashMap<>();
    try (ResultSet resultSet =
        conn.createStatement()
            .executeQuery("SELECT universe_uuid, universe_details_json FROM universe")) {
      while (resultSet.next()) {
        String univUuid = resultSet.getString("universe_uuid");
        JsonNode univDetails = Json.parse(resultSet.getString("universe_details_json"));
        JsonNode nodeDetailsSet = univDetails.get("nodeDetailsSet");
        if (nodeDetailsSet == null || !nodeDetailsSet.isArray()) {
          continue;
        }
        JsonNode clusters = univDetails.get("clusters");
        if (clusters == null || !clusters.isArray()) {
          continue;
        }
        boolean skipUniverse = true;
        Iterator<JsonNode> clusterIter = clusters.iterator();
        while (clusterIter.hasNext()) {
          JsonNode cluster = clusterIter.next();
          JsonNode userIntent = cluster.get("userIntent");
          if (userIntent == null || userIntent.isNull()) {
            break;
          }
          JsonNode providerType = userIntent.get("providerType");
          if (providerType == null || providerType.isNull()) {
            break;
          }
          if (!ELIGIBLE_PROVIDERS.contains(providerType.asText())) {
            break;
          }
          skipUniverse = false;
          JsonNode useSystemd = userIntent.get("useSystemd");
          boolean systemdEnabled =
              useSystemd != null && !useSystemd.isNull() && useSystemd.asBoolean();
          univConfigs.computeIfAbsent(univUuid, k -> new UniverseConfig()).systemdEnabled =
              systemdEnabled;
        }
        if (skipUniverse) {
          log.debug("Skipping universe: {}", univUuid);
          continue;
        }
        Iterator<JsonNode> nodeIter = nodeDetailsSet.iterator();
        while (nodeIter.hasNext()) {
          JsonNode nodeDetails = nodeIter.next();
          JsonNode state = nodeDetails.get("state");
          if (state == null || !state.isTextual()) {
            continue;
          }
          if (!"Live".equalsIgnoreCase(state.asText())) {
            continue;
          }
          JsonNode cloudInfo = nodeDetails.get("cloudInfo");
          if (cloudInfo == null || !cloudInfo.isObject()) {
            continue;
          }
          JsonNode privateIpNode = cloudInfo.get("private_ip");
          if (privateIpNode == null || privateIpNode.isNull()) {
            continue;
          }
          String privateIp = privateIpNode.asText();
          log.debug("Universe node IP: {}", privateIp);
          univConfigs.computeIfAbsent(univUuid, k -> new UniverseConfig()).nodeIps.add(privateIp);
        }
      }
    }
    return univConfigs;
  }

  private Map<String, NodeInstanceConfig> getNodeInstanceConfigs(Connection conn)
      throws SQLException {
    Map<String, NodeInstanceConfig> nodeInstanceConfigs = new HashMap<>();
    try (ResultSet resultSet =
        conn.createStatement()
            .executeQuery(
                "SELECT node_instance.node_uuid as node_uuid, node_instance.instance_name as"
                    + " instance_name, node_details_json::jsonb->>'ip' as ip, provider.uuid as"
                    + " provider, pgp_sym_decrypt(provider.details,"
                    + " 'provider::details')::json->>'skipProvisioning' as manual from"
                    + " node_instance LEFT JOIN (availability_zone  INNER JOIN (region INNER JOIN"
                    + " provider ON region.provider_uuid = provider.uuid) ON"
                    + " availability_zone.region_uuid = region.uuid) ON node_instance.zone_uuid ="
                    + " availability_zone.uuid")) {
      while (resultSet.next()) {
        String instanceUuid = resultSet.getString("node_uuid");
        String instanceName = resultSet.getString("instance_name");
        String ip = resultSet.getString("ip");
        String provider = resultSet.getString("provider");
        String manual = resultSet.getString("manual");
        // Process only the on-prem manual nodes. Non-manual nodes are picked for node-agent
        // installation later in the tasks.
        if (StringUtils.isNotBlank(ip) && "true".equalsIgnoreCase(manual)) {
          NodeInstanceConfig nodeInstanceConfig = new NodeInstanceConfig();
          nodeInstanceConfig.instanceName = instanceName;
          nodeInstanceConfig.ip = ip;
          nodeInstanceConfig.provider = provider;
          nodeInstanceConfigs.put(instanceUuid, nodeInstanceConfig);
        }
      }
    }
    return nodeInstanceConfigs;
  }

  private Map<String, String> getNodeAgentStates(Connection conn) throws SQLException {
    Map<String, String> states = new HashMap<>();
    try (ResultSet resultSet =
        conn.createStatement().executeQuery("SELECT ip, state from node_agent")) {
      while (resultSet.next()) {
        states.put(resultSet.getString("ip"), resultSet.getString("state"));
      }
    }
    return states;
  }

  public boolean run(Path outputDir) {
    String dbUrl = config.getString(DATABASE_CONNECT_URL_PARAM);
    String dbUsername = config.getString(DATABASE_USERNAME_PARAM);
    String dbPassword = config.getString(DATABASE_PASSWORD_PARAM);
    try {
      Class.forName(config.getString(DATABASE_DRIVER_PARAM)).getDeclaredConstructor().newInstance();
    } catch (Exception e) {
      throw new RuntimeException("Failed to load database driver", e);
    }
    Map<String, String> nodeAgentClientRuntimeConfigs = null;
    Map<String, UniverseConfig> univConfigs = null;
    Map<String, NodeInstanceConfig> nodeInstanceConfigs = null;
    Map<String, String> nodeAgentStates = null;
    try (Connection conn = DriverManager.getConnection(dbUrl, dbUsername, dbPassword)) {
      nodeAgentClientRuntimeConfigs = getNodeAgentClientRuntimeConfigs(conn);
      univConfigs = getUniverseConfigs(conn);
      nodeInstanceConfigs = getNodeInstanceConfigs(conn);
      nodeAgentStates = getNodeAgentStates(conn);
    } catch (SQLException e) {
      throw new RuntimeException(e);
    }
    // For the precheck output.
    PrecheckOutput precheckOutput = new PrecheckOutput();
    // Check for any disabled runtime config override.
    for (Map.Entry<String, String> entry : nodeAgentClientRuntimeConfigs.entrySet()) {
      if ("false".equalsIgnoreCase(StringUtils.trim(entry.getValue()))) {
        precheckOutput.nodeAgentClientDisabled = true;
        precheckOutput.passed = false;
      }
    }
    // Check for node instances without node agents.
    for (Map.Entry<String, NodeInstanceConfig> entry : nodeInstanceConfigs.entrySet()) {
      String state = nodeAgentStates.get(entry.getValue().ip);
      if (state == null || !state.equalsIgnoreCase("READY")) {
        precheckOutput.nodeInstanceConfigs.put(entry.getKey(), entry.getValue());
        precheckOutput.passed = false;
      }
    }
    // Check for any universe node IPs without node agents.
    for (Map.Entry<String, UniverseConfig> entry : univConfigs.entrySet()) {
      UniverseConfig univConfig = entry.getValue();
      if (!univConfig.systemdEnabled) {
        precheckOutput.universeConfigs.computeIfAbsent(entry.getKey(), k -> new UniverseConfig())
                .systemdEnabled =
            univConfig.systemdEnabled;
        precheckOutput.passed = false;
      }
      for (String nodeIp : univConfig.nodeIps) {
        String state = nodeAgentStates.get(nodeIp);
        if (state == null || !state.equalsIgnoreCase("READY")) {
          precheckOutput
              .universeConfigs
              .computeIfAbsent(
                  entry.getKey(),
                  k -> {
                    UniverseConfig config = new UniverseConfig();
                    config.systemdEnabled = univConfig.systemdEnabled;
                    return config;
                  })
              .nodeIps
              .add(nodeIp);
          precheckOutput.passed = false;
        }
      }
    }
    try {
      log.info(
          "YBA upgrade precheck result: {}",
          mapper.writerWithDefaultPrettyPrinter().writeValueAsString(precheckOutput));
      // For dumping the data fetched from the DB.
      Map<String, Object> dumpOutputMap =
          Map.of(
              "nodeAgentClientRuntimeConfigs",
              nodeAgentClientRuntimeConfigs,
              "universeConfigs",
              univConfigs,
              "nodeInstanceConfigs",
              nodeInstanceConfigs,
              "nodeAgentStates",
              nodeAgentStates);
      Path dumpFilepath = outputDir.resolve("precheck_dumps.json");
      Path precheckOutputPath = outputDir.resolve("precheck_output.json");
      mapper.writerWithDefaultPrettyPrinter().writeValue(dumpFilepath.toFile(), dumpOutputMap);
      mapper
          .writerWithDefaultPrettyPrinter()
          .writeValue(precheckOutputPath.toFile(), precheckOutput);
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
    return precheckOutput.passed;
  }
}
