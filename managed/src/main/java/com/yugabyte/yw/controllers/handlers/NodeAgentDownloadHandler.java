// Copyright (c) Yugabyte, Inc.
package com.yugabyte.yw.controllers.handlers;

import com.cronutils.utils.VisibleForTesting;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.utils.FileUtils;

import javax.inject.Inject;
import javax.inject.Singleton;
import java.io.File;
import java.io.InputStream;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Objects;

import lombok.AllArgsConstructor;
import lombok.Getter;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang.StringUtils;
import org.apache.commons.lang3.EnumUtils;
import play.mvc.Http;

@Slf4j
@Singleton
public class NodeAgentDownloadHandler {
  private final String NODE_AGENT_INSTALLER_FILE = "node-agent-installer.sh";

  private final Config appConfig;
  private final ConfigHelper configHelper;

  @Inject
  public NodeAgentDownloadHandler(Config appConfig, ConfigHelper configHelper) {
    this.configHelper = configHelper;
    this.appConfig = appConfig;
  }

  @AllArgsConstructor
  public class NodeAgentDownloadFile {
    @Getter String ContentType;
    @Getter InputStream Content;
    @Getter String FileName;
  }

  private enum DownloadType {
    INSTALLER,
    PACKAGE;
  }

  private enum OS {
    DARWIN,
    LINUX
  }

  private enum Arch {
    ARM64,
    AMD64;
  }

  @VisibleForTesting
  void validateDownloadType(DownloadType downloadType, OS osType, Arch archType) {
    if (downloadType == null) {
      throw new PlatformServiceException(
          Http.Status.BAD_REQUEST, "Incorrect download step provided");
    }
    if (downloadType == DownloadType.PACKAGE && (osType == null || archType == null)) {
      throw new PlatformServiceException(
          Http.Status.BAD_REQUEST, "Incorrect OS or Arch passed for package download step");
    }
  }

  /**
   * Validates the request type and returns the node agent download file.
   *
   * @param type download type, os type, arch type.
   * @return the Node Agent download file (installer or build package).
   */
  public NodeAgentDownloadFile validateAndGetDownloadFile(String type, String os, String arch) {
    DownloadType downloadType =
        StringUtils.isBlank(type)
            ? DownloadType.INSTALLER
            : EnumUtils.getEnumIgnoreCase(DownloadType.class, type);
    OS osType = EnumUtils.getEnumIgnoreCase(OS.class, os);
    Arch archType = EnumUtils.getEnumIgnoreCase(Arch.class, arch);
    validateDownloadType(downloadType, osType, archType);
    Path nodeAgentsBuildPath =
        Paths.get(appConfig.getString("yb.storage.path"), "node-agents", "build");
    InputStream is;

    if (downloadType == DownloadType.PACKAGE) {
      String softwareVersion =
          Objects.requireNonNull(
              (String)
                  configHelper.getConfig(ConfigHelper.ConfigType.SoftwareVersion).get("version"));
      // An example from build job is node_agent-2.15.3.0-b1372-darwin-amd64.tar.gz.
      String buildPkgFile =
          String.format(
              "node_agent-%s-%s-%s.tar.gz",
              softwareVersion, osType.name().toLowerCase(), archType.name().toLowerCase());
      File buildZip = new File(nodeAgentsBuildPath.toString(), buildPkgFile);
      is = com.yugabyte.yw.common.utils.FileUtils.getInputStreamOrFail(buildZip);
      return new NodeAgentDownloadFile("application/gzip", is, buildPkgFile);
    }
    File installerFile = new File(nodeAgentsBuildPath.toString(), NODE_AGENT_INSTALLER_FILE);
    is = FileUtils.getInputStreamOrFail(installerFile);
    return new NodeAgentDownloadFile("application/x-sh", is, NODE_AGENT_INSTALLER_FILE);
  }
}
