// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.common.ShellProcessHandler;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.utils.NaturalOrderComparator;
import com.yugabyte.yw.forms.NodeInstanceFormData.NodeInstanceData;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.AccessKey.KeyInfo;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.helpers.NodeConfig.Operation;
import com.yugabyte.yw.models.helpers.NodeConfig.Type;
import com.yugabyte.yw.models.helpers.NodeConfig.ValidationResult;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.function.Function;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.Builder;
import lombok.Getter;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections.CollectionUtils;
import org.apache.commons.lang3.StringUtils;
import play.libs.Json;

@Singleton
@Slf4j
public class NodeConfigValidator {
  public static final String CONFIG_KEY_FORMAT = "yb.node_agent.preflight_checks.%s";

  private RuntimeConfigFactory runtimeConfigFactory;

  private final ShellProcessHandler shellProcessHandler;

  @Inject
  public NodeConfigValidator(
      RuntimeConfigFactory runtimeConfigFactory, ShellProcessHandler shellProcessHandler) {
    this.runtimeConfigFactory = runtimeConfigFactory;
    this.shellProcessHandler = shellProcessHandler;
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
    private Operation operation;
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
    KeyInfo keyInfo = accessKey.getKeyInfo();
    Operation operation =
        accessKey.getKeyInfo().skipProvisioning ? Operation.CONFIGURE : Operation.PROVISION;

    Set<NodeConfig> nodeConfigs =
        nodeData.nodeConfigs == null ? new HashSet<>() : new HashSet<>(nodeData.nodeConfigs);
    ImmutableMap.Builder<Type, ValidationResult> resultsBuilder = ImmutableMap.builder();

    boolean canSshNode = sshIntoNode(provider, nodeData, operation);
    nodeConfigs.add(new NodeConfig(Type.SSH_ACCESS, String.valueOf(canSshNode)));

    for (NodeConfig nodeConfig : nodeConfigs) {
      ValidationData input =
          ValidationData.builder()
              .accessKey(accessKey)
              .instanceType(instanceType)
              .provider(provider)
              .nodeConfig(nodeConfig)
              .nodeInstanceData(nodeData)
              .operation(operation)
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
    AccessKey accessKey = input.getAccessKey();
    KeyInfo keyInfo = accessKey.getKeyInfo();
    switch (type) {
      case PROMETHEUS_SPACE:
        {
          int value = getFromConfig(CONFIG_INT_SUPPLIER, provider, "min_prometheus_space_mb");
          return Integer.parseInt(nodeConfig.getValue()) >= value;
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
      case CPU_CORES:
        {
          return Double.compare(Double.parseDouble(nodeConfig.getValue()), instanceType.numCores)
              >= 0;
        }
      case TMP_DIR_SPACE:
        {
          int value = getFromConfig(CONFIG_INT_SUPPLIER, provider, "min_tmp_dir_space_mb");
          return Integer.parseInt(nodeConfig.getValue()) >= value;
        }
      case PAM_LIMITS_WRITABLE:
        {
          return Boolean.valueOf(nodeConfig.getValue()) == false;
        }
      case PYTHON_VERSION:
        {
          String value = getFromConfig(CONFIG_STRING_SUPPLIER, provider, "min_python_version");
          return new NaturalOrderComparator().compare(nodeConfig.getValue(), value) >= 0;
        }
      case CHRONYD_RUNNING:
        {
          return Boolean.valueOf(nodeConfig.getValue()) == !keyInfo.setUpChrony;
        }
      case MOUNT_POINTS_VOLUME:
        {
          int value = getFromConfig(CONFIG_INT_SUPPLIER, provider, "min_mount_point_dir_space_mb");
          return checkJsonFieldsGreaterEquals(nodeConfig.getValue(), value);
        }
      case NODE_EXPORTER_PORT:
        {
          return Integer.parseInt(nodeConfig.getValue()) == keyInfo.nodeExporterPort;
        }
      case ULIMIT_CORE:
        {
          String value = getFromConfig(CONFIG_STRING_SUPPLIER, provider, "ulimit_core");
          return new NaturalOrderComparator().compare(nodeConfig.getValue(), value) >= 0;
        }
      case ULIMIT_OPEN_FILES:
        {
          String value = getFromConfig(CONFIG_STRING_SUPPLIER, provider, "ulimit_open_files");
          return new NaturalOrderComparator().compare(nodeConfig.getValue(), value) >= 0;
        }
      case ULIMIT_USER_PROCESSES:
        {
          String value = getFromConfig(CONFIG_STRING_SUPPLIER, provider, "ulimit_user_processes");
          return new NaturalOrderComparator().compare(nodeConfig.getValue(), value) >= 0;
        }

      case SWAPPINESS:
        {
          int value = getFromConfig(CONFIG_INT_SUPPLIER, provider, "swappiness");
          return Integer.parseInt(nodeConfig.getValue()) == value;
        }
      case MOUNT_POINTS_WRITABLE:
      case MASTER_HTTP_PORT:
      case MASTER_RPC_PORT:
      case TSERVER_HTTP_PORT:
      case TSERVER_RPC_PORT:
      case YB_CONTROLLER_HTTP_PORT:
      case YB_CONTROLLER_RPC_PORT:
      case REDIS_SERVER_HTTP_PORT:
      case REDIS_SERVER_RPC_PORT:
      case YQL_SERVER_HTTP_PORT:
      case YQL_SERVER_RPC_PORT:
      case YSQL_SERVER_HTTP_PORT:
      case YSQL_SERVER_RPC_PORT:
      case SSH_PORT:
        {
          return checkJsonFieldsEqual(nodeConfig.getValue(), "true");
        }
      case NTP_SERVICE_STATUS:
      case INTERNET_CONNECTION:
      case PROMETHEUS_NO_NODE_EXPORTER:
      case HOME_DIR_EXISTS:
      case NODE_EXPORTER_RUNNING:
      case SYSTEMD_SUDOER_ENTRY:
      case SUDO_ACCESS:
      case SSH_ACCESS:
      case OPENSSL:
      case POLICYCOREUTILS:
      case RSYNC:
      case XXHASH:
      case LIBATOMIC1:
      case LIBNCURSES6:
      case LIBATOMIC:
      case AZCOPY:
      case GSUTIL:
      case S3CMD:
        {
          return Boolean.valueOf(nodeConfig.getValue()) == true;
        }
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
          return input.getOperation() == Operation.CONFIGURE
              ? true
              : CollectionUtils.isNotEmpty(keyInfo.ntpServers);
        }
      case CHRONYD_RUNNING:
      case RSYNC:
      case XXHASH:
      case AZCOPY:
      case GSUTIL:
      case S3CMD:
      case SWAPPINESS:
      case SYSTEMD_SUDOER_ENTRY:
      case MASTER_HTTP_PORT:
      case MASTER_RPC_PORT:
      case TSERVER_HTTP_PORT:
      case TSERVER_RPC_PORT:
      case YB_CONTROLLER_HTTP_PORT:
      case YB_CONTROLLER_RPC_PORT:
      case REDIS_SERVER_HTTP_PORT:
      case REDIS_SERVER_RPC_PORT:
      case YQL_SERVER_HTTP_PORT:
      case YQL_SERVER_RPC_PORT:
      case YSQL_SERVER_HTTP_PORT:
      case YSQL_SERVER_RPC_PORT:
        {
          return false;
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

  public boolean checkJsonFieldsGreaterEquals(String jsonStr, int expected) {
    try {
      JsonNode node = Json.parse(jsonStr);
      if (!node.isObject()) {
        return false;
      }
      ObjectNode object = (ObjectNode) node;
      Iterator<String> iter = object.fieldNames();
      while (iter.hasNext()) {
        String value = iter.next();
        if (Integer.parseInt(object.get(value).asText()) < expected) {
          return false;
        }
      }
    } catch (Exception e) {
      return false;
    }
    return true;
  }

  public boolean sshIntoNode(Provider provider, NodeInstanceData nodeData, Operation operation) {
    AccessKey accessKey = AccessKey.getLatestKey(provider.uuid);
    KeyInfo keyInfo = accessKey.getKeyInfo();
    String sshUser = operation == Operation.CONFIGURE ? "yugabyte" : keyInfo.sshUser;
    List<String> commandList =
        ImmutableList.of(
            "ssh",
            "-i",
            keyInfo.privateKey,
            "-p",
            Integer.toString(keyInfo.sshPort),
            String.format("%s@%s", sshUser, nodeData.ip),
            "exit");

    ShellProcessContext shellProcessContext =
        ShellProcessContext.builder()
            .logCmdOutput(true)
            .timeoutSecs(getFromConfig(CONFIG_INT_SUPPLIER, provider, "ssh_timeout"))
            .build();
    ShellResponse response = shellProcessHandler.run(commandList, shellProcessContext);

    return response.isSuccess();
  }
}
