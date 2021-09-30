// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers.handlers;

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.SupportBundleFormData;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.SupportBundle;
import com.yugabyte.yw.models.Universe;
import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.UUID;
import java.util.zip.GZIPOutputStream;
import org.apache.commons.compress.archivers.tar.TarArchiveEntry;
import org.apache.commons.compress.archivers.tar.TarArchiveOutputStream;
import org.apache.commons.compress.utils.IOUtils;
import org.apache.commons.io.FileUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class SupportBundleHandler {

  @Inject private Config config;

  @Inject private RuntimeConfigFactory runtimeConfigFactory;

  @Inject private UniverseInfoHandler universeInfoHandler;

  public static final Logger LOG = LoggerFactory.getLogger(SupportBundleHandler.class);

  public Path downloadApplicationLogs(Customer customer, Universe universe, Path basePath)
      throws IOException {
    String appHomeDir =
        config.hasPath("application.home") ? config.getString("application.home") : ".";
    String logDir =
        config.hasPath("log.override.path")
            ? config.getString("log.override.path")
            : String.format("%s/logs", appHomeDir);
    String destDir = basePath.toString() + "/" + "application_logs";
    Path destPath = Paths.get(destDir);
    Files.createDirectories(destPath);
    LOG.debug("Downloading application logs to {}", destDir);
    File source = new File(logDir);
    File dest = new File(destDir);
    FileUtils.copyDirectory(source, dest);
    return basePath;
  }

  public UUID createBundle(Customer customer, Universe universe, SupportBundleFormData bundleData)
      throws IOException {
    String storagePath = runtimeConfigFactory.staticApplicationConf().getString("yb.storage.path");
    String datePrefix = new SimpleDateFormat("yyyyMMddHHmmss.SSS").format(new Date());
    String bundleName = "yb-support-bundle-" + universe.name + "-" + datePrefix + "-logs";
    Path bundlePath = Paths.get(storagePath + "/" + bundleName);

    Files.createDirectories(bundlePath);

    LOG.debug("Fetching Universe {} logs", universe.name);
    universeInfoHandler.downloadUniverseLogs(customer, universe, bundlePath);
    downloadApplicationLogs(customer, universe, bundlePath);

    try (FileOutputStream fos =
            new FileOutputStream(bundlePath.toAbsolutePath().toString().concat(".tar.gz"));
        GZIPOutputStream gos = new GZIPOutputStream(new BufferedOutputStream(fos));
        TarArchiveOutputStream tarOS = new TarArchiveOutputStream(gos)) {

      tarOS.setLongFileMode(TarArchiveOutputStream.LONGFILE_POSIX);
      addFilesToTarGZ(bundlePath.toString(), "", tarOS);
      tarOS.close();
    }

    SupportBundle supportBundle =
        new SupportBundle(universe, Paths.get(bundlePath.toString() + ".tar.gz"), bundleData);
    LOG.debug("Created new support bundle with UUID {}", supportBundle.bundleUUID);
    supportBundle.save();
    return supportBundle.getBundleUUID();
  }

  public void addFilesToTarGZ(String filePath, String parent, TarArchiveOutputStream tarArchive)
      throws IOException {
    File file = new File(filePath);
    String entryName = parent + file.getName();
    tarArchive.putArchiveEntry(new TarArchiveEntry(file, entryName));
    if (file.isFile()) {
      try (FileInputStream fis = new FileInputStream(file);
          BufferedInputStream bis = new BufferedInputStream(fis)) {
        IOUtils.copy(bis, tarArchive);
        tarArchive.closeArchiveEntry();
        bis.close();
      }
    } else if (file.isDirectory()) {
      // no content to copy so close archive entry
      tarArchive.closeArchiveEntry();
      for (File f : file.listFiles()) {
        addFilesToTarGZ(f.getAbsolutePath(), entryName + File.separator, tarArchive);
      }
    }
  }
}
