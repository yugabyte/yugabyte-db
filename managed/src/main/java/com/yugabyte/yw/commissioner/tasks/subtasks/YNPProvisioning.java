// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.CloudQueryHelper;
import com.yugabyte.yw.common.FileHelperService;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.CustomerConfKeys;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.ProviderConfKeys;
import com.yugabyte.yw.forms.AdditionalServicesStateData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.ImageBundle;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.StandardOpenOption;
import java.time.Instant;
import java.util.List;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class YNPProvisioning extends AbstractTaskBase {
  private final CloudQueryHelper queryHelper;
  private final FileHelperService fileHelperService;
  private ShellProcessContext shellContext =
      ShellProcessContext.builder().logCmdOutput(true).build();

  @Inject
  protected YNPProvisioning(
      BaseTaskDependencies baseTaskDependencies,
      CloudQueryHelper queryHelper,
      FileHelperService fileHelperService) {
    super(baseTaskDependencies);
    this.queryHelper = queryHelper;
    this.fileHelperService = fileHelperService;
  }

  public static class Params extends NodeTaskParams {
    public String sshUser;
    public UUID customerUuid;
    public String nodeAgentInstallDir;
    public boolean isYbPrebuiltImage;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  public void generateProvisionConfig(
      Universe universe,
      NodeDetails node,
      Provider provider,
      String outputFilePath,
      Path nodeAgentHome) {

    ObjectMapper mapper = new ObjectMapper();
    UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;

    try {
      ObjectNode rootNode = mapper.createObjectNode();

      // "ynp" JSON Object
      ObjectNode ynpNode = mapper.createObjectNode();
      ynpNode.put("node_ip", node.cloudInfo.private_ip);
      ynpNode.put("is_install_node_agent", false);
      ynpNode.put("yb_user_id", "1994");
      ynpNode.put("is_airgap", provider.getDetails().airGapInstall);
      ynpNode.put("is_yb_prebuilt_image", taskParams().isYbPrebuiltImage);
      ynpNode.put("is_ybcontroller_disabled", !universe.getUniverseDetails().isEnableYbc());
      ynpNode.put(
          "tmp_directory",
          confGetter.getConfForScope(provider, ProviderConfKeys.remoteTmpDirectory));
      ynpNode.put("is_configure_clockbound", userIntent.isUseClockbound());
      setCommunicationPorts(ynpNode, universe.getUniverseDetails().communicationPorts);
      if (!provider.getYbHome().isEmpty()) {
        ynpNode.put("yb_home_dir", provider.getYbHome());
      }
      Customer customer = Customer.getOrBadRequest(provider.getCustomerUUID());
      boolean enableEarlyoomFeature =
          confGetter.getConfForScope(customer, CustomerConfKeys.enableEarlyoomFeature);
      if (enableEarlyoomFeature) {
        AdditionalServicesStateData data =
            universe.getUniverseDetails().additionalServicesStateData;
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
      if (provider.getDetails().getNtpServers() != null
          && !provider.getDetails().getNtpServers().isEmpty()) {
        ArrayNode arrayNode = mapper.createArrayNode();
        List<String> ntpServers = provider.getDetails().getNtpServers();
        for (int i = 0; i < ntpServers.size(); i++) {
          arrayNode.add(ntpServers.get(i));
        }
        ynpNode.set("chrony_servers", arrayNode);
      }
      rootNode.set("ynp", ynpNode);

      // "extra" JSON Object
      ObjectNode extraNode = mapper.createObjectNode();
      extraNode.put("cloud_type", node.cloudInfo.cloud);
      extraNode.put("is_cloud", true);

      // Set mount paths
      if (taskParams().deviceInfo.mountPoints != null) {
        extraNode.put("mount_paths", taskParams().deviceInfo.mountPoints);
      } else {
        int numVolumes =
            universe
                .getCluster(node.placementUuid)
                .userIntent
                .getDeviceInfoForNode(node)
                .numVolumes;
        StringBuilder volumePaths = new StringBuilder();
        for (int i = 0; i < numVolumes; i++) {
          if (i > 0) {
            volumePaths.append(" ");
          }
          volumePaths.append("/mnt/d").append(i);
        }
        extraNode.put("mount_paths", volumePaths.toString());
      }

      // Azure-specific disk lun indexes
      if (node.cloudInfo.cloud.equals(Common.CloudType.azu.toString())
          && node.cloudInfo.lun_indexes.length > 0) {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < node.cloudInfo.lun_indexes.length; i++) {
          sb.append(node.cloudInfo.lun_indexes[i]);
          if (i < node.cloudInfo.lun_indexes.length - 1) {
            sb.append(" ");
          }
        }
        extraNode.put("disk_lun_indexes", sb.toString());
      }

      // Set package path
      String localPackagePath = nodeAgentHome.toString() + "/thirdparty";
      extraNode.put("package_path", localPackagePath);

      // Set device paths for cloud providers
      if (!provider.getCode().equals(CloudType.onprem.toString())) {
        String storageType = null;
        if (taskParams().deviceInfo.storageType != null) {
          storageType = taskParams().deviceInfo.storageType.toString().toLowerCase();
        }
        List<String> devicePaths =
            this.queryHelper.getDeviceNames(
                provider,
                Common.CloudType.valueOf(node.cloudInfo.cloud),
                Integer.toString(taskParams().deviceInfo.numVolumes),
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
              Util.retreiveImageBundleUUID(
                  universe.getUniverseDetails().arch, userIntent, provider);
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
      rootNode.set("extra", extraNode);

      // "logging" JSON Object
      ObjectNode loggingNode = mapper.createObjectNode();
      loggingNode.put("level", "INFO");
      loggingNode.put("directory", nodeAgentHome.resolve("logs").toString());
      rootNode.set("logging", loggingNode);

      // Convert to JSON string
      String jsonString = mapper.writerWithDefaultPrettyPrinter().writeValueAsString(rootNode);
      // Log JSON for debugging
      log.debug("Generated JSON:\n{}", jsonString);

      // Write to file with proper truncation
      Path outputPath = Paths.get(outputFilePath);
      Files.write(
          outputPath,
          jsonString.getBytes(StandardCharsets.UTF_8),
          StandardOpenOption.CREATE,
          StandardOpenOption.TRUNCATE_EXISTING);
    } catch (Exception e) {
      log.error("Failed generating JSON file: ", e);
    }
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    NodeDetails node = universe.getNodeOrBadRequest(taskParams().nodeName);
    if (taskParams().sshUser != null) {
      shellContext = shellContext.toBuilder().sshUser(taskParams().sshUser).build();
    }
    Path nodeAgentHomePath = Paths.get(taskParams().nodeAgentInstallDir, NodeAgent.NODE_AGENT_DIR);
    Path nodeAgentScriptsPath = nodeAgentHomePath.resolve("scripts");

    Provider provider =
        Provider.getOrBadRequest(
            UUID.fromString(universe.getCluster(node.placementUuid).userIntent.provider));
    boolean disableGolangYnpDriver =
        confGetter.getGlobalConf(GlobalConfKeys.disableGolangYnpDriver);
    String customTmpDirectory =
        confGetter.getConfForScope(provider, ProviderConfKeys.remoteTmpDirectory);
    String targetConfigPath =
        Paths.get(
                customTmpDirectory, String.format("config_%d.json", Instant.now().getEpochSecond()))
            .toString();
    String tmpDirectory =
        fileHelperService.createTempFile(node.cloudInfo.private_ip + "-", ".json").toString();
    generateProvisionConfig(universe, node, provider, tmpDirectory, nodeAgentHomePath);
    nodeUniverseManager.uploadFileToNode(
        node, universe, tmpDirectory, targetConfigPath, "755", shellContext);
    // Copy the conf file to scripts folder and run the provisioning script as in manual onprem.
    StringBuilder sb = new StringBuilder();
    sb.append("cd ").append(nodeAgentScriptsPath);
    sb.append(" && mv -f ").append(targetConfigPath);
    sb.append(" config.json && chmod +x node-agent-provision.sh");
    sb.append(" && ./node-agent-provision.sh --extra_vars config.json");
    if (disableGolangYnpDriver) {
      sb.append(" --use_python_driver");
    }
    sb.append(" --cloud_type ").append(node.cloudInfo.cloud);
    if (provider.getDetails().airGapInstall) {
      sb.append(" --is_airgap");
    }
    sb.append(" && chown -R $(id -u):$(id -g) ").append(nodeAgentHomePath);
    List<String> command = getCommand("/bin/bash", "-c", sb.toString());
    log.debug("Running YNP installation command: {}", command);
    nodeUniverseManager
        .runCommand(node, universe, command, shellContext)
        .processErrors("Installation failed");
  }

  private void setCommunicationPorts(
      ObjectNode ynpNode, UniverseTaskParams.CommunicationPorts ports) {
    // TODO consume all these ports in YNP.
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

  private List<String> getCommand(String... args) {
    ImmutableList.Builder<String> commandBuilder = ImmutableList.builder();
    return commandBuilder.add("sudo").add(args).build();
  }
}
