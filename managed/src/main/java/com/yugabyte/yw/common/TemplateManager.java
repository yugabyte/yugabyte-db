// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.ProviderDetails;
import java.io.File;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;

@Singleton
@Slf4j
public class TemplateManager extends DevopsBase {
  private static final String COMMAND_TYPE = "instance";
  public static final String PROVISION_SCRIPT = "provision_instance.py";

  @Inject Config appConfig;

  @Inject NodeAgentClient nodeAgentClient;

  @Override
  protected String getCommandType() {
    return COMMAND_TYPE;
  }

  private String getOrCreateProvisionFilePath(UUID providerUUID) {
    File provisionBasePathName = new File(appConfig.getString("yb.storage.path"), "/provision");
    if (!provisionBasePathName.exists() && !provisionBasePathName.mkdirs()) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR,
          "Provision path " + provisionBasePathName.getAbsolutePath() + " doesn't exists.");
    }
    File provisionFilePath =
        new File(provisionBasePathName.getAbsoluteFile(), providerUUID.toString());
    if (provisionFilePath.isDirectory() || provisionFilePath.mkdirs()) {
      return provisionFilePath.getAbsolutePath();
    }
    throw new PlatformServiceException(
        INTERNAL_SERVER_ERROR,
        "Unable to create provision file path " + provisionFilePath.getAbsolutePath());
  }

  public void createProvisionTemplate(
      AccessKey accessKey,
      boolean airGapInstall,
      boolean passwordlessSudoAccess,
      boolean installNodeExporter,
      Integer nodeExporterPort,
      String nodeExporterUser,
      boolean setUpChrony,
      List<String> ntpServers) {
    String path = getOrCreateProvisionFilePath(accessKey.getProviderUUID());
    AccessKey.KeyInfo keyInfo = accessKey.getKeyInfo();

    // Construct template command.
    List<String> commandArgs = new ArrayList<>();
    commandArgs.add("--name");
    commandArgs.add(PROVISION_SCRIPT);
    commandArgs.add("--destination");
    commandArgs.add(path);

    Provider provider = Provider.getOrBadRequest(accessKey.getProviderUUID());
    ProviderDetails details = provider.getDetails();

    if (details.sshUser == null) {
      log.warn(
          "skip provision script templating, provider {} has no ssh user defined",
          provider.getName());
      return;
    }
    commandArgs.add("--ssh_user");
    commandArgs.add(details.sshUser);

    commandArgs.add("--vars_file");
    commandArgs.add(keyInfo.vaultFile);
    commandArgs.add("--vault_password_file");
    commandArgs.add(keyInfo.vaultPasswordFile);
    commandArgs.add("--private_key_file");
    commandArgs.add(keyInfo.privateKey);
    commandArgs.add("--local_package_path");
    commandArgs.add(appConfig.getString("yb.thirdparty.packagePath"));

    commandArgs.add("--custom_ssh_port");
    commandArgs.add(details.sshPort.toString());
    commandArgs.add("--provider_id");
    commandArgs.add(provider.getUuid().toString());

    if (airGapInstall) {
      commandArgs.add("--air_gap");
    }

    if (passwordlessSudoAccess) {
      commandArgs.add("--passwordless_sudo");
    }

    if (installNodeExporter) {
      commandArgs.add("--install_node_exporter");
      commandArgs.add("--node_exporter_port");
      commandArgs.add(nodeExporterPort.toString());
      commandArgs.add("--node_exporter_user");
      commandArgs.add(nodeExporterUser);
    }

    if (setUpChrony) {
      commandArgs.add("--use_chrony");
      if (ntpServers != null && !ntpServers.isEmpty()) {
        for (String server : ntpServers) {
          commandArgs.add("--ntp_server");
          commandArgs.add(server);
        }
      }
    }

    if (nodeAgentClient.isClientEnabled(provider)) {
      commandArgs.add("--install_node_agent");
      commandArgs.add("--node_agent_port");
      commandArgs.add(String.valueOf(confGetter.getGlobalConf(GlobalConfKeys.nodeAgentServerPort)));
    }

    JsonNode result =
        execAndParseShellResponse(
            DevopsCommand.builder()
                .providerUUID(accessKey.getProviderUUID())
                .command("template")
                .commandArgs(commandArgs)
                .build());

    if (result.get("error") == null) {
      details.passwordlessSudoAccess = passwordlessSudoAccess;
      details.provisionInstanceScript = path + "/" + PROVISION_SCRIPT;
      details.airGapInstall = airGapInstall;
      details.installNodeExporter = installNodeExporter;
      details.nodeExporterPort = nodeExporterPort;
      details.nodeExporterUser = nodeExporterUser;
      details.setUpChrony = setUpChrony;
      details.ntpServers = ntpServers;
      provider.save();
    } else {
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, result);
    }
  }
}
