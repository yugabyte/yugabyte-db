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
    // TODO: Add supported OS
    CENTOS,
    AMZN,
    DARWIN;
  }

  private enum Arch {
    // TODO: Add supported architectures
    ARM,
    AMD64;
  }

  @VisibleForTesting
  void validateDownloadTypeOrThrowError(String type, String os, String arch) {
    if (!EnumUtils.isValidEnumIgnoreCase(DownloadType.class, type)) {
      throw new PlatformServiceException(
          Http.Status.BAD_REQUEST, "Incorrect download step provided");
    }
    DownloadType downloadType = EnumUtils.getEnumIgnoreCase(DownloadType.class, type);
    if (downloadType == DownloadType.PACKAGE
        && (!EnumUtils.isValidEnumIgnoreCase(OS.class, os)
            || !EnumUtils.isValidEnumIgnoreCase(Arch.class, arch))) {
      throw new PlatformServiceException(
          Http.Status.BAD_REQUEST, "Incorrect OS or Arch passed for Package download step.");
    }
  }

  /**
   * Validates the request type and returns the node agent download file.
   *
   * @param type download type, os type, arch type.
   * @return the Node Agent download file (installer or build package).
   */
  public NodeAgentDownloadFile validateAndGetDownloadFile(String type, String os, String arch) {
    validateDownloadTypeOrThrowError(type, os, arch);
    final DownloadType downloadType = EnumUtils.getEnumIgnoreCase(DownloadType.class, type);

    Path nodeAgentsBuildPath =
        Paths.get(appConfig.getString("yb.storage.path"), "node-agents", "build");
    InputStream is;

    if (downloadType == DownloadType.INSTALLER) {
      File installerFile = new File(nodeAgentsBuildPath.toString(), NODE_AGENT_INSTALLER_FILE);
      is = FileUtils.getInputStreamOrFail(installerFile);
      return new NodeAgentDownloadFile("application/x-sh", is, NODE_AGENT_INSTALLER_FILE);
    }
    String softwareVersion =
        Objects.requireNonNull(
            (String)
                configHelper.getConfig(ConfigHelper.ConfigType.SoftwareVersion).get("version"));
    OS osType = EnumUtils.getEnumIgnoreCase(OS.class, os);
    Arch archType = EnumUtils.getEnumIgnoreCase(Arch.class, arch);
    String buildPkgFile =
        new StringBuilder()
            .append("node-agent-")
            .append(softwareVersion)
            .append("-")
            .append(osType.name().toLowerCase())
            .append("-")
            .append(archType.name().toLowerCase())
            .append(".tgz")
            .toString();
    File buildZip = new File(nodeAgentsBuildPath.toString(), buildPkgFile);
    is = com.yugabyte.yw.common.utils.FileUtils.getInputStreamOrFail(buildZip);
    return new NodeAgentDownloadFile("application/gzip", is, buildPkgFile);
  }
}
