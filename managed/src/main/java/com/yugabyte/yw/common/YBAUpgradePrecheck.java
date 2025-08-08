// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.annotation.JsonInclude.Include;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.SerializationFeature;
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

  private final Config config;

  private final ObjectMapper mapper =
      new ObjectMapper()
          .configure(SerializationFeature.FAIL_ON_EMPTY_BEANS, false)
          .setSerializationInclusion(Include.ALWAYS);

  // This is the output to be serialized and dumped to a file.
  static class PrecheckOutput {
    @JsonProperty public boolean passed = true;
    @JsonProperty Set<String> disabledNodeAgentClients = new HashSet<>();
    @JsonProperty Set<String> nodeInstanceIps = new HashSet<>();
    @JsonProperty Map<String, UniverseConfig> universeConfigs = new HashMap<>();
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
                "SELECT path, value AS value FROM runtime_config_entry WHERE"
                    + " path = 'yb.node_agent.client.enabled'")) {
      while (resultSet.next()) {
        runtimeValues.put(
            resultSet.getString("path"),
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
        if (clusters != null && clusters.isArray()) {
          Iterator<JsonNode> iter = clusters.iterator();
          while (iter.hasNext()) {
            JsonNode cluster = iter.next();
            JsonNode userIntent = cluster.get("userIntent");
            if (userIntent == null || userIntent.isNull()) {
              continue;
            }
            JsonNode useSystemd = userIntent.get("useSystemd");
            if (useSystemd == null || useSystemd.isNull()) {
              continue;
            }
            univConfigs.computeIfAbsent(
                univUuid,
                k -> {
                  UniverseConfig univConfig = new UniverseConfig();
                  univConfig.systemdEnabled = useSystemd.asBoolean();
                  return univConfig;
                });
          }
        }
        Iterator<JsonNode> iter = nodeDetailsSet.iterator();
        while (iter.hasNext()) {
          JsonNode nodeDetails = iter.next();
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

  private Set<String> getNodeInstanceIps(Connection conn) throws SQLException {
    Set<String> ips = new HashSet<>();
    try (ResultSet resultSet =
        conn.createStatement()
            .executeQuery("SELECT node_details_json::jsonb->>'ip' as ip FROM node_instance")) {
      while (resultSet.next()) {
        String ip = resultSet.getString("ip");
        if (StringUtils.isNotBlank(ip)) {
          ips.add(ip);
        }
      }
    }
    return ips;
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
    Set<String> nodeInstanceIps = null;
    Map<String, String> nodeAgentStates = null;
    try (Connection conn = DriverManager.getConnection(dbUrl, dbUsername, dbPassword)) {
      nodeAgentClientRuntimeConfigs = getNodeAgentClientRuntimeConfigs(conn);
      univConfigs = getUniverseConfigs(conn);
      nodeInstanceIps = getNodeInstanceIps(conn);
      nodeAgentStates = getNodeAgentStates(conn);
    } catch (SQLException e) {
      throw new RuntimeException(e);
    }
    // For the precheck output.
    PrecheckOutput precheckOutput = new PrecheckOutput();
    // Check for any disabled runtime config override.
    for (Map.Entry<String, String> entry : nodeAgentClientRuntimeConfigs.entrySet()) {
      if ("false".equalsIgnoreCase(StringUtils.trim(entry.getValue()))) {
        precheckOutput.disabledNodeAgentClients.add(entry.getValue());
        precheckOutput.passed = false;
      }
    }
    // Check for node instances without node agents.
    for (String nodeInstanceIp : nodeInstanceIps) {
      String state = nodeAgentStates.get(nodeInstanceIp);
      if (state == null || !state.equalsIgnoreCase("READY")) {
        precheckOutput.nodeInstanceIps.add(nodeInstanceIp);
        precheckOutput.passed = false;
      }
    }
    // Check for any universe node IPs without node agents.
    for (Map.Entry<String, UniverseConfig> entry : univConfigs.entrySet()) {
      UniverseConfig univConfig = entry.getValue();
      if (!univConfig.systemdEnabled) {
        precheckOutput.universeConfigs.computeIfAbsent(
            entry.getKey(),
            k -> {
              UniverseConfig failedUnivConfig = new UniverseConfig();
              failedUnivConfig.systemdEnabled = univConfig.systemdEnabled;
              return failedUnivConfig;
            });
        precheckOutput.passed = false;
      }
      for (String nodeIp : univConfig.nodeIps) {
        String state = nodeAgentStates.get(nodeIp);
        if (state == null || !state.equalsIgnoreCase("READY")) {
          precheckOutput
              .universeConfigs
              .computeIfAbsent(entry.getKey(), k -> new UniverseConfig())
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
              "nodeInstanceIps",
              nodeInstanceIps,
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
