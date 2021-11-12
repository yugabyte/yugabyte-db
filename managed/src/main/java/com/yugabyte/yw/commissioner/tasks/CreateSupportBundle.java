package com.yugabyte.yw.commissioner.tasks;

import com.google.inject.Inject;
import com.google.common.base.Throwables;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.SupportBundleTaskParams;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.controllers.handlers.UniverseInfoHandler;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.SupportBundle;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.SupportBundle.SupportBundleStatusType;
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
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.compress.archivers.tar.TarArchiveEntry;
import org.apache.commons.compress.archivers.tar.TarArchiveOutputStream;
import org.apache.commons.compress.utils.IOUtils;
import org.apache.commons.io.FileUtils;

@Slf4j
public class CreateSupportBundle extends AbstractTaskBase {

  @Inject private UniverseInfoHandler universeInfoHandler;

  @Inject
  protected CreateSupportBundle(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected SupportBundleTaskParams taskParams() {
    return (SupportBundleTaskParams) taskParams;
  }

  @Override
  public void run() {
    SupportBundle supportBundle = taskParams().supportBundle;
    try {
      Path gzipPath = generateBundle(supportBundle);
      supportBundle.setPath(gzipPath);
      supportBundle.setStatus(SupportBundleStatusType.Success);
    } catch (IOException | RuntimeException e) {
      taskParams().supportBundle.setStatus(SupportBundleStatusType.Failed);
      Throwables.throwIfUnchecked(e);
      throw new RuntimeException(e);
    } finally {
      supportBundle.update();
    }
  }

  public Path generateBundle(SupportBundle supportBundle) throws IOException {
    Customer customer = taskParams().customer;
    Universe universe = taskParams().universe;
    Path bundlePath = generateBundlePath(universe);
    Files.createDirectories(bundlePath);
    Path gzipPath =
        Paths.get(bundlePath.toAbsolutePath().toString().concat(".tar.gz")); // test this path
    log.debug("gzip support bundle path: {}", gzipPath.toString());
    log.debug("Fetching Universe {} logs", universe.name);
    universeInfoHandler.downloadUniverseLogs(customer, universe, bundlePath);
    downloadApplicationLogs(customer, universe, bundlePath);

    try (FileOutputStream fos = new FileOutputStream(gzipPath.toString()); // need to test this path
        GZIPOutputStream gos = new GZIPOutputStream(new BufferedOutputStream(fos));
        TarArchiveOutputStream tarOS = new TarArchiveOutputStream(gos)) {

      tarOS.setLongFileMode(TarArchiveOutputStream.LONGFILE_POSIX);
      addFilesToTarGZ(bundlePath.toString(), "", tarOS);
    }
    log.debug(
        "Finished aggregating logs for support bundle with UUID {}", supportBundle.getBundleUUID());
    return gzipPath;
  }

  private Path generateBundlePath(Universe universe) {
    String storagePath = runtimeConfigFactory.staticApplicationConf().getString("yb.storage.path");
    String datePrefix = new SimpleDateFormat("yyyyMMddHHmmss.SSS").format(new Date());
    String bundleName = "yb-support-bundle-" + universe.name + "-" + datePrefix + "-logs";
    Path bundlePath = Paths.get(storagePath + "/" + bundleName);
    return bundlePath;
  }

  private Path downloadApplicationLogs(Customer customer, Universe universe, Path basePath)
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
    log.debug("Downloading application logs to {}", destDir);
    File source = new File(logDir);
    File dest = new File(destDir);
    FileUtils.copyDirectory(source, dest);
    return basePath;
  }

  private static void addFilesToTarGZ(
      String filePath, String parent, TarArchiveOutputStream tarArchive) throws IOException {
    File file = new File(filePath);
    String entryName = parent + file.getName();
    tarArchive.putArchiveEntry(new TarArchiveEntry(file, entryName));
    if (file.isFile()) {
      try (FileInputStream fis = new FileInputStream(file);
          BufferedInputStream bis = new BufferedInputStream(fis)) {
        IOUtils.copy(bis, tarArchive);
        tarArchive.closeArchiveEntry();
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
