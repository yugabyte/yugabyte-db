// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.models.AccessKey;
import java.io.File;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;

@Singleton
public class TemplateManager extends DevopsBase {
  private static final String COMMAND_TYPE = "instance";
  public static final String PROVISION_SCRIPT = "provision_instance.py";

  @Inject play.Configuration appConfig;

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
    AccessKey.KeyInfo keyInfo = accessKey.getKeyInfo();
    String path = getOrCreateProvisionFilePath(accessKey.getProviderUUID());

    // Construct template command.
    List<String> commandArgs = new ArrayList<>();
    commandArgs.add("--name");
    commandArgs.add(PROVISION_SCRIPT);
    commandArgs.add("--destination");
    commandArgs.add(path);
    commandArgs.add("--ssh_user");
    commandArgs.add(keyInfo.sshUser);
    commandArgs.add("--vars_file");
    commandArgs.add(keyInfo.vaultFile);
    commandArgs.add("--vault_password_file");
    commandArgs.add(keyInfo.vaultPasswordFile);
    commandArgs.add("--private_key_file");
    commandArgs.add(keyInfo.privateKey);
    commandArgs.add("--local_package_path");
    commandArgs.add(appConfig.getString("yb.thirdparty.packagePath"));
    commandArgs.add("--custom_ssh_port");
    commandArgs.add(keyInfo.sshPort.toString());

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

    JsonNode result =
        execAndParseCommandCloud(accessKey.getProviderUUID(), "template", commandArgs);

    if (result.get("error") == null) {
      keyInfo.passwordlessSudoAccess = passwordlessSudoAccess;
      keyInfo.provisionInstanceScript = path + "/" + PROVISION_SCRIPT;
      keyInfo.airGapInstall = airGapInstall;
      keyInfo.installNodeExporter = installNodeExporter;
      keyInfo.nodeExporterPort = nodeExporterPort;
      keyInfo.nodeExporterUser = nodeExporterUser;
      keyInfo.setUpChrony = setUpChrony;
      keyInfo.ntpServers = ntpServers;
      accessKey.setKeyInfo(keyInfo);
      accessKey.save();
    } else {
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, result);
    }
  }
}
