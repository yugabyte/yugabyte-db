// Copyright (c) Yugabyte, Inc.

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
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.ProviderConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.forms.AdditionalServicesStateData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
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
  private final NodeUniverseManager nodeUniverseManager;
  private final RuntimeConfGetter confGetter;
  private final CloudQueryHelper queryHelper;
  private final FileHelperService fileHelperService;
  private ShellProcessContext shellContext =
      ShellProcessContext.builder().logCmdOutput(true).build();

  @Inject
  protected YNPProvisioning(
      BaseTaskDependencies baseTaskDependencies,
      NodeUniverseManager nodeUniverseManager,
      RuntimeConfGetter confGetter,
      CloudQueryHelper queryHelper,
      FileHelperService fileHelperService) {
    super(baseTaskDependencies);
    this.nodeUniverseManager = nodeUniverseManager;
    this.confGetter = confGetter;
    this.queryHelper = queryHelper;
    this.fileHelperService = fileHelperService;
  }

  public static class Params extends NodeTaskParams {
    public String sshUser;
    public UUID customerUuid;
    public String nodeAgentInstallDir;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  public void getProvisionArguments(
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
      ynpNode.put(
          "tmp_directory",
          confGetter.getConfForScope(provider, ProviderConfKeys.remoteTmpDirectory));
      ynpNode.put("is_configure_clockbound", userIntent.isUseClockbound());
      AdditionalServicesStateData data = universe.getUniverseDetails().additionalServicesStateData;
      if (data != null
          && data.getEarlyoomConfig() != null
          && data.getEarlyoomConfig().isEnabled()) {
        ynpNode.put("earlyoom_enable", true);
        ynpNode.put("earlyoom_args", AdditionalServicesStateData.toArgs(data.getEarlyoomConfig()));
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
    Provider provider =
        Provider.getOrBadRequest(
            UUID.fromString(universe.getCluster(node.placementUuid).userIntent.provider));
    String customTmpDirectory =
        confGetter.getConfForScope(provider, ProviderConfKeys.remoteTmpDirectory);
    String targetConfigPath =
        Paths.get(
                customTmpDirectory, String.format("config_%d.json", Instant.now().getEpochSecond()))
            .toString();
    String tmpDirectory =
        fileHelperService.createTempFile(node.cloudInfo.private_ip + "-", ".json").toString();
    getProvisionArguments(universe, node, provider, tmpDirectory, nodeAgentHomePath);
    nodeUniverseManager.uploadFileToNode(
        node, universe, tmpDirectory, targetConfigPath, "755", shellContext);
    StringBuilder sb = new StringBuilder();
    sb.append("cd ").append(nodeAgentHomePath);
    sb.append(" && mv -f ").append(targetConfigPath);
    sb.append(" config.json && chmod +x node-agent-provision.sh");
    sb.append(" && ./node-agent-provision.sh --extra_vars config.json");
    sb.append(" --cloud_type ").append(node.cloudInfo.cloud);
    if (provider.getDetails().airGapInstall) {
      sb.append(" --is_airgap");
    }
    List<String> command = getCommand("/bin/bash", "-c", sb.toString());
    log.debug("Running YNP installation command: {}", command);
    nodeUniverseManager
        .runCommand(node, universe, command, shellContext)
        .processErrors("Installation failed");
  }

  private List<String> getCommand(String... args) {
    ImmutableList.Builder<String> commandBuilder = ImmutableList.builder();
    return commandBuilder.add("sudo").add(args).build();
  }
}
