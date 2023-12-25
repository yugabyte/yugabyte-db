// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableMap;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.utils.NaturalOrderComparator;
import com.yugabyte.yw.forms.NodeInstanceFormData.NodeInstanceData;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.AccessKey.KeyInfo;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.helpers.NodeConfig.Type;
import com.yugabyte.yw.models.helpers.NodeConfig.ValidationResult;
import java.util.EnumSet;
import java.util.HashSet;
import java.util.Iterator;
import java.util.Map;
import java.util.Set;
import java.util.function.Function;
import java.util.stream.Collectors;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.Builder;
import lombok.Getter;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.lang3.StringUtils;
import play.libs.Json;

@Singleton
@Slf4j
public class NodeConfigValidator {
  public static final String CONFIG_KEY_FORMAT = "yb.node_agent.preflight_checks.%s";

  private RuntimeConfigFactory runtimeConfigFactory;

  @Inject
  public NodeConfigValidator(RuntimeConfigFactory runtimeConfigFactory) {
    this.runtimeConfigFactory = runtimeConfigFactory;
  }

  @Builder
  @Getter
  // Placeholder key for retrieving a config value.
  private static class ConfigKey {
    private String path;
    private Provider provider;

    @Override
    public String toString() {
      return String.format(
          "%s(path=%s, provider=%s)", getClass().getSimpleName(), path, provider.uuid);
    }
  }

  @Builder
  @Getter
  // Placeholder key for passing validation data.
  private static class ValidationData {
    private AccessKey accessKey;
    private InstanceType instanceType;
    private Provider provider;
    private NodeConfig nodeConfig;
    private NodeInstanceData nodeInstanceData;
  }

  private final Function<Provider, Config> PROVIDER_CONFIG =
      provider -> runtimeConfigFactory.forProvider(provider);

  /** Supplier for an Integer value from application config file. */
  public final Function<ConfigKey, Integer> CONFIG_INT_SUPPLIER =
      key -> PROVIDER_CONFIG.apply(key.provider).getInt(key.path);

  /** Supplier for a String value from application config file. */
  public final Function<ConfigKey, String> CONFIG_STRING_SUPPLIER =
      key -> PROVIDER_CONFIG.apply(key.provider).getString(key.path);

  /** Supplier for a boolean value from application config file. */
  public final Function<ConfigKey, Boolean> CONFIG_BOOL_SUPPLIER =
      key -> PROVIDER_CONFIG.apply(key.provider).getBoolean(key.path);

  /**
   * Validates the node configs in the instance data.
   *
   * @param provider the provider.
   * @param nodeData the node instance data.
   * @return a map of config type to validation result.
   */
  public Map<Type, ValidationResult> validateNodeConfigs(
      Provider provider, NodeInstanceData nodeData) {
    InstanceType instanceType = InstanceType.getOrBadRequest(provider.uuid, nodeData.instanceType);
    AccessKey accessKey = AccessKey.getLatestKey(provider.uuid);
    Set<NodeConfig> mergedNodeConfigs =
        nodeData.nodeConfigs == null ? new HashSet<>() : new HashSet<>(nodeData.nodeConfigs);
    ImmutableMap.Builder<Type, ValidationResult> resultsBuilder = ImmutableMap.builder();
    Set<Type> inputTypes =
        mergedNodeConfigs.stream().map(NodeConfig::getType).collect(Collectors.toSet());
    EnumSet.allOf(Type.class)
        .stream()
        .filter(t -> !inputTypes.contains(t))
        .forEach(
            t -> {
              mergedNodeConfigs.add(new NodeConfig(t, null));
            });
    for (NodeConfig nodeConfig : mergedNodeConfigs) {
      ValidationData input =
          ValidationData.builder()
              .accessKey(accessKey)
              .instanceType(instanceType)
              .provider(provider)
              .nodeConfig(nodeConfig)
              .nodeInstanceData(nodeData)
              .build();
      boolean isValid = isNodeConfigValid(input);
      boolean isRequired = isNodeConfigRequired(input);
      resultsBuilder.put(
          nodeConfig.getType(),
          ValidationResult.builder()
              .type(nodeConfig.getType())
              .isValid(isValid)
              .isRequired(isRequired)
              .value(nodeConfig.getValue())
              .description(nodeConfig.getType().getDescription())
              .build());
    }
    return resultsBuilder.build();
  }

