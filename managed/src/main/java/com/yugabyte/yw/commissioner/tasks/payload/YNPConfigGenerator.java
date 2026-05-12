// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.payload;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.CloudQueryHelper;
import com.yugabyte.yw.common.FileHelperService;
import com.yugabyte.yw.common.ImageBundleUtil;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.CustomerConfKeys;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.ProviderConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.forms.AdditionalServicesStateData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.ImageBundle;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.util.List;
import java.util.Objects;
import java.util.UUID;
import javax.inject.Inject;
import lombok.Builder;
import lombok.Getter;
import lombok.NonNull;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.lang3.StringUtils;

/**
 * This class generates the configuration file for YugaByte Node Agent based on the provided
 * parameters and the data fetched from the database. It constructs a JSON configuration that YNP
 * can consume to perform operations on the node.
 */
@Slf4j
public class YNPConfigGenerator {
  private final RuntimeConfGetter confGetter;
  private final CloudQueryHelper queryHelper;
  private final ImageBundleUtil imageBundleUtil;
  private final FileHelperService fileHelperService;
  private final ObjectMapper mapper;

  /** Generator input parameters. Some fields are optional. */
  @Builder(toBuilder = true)
  @Getter
  public static class ConfigParams {
    @NonNull private Path nodeAgentHome;
    @NonNull private Provider provider;
    private NodeInstance nodeInstance;
    private NodeDetails nodeDetails;
    private Universe universe;
    private boolean isYbPrebuiltImage;
    private UserIntent userIntent;
  }

  @Inject
  public YNPConfigGenerator(
      RuntimeConfGetter confGetter,
      CloudQueryHelper queryHelper,
      ImageBundleUtil imageBundleUtil,
      FileHelperService fileHelperService) {
    this.confGetter = confGetter;
    this.queryHelper = queryHelper;
    this.imageBundleUtil = imageBundleUtil;
    this.fileHelperService = fileHelperService;
    this.mapper = new ObjectMapper();
  }

  private String getLogLevel() {
    int requestLogLevel = confGetter.getGlobalConf(GlobalConfKeys.nodeAgentServerRequestLogLevel);
    // This mapping is same as in node-agent server config.
    switch (requestLogLevel) {
      case 0:
        return "DEBUG";
      case 1:
        return "INFO";
      case 2:
        return "WARN";
      case 3:
        return "ERROR";
      default:
        // Default log level
        return "INFO";
    }
  }

  private static void setCommunicationPorts(
      ObjectNode ynpNode, UniverseTaskParams.CommunicationPorts ports) {
    ynpNode.put("master_http_port", String.valueOf(ports.masterHttpPort));
    ynpNode.put("master_rpc_port", String.valueOf(ports.masterRpcPort));
    ynpNode.put("tserver_http_port", String.valueOf(ports.tserverHttpPort));
    ynpNode.put("tserver_rpc_port", String.valueOf(ports.tserverRpcPort));
    ynpNode.put("yb_controller_http_port", String.valueOf(ports.ybControllerHttpPort));
    ynpNode.put("yb_controller_rpc_port", String.valueOf(ports.ybControllerrRpcPort));
    ynpNode.put("ycql_server_http_port", String.valueOf(ports.yqlServerHttpPort));
    ynpNode.put("ycql_server_rpc_port", String.valueOf(ports.yqlServerRpcPort));
    ynpNode.put("ysql_server_http_port", String.valueOf(ports.ysqlServerHttpPort));
    ynpNode.put("ysql_server_rpc_port", String.valueOf(ports.ysqlServerRpcPort));
    ynpNode.put("node_exporter_port", String.valueOf(ports.nodeExporterPort));
  }

  private void populateFromProvider(ConfigParams params, ObjectNode rootNode) {
    Provider provider = Objects.requireNonNull(params.getProvider());
    ObjectNode ynpNode = (ObjectNode) rootNode.get("ynp");
    ObjectNode extraNode = (ObjectNode) rootNode.get("extra");
    ynpNode.put(
        "tmp_directory", confGetter.getConfForScope(provider, ProviderConfKeys.remoteTmpDirectory));
    if (!provider.getYbHome().isEmpty()) {
      ynpNode.put("yb_home_dir", provider.getYbHome());
    }
    if (confGetter.getConfForScope(provider, ProviderConfKeys.useSystemLevelSystemd)) {
      ynpNode.put("use_system_level_systemd", true);
    }
    ynpNode.put("is_airgap", provider.getDetails().airGapInstall);
    ynpNode.put("check_available_ports", provider.isManualOnprem());
    ynpNode.put("check_clean_dirs", provider.isManualOnprem());
    ynpNode.put(
        "min_home_dir_space_gb",
        String.valueOf(confGetter.getConfForScope(provider, ProviderConfKeys.minHomeDirSpaceGb)));
    ynpNode.put(
        "min_mount_point_dir_space_gb",
        String.valueOf(
            confGetter.getConfForScope(provider, ProviderConfKeys.minMountPointDirSpaceGb)));
    ynpNode.put(
        "min_tmp_dir_space_gb",
        String.valueOf(confGetter.getConfForScope(provider, ProviderConfKeys.minTempDirSpaceGb)));
    ynpNode.put(
        "min_prometheus_space_gb",
        String.valueOf(confGetter.getConfForScope(provider, ProviderConfKeys.minHomeDirSpaceGb)));
    extraNode.put("is_cloud", !provider.isManualOnprem());
    extraNode.put("cloud_type", provider.getCode());
    ynpNode.put("configure_cgroup", Util.configureCgroup(provider, true, confGetter));
    // Set package path
    extraNode.put("package_path", params.getNodeAgentHome().resolve("thirdparty").toString());
    if (CollectionUtils.isNotEmpty(params.getProvider().getDetails().getNtpServers())) {
      ArrayNode arrayNode = mapper.createArrayNode();
      params.getProvider().getDetails().getNtpServers().forEach(s -> arrayNode.add(s));
      ynpNode.set("chrony_servers", arrayNode);
    }
  }

