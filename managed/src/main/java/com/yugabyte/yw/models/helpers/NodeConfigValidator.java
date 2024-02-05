// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.NodeAgentClient;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.common.ShellProcessHandler;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.config.ConfKeyInfo;
import com.yugabyte.yw.common.config.ProviderConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.utils.NaturalOrderComparator;
import com.yugabyte.yw.forms.NodeInstanceFormData.NodeInstanceData;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.AccessKey.KeyInfo;
import com.yugabyte.yw.models.AccessKey.MigratedKeyInfoFields;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.helpers.NodeConfig.Operation;
import com.yugabyte.yw.models.helpers.NodeConfig.Type;
import com.yugabyte.yw.models.helpers.NodeConfig.ValidationResult;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.function.Function;
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

  private final ShellProcessHandler shellProcessHandler;

  private final NodeAgentClient nodeAgentClient;

  @Inject
  public NodeConfigValidator(
      RuntimeConfigFactory runtimeConfigFactory,
      ShellProcessHandler shellProcessHandler,
      NodeAgentClient nodeAgentClient) {
    this.runtimeConfigFactory = runtimeConfigFactory;
    this.shellProcessHandler = shellProcessHandler;
    this.nodeAgentClient = nodeAgentClient;
  }

  @Builder
  @Getter
  // Placeholder key for retrieving a config value.
  private static class ConfigKey {
    private final String path;
    private final Provider provider;

    @Override
    public String toString() {
      return String.format(
          "%s(path=%s, provider=%s)", getClass().getSimpleName(), path, provider.getUuid());
    }
  }

  @Builder
  @Getter
  // Placeholder key for passing validation data.
  private static class ValidationData {
    private final AccessKey accessKey;
    private final InstanceType instanceType;
    private final Provider provider;
    private final NodeConfig nodeConfig;
    private final NodeInstanceData nodeInstanceData;
    private final Operation operation;
    private final boolean isDetached;
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
   * Validates the node configs for an existing node instance.
   *
   * @param provider the provider.
   * @param nodeInstanceUuid the node instance UUID.
   * @param nodeConfigs the node configs to be validated.
   * @param isDetached true if the validation is not tied to a universe.
   * @return a map of config type to validation result.
   */
  public Map<Type, ValidationResult> validateNodeConfigs(
      Provider provider, UUID nodeInstanceUuid, Set<NodeConfig> nodeConfigs, boolean isDetached) {
    NodeInstance nodeInstance = NodeInstance.getOrBadRequest(nodeInstanceUuid);
    NodeInstanceData instanceData = nodeInstance.getDetails();
    instanceData.nodeConfigs = nodeConfigs;
    return validateNodeConfigs(provider, instanceData, isDetached);
  }

  /**
   * Validates the node configs in the instance data.
   *
   * @param provider the provider.
   * @param nodeData the node instance data.
   * @param isDetached true if the validation is not tied to a universe.
   * @return a map of config type to validation result.
   */
  public Map<Type, ValidationResult> validateNodeConfigs(
      Provider provider, NodeInstanceData nodeData, boolean isDetached) {
    InstanceType instanceType =
        InstanceType.getOrBadRequest(provider.getUuid(), nodeData.instanceType);
    AccessKey accessKey = AccessKey.getLatestKey(provider.getUuid());
    Operation operation =
        provider.getDetails().skipProvisioning ? Operation.CONFIGURE : Operation.PROVISION;

    Set<NodeConfig> nodeConfigs =
        nodeData.nodeConfigs == null ? new HashSet<>() : new HashSet<>(nodeData.nodeConfigs);
    ImmutableMap.Builder<Type, ValidationResult> resultsBuilder = ImmutableMap.builder();

    boolean canConnect = sshIntoNode(provider, nodeData, operation);
    nodeConfigs.add(new NodeConfig(Type.SSH_ACCESS, String.valueOf(canConnect)));

    if (operation == Operation.CONFIGURE && nodeAgentClient.isClientEnabled(provider)) {
      canConnect = connectToNodeAgent(provider, nodeData, operation);
      nodeConfigs.add(new NodeConfig(Type.NODE_AGENT_ACCESS, String.valueOf(canConnect)));
    }

    for (NodeConfig nodeConfig : nodeConfigs) {
      ValidationData input =
          ValidationData.builder()
              .accessKey(accessKey)
              .instanceType(instanceType)
              .provider(provider)
              .nodeConfig(nodeConfig)
              .nodeInstanceData(nodeData)
              .operation(operation)
              .isDetached(isDetached)
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
    MigratedKeyInfoFields keyInfo = provider.getDetails();
    switch (type) {
      case PROMETHEUS_SPACE:
        {
          int value =
              getFromConfig(CONFIG_INT_SUPPLIER, provider, ProviderConfKeys.minPrometheusSpaceMb);
          return Integer.parseInt(nodeConfig.getValue()) >= value;
        }
      case USER:
        {
          String value = getFromConfig(CONFIG_STRING_SUPPLIER, provider, ProviderConfKeys.user);
          return nodeConfig.getValue().equalsIgnoreCase(value);
        }
      case USER_GROUP:
        {
          String value =
              getFromConfig(CONFIG_STRING_SUPPLIER, provider, ProviderConfKeys.userGroup);
          return nodeConfig.getValue().equalsIgnoreCase(value);
        }
      case HOME_DIR_SPACE:
        {
          int value =
              getFromConfig(CONFIG_INT_SUPPLIER, provider, ProviderConfKeys.minHomeDirSpaceMb);
          return Integer.parseInt(nodeConfig.getValue()) >= value;
        }
      case RAM_SIZE:
        {
          return Double.compare(
                  Double.parseDouble(nodeConfig.getValue()), instanceType.getMemSizeGB() * 1024)
              >= 0;
        }
      case CPU_CORES:
        {
          return Double.compare(
                  Double.parseDouble(nodeConfig.getValue()), instanceType.getNumCores())
              >= 0;
        }
      case TMP_DIR_SPACE:
        {
          int value =
              getFromConfig(CONFIG_INT_SUPPLIER, provider, ProviderConfKeys.minTempDirSpaceMb);
          return Integer.parseInt(nodeConfig.getValue()) >= value;
        }
      case PAM_LIMITS_WRITABLE:
        {
          return !Boolean.parseBoolean(nodeConfig.getValue());
        }
      case PYTHON_VERSION:
        {
          String minValue =
              getFromConfig(CONFIG_STRING_SUPPLIER, provider, ProviderConfKeys.minPyVer);
          String maxValue =
              getFromConfig(CONFIG_STRING_SUPPLIER, provider, ProviderConfKeys.maxPyVer);
          NaturalOrderComparator comparator = new NaturalOrderComparator();
          return comparator.compare(nodeConfig.getValue(), minValue) >= 0
              && comparator.compare(nodeConfig.getValue(), maxValue) < 0;
        }
      case CHRONYD_RUNNING:
        {
          return Boolean.parseBoolean(nodeConfig.getValue()) == !keyInfo.setUpChrony;
        }
      case MOUNT_POINTS_VOLUME:
        {
          int value =
              getFromConfig(
                  CONFIG_INT_SUPPLIER, provider, ProviderConfKeys.minMountPointDirSpaceMb);
          return checkJsonFieldsGreaterEquals(nodeConfig.getValue(), value);
        }
      case ULIMIT_CORE:
        {
          String value =
              getFromConfig(CONFIG_STRING_SUPPLIER, provider, ProviderConfKeys.ulimitCore);
          return "unlimited".equalsIgnoreCase(value)
              || new NaturalOrderComparator().compare(nodeConfig.getValue(), value) >= 0;
        }
      case ULIMIT_OPEN_FILES:
        {
          String value =
              getFromConfig(CONFIG_STRING_SUPPLIER, provider, ProviderConfKeys.ulimitOpenFiles);
          return "unlimited".equalsIgnoreCase(value)
              || new NaturalOrderComparator().compare(nodeConfig.getValue(), value) >= 0;
        }
      case ULIMIT_USER_PROCESSES:
        {
          String value =
              getFromConfig(CONFIG_STRING_SUPPLIER, provider, ProviderConfKeys.ulimitUserProcesses);
          return "unlimited".equalsIgnoreCase(value)
              || new NaturalOrderComparator().compare(nodeConfig.getValue(), value) >= 0;
        }

      case SWAPPINESS:
        {
          int value = getFromConfig(CONFIG_INT_SUPPLIER, provider, ProviderConfKeys.swappiness);
          return Integer.parseInt(nodeConfig.getValue()) == value;
        }
      case VM_MAX_MAP_COUNT:
        {
          int value = getFromConfig(CONFIG_INT_SUPPLIER, provider, ProviderConfKeys.vmMaxMemCount);
          return Integer.parseInt(nodeConfig.getValue()) >= value;
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
      case YCQL_SERVER_HTTP_PORT:
      case YCQL_SERVER_RPC_PORT:
      case YSQL_SERVER_HTTP_PORT:
      case YSQL_SERVER_RPC_PORT:
      case SSH_PORT:
      case NODE_EXPORTER_PORT:
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
      case CHRONYC:
      case GSUTIL:
      case S3CMD:
        {
          return Boolean.parseBoolean(nodeConfig.getValue());
        }
      default:
        return true;
    }
  }

  private boolean isNodeConfigRequired(ValidationData input) {
    MigratedKeyInfoFields keyInfo = input.getProvider().getDetails();
    Provider provider = input.getProvider();
    NodeConfig.Type type = input.nodeConfig.getType();
    switch (type) {
      case PROMETHEUS_NO_NODE_EXPORTER:
        {
          return keyInfo.installNodeExporter;
        }
      case NTP_SERVICE_STATUS:
        {
          return input.getOperation() == Operation.CONFIGURE
              || CollectionUtils.isNotEmpty(keyInfo.ntpServers);
        }
      case SSH_ACCESS:
        {
          return input.getOperation() == Operation.PROVISION
              || !nodeAgentClient.isClientEnabled(provider);
        }
      case NODE_AGENT_ACCESS:
        {
          return input.getOperation() == Operation.CONFIGURE
              && nodeAgentClient.isClientEnabled(provider);
        }
      case VM_MAX_MAP_COUNT:
        {
          return input.getOperation() == Operation.CONFIGURE;
        }
      case CHRONYC:
        {
          return input.getOperation() == Operation.CONFIGURE;
        }
      case CHRONYD_RUNNING:
      case RSYNC:
      case XXHASH:
      case AZCOPY:
      case GSUTIL:
      case S3CMD:
      case SWAPPINESS:
      case LOCALE_PRESENT:
      case SYSTEMD_SUDOER_ENTRY:
        {
          return false;
        }
      case MASTER_HTTP_PORT:
      case MASTER_RPC_PORT:
      case TSERVER_HTTP_PORT:
      case TSERVER_RPC_PORT:
      case REDIS_SERVER_HTTP_PORT:
      case REDIS_SERVER_RPC_PORT:
      case YCQL_SERVER_HTTP_PORT:
      case YCQL_SERVER_RPC_PORT:
      case YSQL_SERVER_HTTP_PORT:
      case YSQL_SERVER_RPC_PORT:
        {
          return !input.isDetached();
        }
      case YB_CONTROLLER_HTTP_PORT:
      case YB_CONTROLLER_RPC_PORT:
        {
          // TODO change this to !input.isDetached() once the issue of not cleaning up yb_controller
          // is fixed.
          return false;
        }
      case NODE_EXPORTER_PORT:
        {
          return input.getOperation() == Operation.PROVISION
              && provider.getDetails().isInstallNodeExporter();
        }
      default:
        return true;
    }
  }

  private <T> T getFromConfig(
      Function<ConfigKey, T> function, Provider provider, ConfKeyInfo<T> key) {
    ConfigKey configKey = ConfigKey.builder().provider(provider).path(key.getKey()).build();
    T value = function.apply(configKey);
    log.debug("Value for {}: {}", configKey, value);
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
    AccessKey accessKey = AccessKey.getLatestKey(provider.getUuid());
    KeyInfo keyInfo = accessKey.getKeyInfo();
    String sshUser = operation == Operation.CONFIGURE ? "yugabyte" : provider.getDetails().sshUser;
    List<String> commandList =
        ImmutableList.of(
            "ssh",
            "-i",
            keyInfo.privateKey,
            "-oStrictHostKeyChecking=no",
            "-p",
            Integer.toString(provider.getDetails().sshPort),
            String.format("%s@%s", sshUser, nodeData.ip),
            "exit");

    ShellProcessContext shellProcessContext =
        ShellProcessContext.builder()
            .logCmdOutput(true)
            .timeoutSecs(getFromConfig(CONFIG_INT_SUPPLIER, provider, ProviderConfKeys.sshTimeout))
            .build();
    ShellResponse response = shellProcessHandler.run(commandList, shellProcessContext);

    return response.isSuccess();
  }

  public boolean connectToNodeAgent(
      Provider provider, NodeInstanceData nodeData, Operation operation) {
    Optional<NodeAgent> optional = NodeAgent.maybeGetByIp(nodeData.ip);
    if (optional.isPresent()) {
      NodeAgent nodeAgent = optional.get();
      try {
        nodeAgentClient.ping(nodeAgent);
        return true;
      } catch (RuntimeException e) {
        log.error("Failed to connect to node agent {} - {}", nodeAgent.getUuid(), e.getMessage());
      }
    }
    return false;
  }
}