  private boolean isNodeConfigValid(ValidationData input) {
    NodeConfig nodeConfig = input.getNodeConfig();
    if (StringUtils.isBlank(nodeConfig.getValue())) {
      return false;
    }
    InstanceType instanceType = input.getInstanceType();
    Provider provider = input.getProvider();
    NodeConfig.Type type = input.nodeConfig.getType();
    switch (type) {
      case NTP_SERVICE_STATUS:
        {
          String value = getFromConfig(CONFIG_STRING_SUPPLIER, provider, "ntp_service");
          return nodeConfig.getValue().equalsIgnoreCase(value);
        }
      case PROMETHEUS_SPACE:
        {
          int value = getFromConfig(CONFIG_INT_SUPPLIER, provider, "min_prometheus_space_mb");
          return Integer.parseInt(nodeConfig.getValue()) >= value;
        }
      case MOUNT_POINTS:
        {
          String value = getFromConfig(CONFIG_STRING_SUPPLIER, provider, "mount_points");
          return checkJsonFieldsEqual(nodeConfig.getValue(), value);
        }
      case USER:
        {
          String value = getFromConfig(CONFIG_STRING_SUPPLIER, provider, "user");
          return nodeConfig.getValue().equalsIgnoreCase(value);
        }
      case USER_GROUP:
        {
          String value = getFromConfig(CONFIG_STRING_SUPPLIER, provider, "user_group");
          return nodeConfig.getValue().equalsIgnoreCase(value);
        }
      case HOME_DIR_SPACE:
        {
          int value = getFromConfig(CONFIG_INT_SUPPLIER, provider, "min_home_dir_space_mb");
          return Integer.parseInt(nodeConfig.getValue()) >= value;
        }
      case RAM_SIZE:
        {
          return Double.compare(
                  Double.parseDouble(nodeConfig.getValue()), instanceType.memSizeGB * 1024)
              >= 0;
        }
      case INTERNET_CONNECTION:
        {
          String value = getFromConfig(CONFIG_STRING_SUPPLIER, provider, "internet_connection");
          return nodeConfig.getValue().equalsIgnoreCase(value);
        }
      case CPU_CORES:
        {
          return Double.compare(Double.parseDouble(nodeConfig.getValue()), instanceType.numCores)
              >= 0;
        }
      case PROMETHEUS_NO_NODE_EXPORTER:
        {
          String value =
              getFromConfig(CONFIG_STRING_SUPPLIER, provider, "prometheus_no_node_exporter");
          return nodeConfig.getValue().equalsIgnoreCase(value);
        }
      case TMP_DIR_SPACE:
        {
          int value = getFromConfig(CONFIG_INT_SUPPLIER, provider, "min_tmp_dir_space_mb");
          return Integer.parseInt(nodeConfig.getValue()) >= value;
        }
      case PAM_LIMITS_WRITABLE:
        {
          String value = getFromConfig(CONFIG_STRING_SUPPLIER, provider, "pam_limits_writable");
          return nodeConfig.getValue().equalsIgnoreCase(value);
        }
      case PORTS:
        {
          String value = getFromConfig(CONFIG_STRING_SUPPLIER, provider, "ports");
          return checkJsonFieldsEqual(nodeConfig.getValue(), value);
        }
      case PYTHON_VERSION:
        String value = getFromConfig(CONFIG_STRING_SUPPLIER, provider, "min_python_version");
        return new NaturalOrderComparator().compare(nodeConfig.getValue(), value) >= 0;
      default:
        return true;
    }
  }

  private boolean isNodeConfigRequired(ValidationData input) {
    AccessKey accessKey = input.getAccessKey();
    KeyInfo keyInfo = accessKey.getKeyInfo();
    NodeConfig.Type type = input.nodeConfig.getType();
    switch (type) {
      case PROMETHEUS_NO_NODE_EXPORTER:
        {
          return keyInfo.installNodeExporter;
        }
      case NTP_SERVICE_STATUS:
        {
          return CollectionUtils.isNotEmpty(keyInfo.ntpServers);
        }
      default:
        return true;
    }
  }

  private <T> T getFromConfig(Function<ConfigKey, T> function, Provider provider, String key) {
    ConfigKey configKey =
        ConfigKey.builder().provider(provider).path(String.format(CONFIG_KEY_FORMAT, key)).build();
    T value = function.apply(configKey);
    log.trace("Value for {}: {}", configKey, value);
    return value;
  }

  private boolean checkJsonFieldsEqual(String jsonStr, String expected) {
    try {
      JsonNode node = Json.parse(jsonStr);
      if (!node.isObject()) {
        return false;
      }
      ObjectNode object = (ObjectNode) node;
      Iterator<String> iter = object.fieldNames();
      while (iter.hasNext()) {
        String value = iter.next();
        if (!expected.equalsIgnoreCase(object.get(value).asText())) {
          return false;
        }
      }
    } catch (Exception e) {
      return false;
    }
    return true;
  }
}