  private void populateFromNodeInstance(ConfigParams params, ObjectNode rootNode) {
    Provider provider = Objects.requireNonNull(params.getProvider());
    NodeInstance nodeInstance = Objects.requireNonNull(params.getNodeInstance());
    ObjectNode ynpNode = (ObjectNode) rootNode.get("ynp");
    ObjectNode extraNode = (ObjectNode) rootNode.get("extra");
    InstanceType instanceType =
        InstanceType.get(provider.getUuid(), nodeInstance.getInstanceTypeCode());
    ynpNode.put("node_ip", nodeInstance.getDetails().ip);
    extraNode.put(
        "mount_paths", instanceType.getInstanceTypeDetails().volumeDetailsList.get(0).mountPath);
    extraNode.put(
        "volume_size", instanceType.getInstanceTypeDetails().volumeDetailsList.get(0).volumeSizeGB);
  }

  private void populateFromNodeDetails(ConfigParams params, ObjectNode rootNode) {
    Provider provider = Objects.requireNonNull(params.getProvider());
    NodeDetails node = Objects.requireNonNull(params.getNodeDetails());
    ObjectNode ynpNode = (ObjectNode) rootNode.get("ynp");
    ObjectNode extraNode = (ObjectNode) rootNode.get("extra");
    Universe universe =
        Objects.requireNonNull(
            params.getUniverse(), "Universe must be provided if node details are provided");
    UserIntent userIntent = params.userIntent;
    if (userIntent == null) {
      userIntent =
          Objects.requireNonNull(
              universe.getCluster(node.placementUuid).userIntent, "User intent must be available");
    }
    if (node.cloudInfo.private_ip != null) {
      ynpNode.put("node_ip", node.cloudInfo.private_ip);
    }
    ynpNode.put("configure_cgroup", Util.configureCgroup(userIntent, provider, true, confGetter));
    DeviceInfo deviceInfo = userIntent.getDeviceInfoForNode(node);
    if (deviceInfo.mountPoints != null) {
      extraNode.put("mount_paths", deviceInfo.mountPoints);
    } else {
      StringBuilder volumePaths = new StringBuilder();
      for (int i = 0; i < deviceInfo.numVolumes; i++) {
        if (i > 0) {
          volumePaths.append(" ");
        }
        volumePaths.append("/mnt/d").append(i);
      }
      extraNode.put("mount_paths", volumePaths.toString());
    }
    if (userIntent.providerType == Common.CloudType.azu && node.cloudInfo.lun_indexes.length > 0) {
      StringBuilder sb = new StringBuilder();
      for (int i = 0; i < node.cloudInfo.lun_indexes.length; i++) {
        sb.append(node.cloudInfo.lun_indexes[i]);
        if (i < node.cloudInfo.lun_indexes.length - 1) {
          sb.append(" ");
        }
      }
      extraNode.put("disk_lun_indexes", sb.toString());
    }
    if (provider.getCloudCode() != CloudType.onprem) {
      // Set device paths for cloud providers
      String storageType = null;
      if (deviceInfo.storageType != null) {
        storageType = deviceInfo.storageType.toString().toLowerCase();
      }
      List<String> devicePaths =
          this.queryHelper.getDeviceNames(
              provider,
              userIntent.providerType,
              Integer.toString(deviceInfo.numVolumes),
              storageType,
              node.cloudInfo.region,
              node.cloudInfo.instance_type);
      extraNode.put("device_paths", String.join(" ", devicePaths));
    }
    if (provider.getCloudCode() != CloudType.onprem) {
      if (node.sshPortOverride != null && node.sshPortOverride != 22) {
        extraNode.put("custom_ssh_port", node.sshPortOverride);
      } else if (node.sshPortOverride == null) {
        UUID imageBundleUUID =
            Util.retreiveImageBundleUUID(universe.getUniverseDetails().arch, userIntent, provider);
        if (imageBundleUUID != null) {
          ImageBundle.NodeProperties overwriteProperties =
              imageBundleUtil.getNodePropertiesOrFail(
                  imageBundleUUID, node.getRegion(), userIntent.providerType.toString());
          if (overwriteProperties.getSshPort() != 22) {
            extraNode.put("custom_ssh_port", overwriteProperties.getSshPort());
          }
        }
      }
    }
  }

