// Copyright (c) Yugabyte, Inc.
package com.yugabyte.yw.controllers.handlers;

import com.cronutils.utils.VisibleForTesting;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.PlatformServiceException;
import java.io.BufferedInputStream;
import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Objects;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.AllArgsConstructor;
import lombok.Getter;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.compress.archivers.tar.TarArchiveEntry;
import org.apache.commons.compress.archivers.tar.TarArchiveInputStream;
import org.apache.commons.compress.compressors.gzip.GzipCompressorInputStream;
import org.apache.commons.compress.utils.IOUtils;
import org.apache.commons.lang.StringUtils;
import org.apache.commons.lang3.EnumUtils;
import play.mvc.Http;
import play.mvc.Http.Status;

@Slf4j
@Singleton
public class NodeAgentDownloadHandler {
  private static final String NODE_AGENT_INSTALLER_FILE = "node-agent-installer.sh";
  private static final String NODE_AGENT_TGZ_FILE_FORMAT = "node_agent-%s-%s-%s.tar.gz";

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

  private static byte[] getInstallerScript(String filepath) {
    try (TarArchiveInputStream tarInput =
        new TarArchiveInputStream(new GzipCompressorInputStream(new FileInputStream(filepath)))) {
      TarArchiveEntry currEntry;
      while ((currEntry = tarInput.getNextTarEntry()) != null) {
        if (!currEntry.isFile() || !currEntry.getName().endsWith(NODE_AGENT_INSTALLER_FILE)) {
          continue;
        }
        BufferedInputStream in = new BufferedInputStream(tarInput);
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        IOUtils.copy(in, out);
        return out.toByteArray();
      }
    } catch (IOException e) {
      throw new PlatformServiceException(
          Status.INTERNAL_SERVER_ERROR, "Error in reading the node-agent installer.");
    }
    throw new PlatformServiceException(Status.NOT_FOUND, "Node-agent installer does not exist.");
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
    Path nodeAgentsReleasesPath =
        Paths.get(appConfig.getString("yb.storage.path"), "node-agent", "releases");
    String softwareVersion =
        Objects.requireNonNull(
            (String)
                configHelper.getConfig(ConfigHelper.ConfigType.SoftwareVersion).get("version"));
    if (downloadType == DownloadType.PACKAGE) {
      // An example from build job is node_agent-2.15.3.0-b1372-darwin-amd64.tar.gz.
      String buildPkgFile =
          String.format(
              NODE_AGENT_TGZ_FILE_FORMAT,
              softwareVersion,
              osType.name().toLowerCase(),
              archType.name().toLowerCase());
      File buildZip = new File(nodeAgentsReleasesPath.toString(), buildPkgFile);
      InputStream is = com.yugabyte.yw.common.utils.FileUtils.getInputStreamOrFail(buildZip);
      return new NodeAgentDownloadFile("application/gzip", is, buildPkgFile);
    }
    // Look for the installer script in any of the tgz file.
    // This makes distribution of the installer more convenient.
    String defaultPkgFile =
        String.format(NODE_AGENT_TGZ_FILE_FORMAT, softwareVersion, "linux", "amd64");
    byte[] contents = getInstallerScript(nodeAgentsReleasesPath.resolve(defaultPkgFile).toString());
    return new NodeAgentDownloadFile(
        "application/x-sh", new ByteArrayInputStream(contents), NODE_AGENT_INSTALLER_FILE);
  }
}