  private void populateFromUniverse(ConfigParams params, ObjectNode rootNode) {
    Universe universe = Objects.requireNonNull(params.getUniverse());
    ObjectNode ynpNode = (ObjectNode) rootNode.get("ynp");
    setCommunicationPorts(ynpNode, universe.getUniverseDetails().communicationPorts);
    ynpNode.put("is_ybcontroller_disabled", !universe.getUniverseDetails().isEnableYbc());
    ynpNode.put(
        "is_configure_clockbound",
        universe.getUniverseDetails().getPrimaryCluster().userIntent.isUseClockbound());
    Customer customer = Customer.getOrBadRequest(params.getProvider().getCustomerUUID());
    boolean enableEarlyoomFeature =
        confGetter.getConfForScope(customer, CustomerConfKeys.enableEarlyoomFeature);
    if (enableEarlyoomFeature) {
      AdditionalServicesStateData data = universe.getUniverseDetails().additionalServicesStateData;
      if (data != null) {
        ObjectNode earlyoomNode = mapper.createObjectNode();
        if (data.isEarlyoomEnabled()) {
          earlyoomNode.put("earlyoom_enable", true);
          earlyoomNode.put(
              "earlyoom_args", AdditionalServicesStateData.toArgs(data.getEarlyoomConfig()));
        }
        ynpNode.set("earlyoom", earlyoomNode);
      }
    }
  }

  /**
   * This method generates the YNP config as a JSON object based on the provided parameters.
   *
   * @param params the input config params.
   * @return the generated config as an ObjectNode.
   */
  public ObjectNode generateConfig(ConfigParams params) {
    // Create the required JSON nodes.
    ObjectNode rootNode = mapper.createObjectNode();
    ObjectNode ynpNode = mapper.createObjectNode();
    ObjectNode extraNode = mapper.createObjectNode();
    ObjectNode loggingNode = mapper.createObjectNode();
    // Field 'ynp' inserts key-value pairs inside 'ynp'section in the config.
    rootNode.set("ynp", ynpNode);
    // Field 'extra' inserts key-value pairs at the top-level in the config.
    rootNode.set("extra", extraNode);
    // Field 'logging' inserts key-value pairs inside 'logging' in the config.
    rootNode.set("logging", loggingNode);
    // Set up the common independent fields.
    ynpNode.put("is_install_node_agent", false);
    ynpNode.put("yb_user_id", "1994");
    ynpNode.put("is_yb_prebuilt_image", params.isYbPrebuiltImage());
    // Set the logging level based on the global config.
    loggingNode.put("level", getLogLevel());
    loggingNode.put("directory", params.getNodeAgentHome().resolve("logs").toString());
    String ybUserHomeOverride =
        confGetter
            .getConfForScope(params.getProvider(), ProviderConfKeys.ybUserHomeOverride)
            .trim();
    if (StringUtils.isNotEmpty(ybUserHomeOverride)) {
      log.info("Using yb_user_home override value from provider config: {}", ybUserHomeOverride);
      ynpNode.put("yb_user_home", ybUserHomeOverride);
    }
    // Set up provider specific fields.
    populateFromProvider(params, rootNode);
    if (params.getNodeInstance() != null) {
      // Set up node instance specific fields.
      populateFromNodeInstance(params, rootNode);
    }
    if (params.getNodeDetails() != null) {
      // Set up node details specific fields.
      populateFromNodeDetails(params, rootNode);
    }
    if (params.getUniverse() != null) {
      // Set up node universe specific fields.
      populateFromUniverse(params, rootNode);
    }
    return rootNode;
  }

  /**
   * This generates the YNP config file and writes it to a temporary location.
   *
   * @param params the input config params.
   * @return the path to the generated config file.
   */
  public Path generateConfigFile(ConfigParams params) {
    ObjectNode rootNode = generateConfig(params);
    ObjectNode ynpNode = (ObjectNode) rootNode.get("ynp");
    try {
      Path tmpConfigFilepath =
          fileHelperService.createTempFile(ynpNode.get("node_ip").asText() + "-", ".json");
      // Convert to JSON string
      String jsonString = mapper.writerWithDefaultPrettyPrinter().writeValueAsString(rootNode);
      // Log JSON for debugging
      log.debug("Generated JSON:\n{}", jsonString);
      // Write to file with proper truncation
      Files.write(
          tmpConfigFilepath,
          jsonString.getBytes(StandardCharsets.UTF_8),
          StandardOpenOption.CREATE,
          StandardOpenOption.TRUNCATE_EXISTING);
      return tmpConfigFilepath;
    } catch (IOException e) {
      log.error("Failed generating JSON file: ", e);
      throw new RuntimeException(e);
    }
  }
}
