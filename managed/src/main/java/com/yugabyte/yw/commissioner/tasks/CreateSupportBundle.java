package com.yugabyte.yw.commissioner.tasks;

import com.google.inject.Inject;
import com.google.common.base.Throwables;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.SupportBundleTaskParams;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.typesafe.config.Config;
import com.yugabyte.yw.controllers.handlers.UniverseInfoHandler;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.SupportBundle;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.SupportBundle.SupportBundleStatusType;
import com.yugabyte.yw.models.helpers.BundleDetails;
import com.yugabyte.yw.common.supportbundle.SupportBundleComponent;
import com.yugabyte.yw.common.supportbundle.SupportBundleComponentFactory;
import com.yugabyte.yw.common.SupportBundleUtil;
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
import java.text.ParseException;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.compress.archivers.tar.TarArchiveEntry;
import org.apache.commons.compress.archivers.tar.TarArchiveOutputStream;
import org.apache.commons.compress.utils.IOUtils;
import org.apache.commons.io.FileUtils;

@Slf4j
public class CreateSupportBundle extends AbstractTaskBase {

  @Inject private UniverseInfoHandler universeInfoHandler;
  @Inject private SupportBundleComponentFactory supportBundleComponentFactory;
  @Inject private SupportBundleUtil supportBundleUtil;
  @Inject private Config config;

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

  public Path generateBundle(SupportBundle supportBundle) throws IOException, RuntimeException {
    Customer customer = taskParams().customer;
    Universe universe = taskParams().universe;
    Path bundlePath = generateBundlePath(universe);
    Files.createDirectories(bundlePath);
    Path gzipPath =
        Paths.get(bundlePath.toAbsolutePath().toString().concat(".tar.gz")); // test this path
    log.debug("gzip support bundle path: {}", gzipPath.toString());
    log.debug("Fetching Universe {} logs", universe.name);

    // Downloads each type of support bundle component type into the bundle path
    for (BundleDetails.ComponentType componentType : supportBundle.getBundleDetails().components) {
      SupportBundleComponent supportBundleComponent =
          supportBundleComponentFactory.getComponent(componentType);
      try {
        // If both of the dates are given and valid
        if (supportBundleUtil.isValidDate(supportBundle.getStartDate())
            && supportBundleUtil.isValidDate(supportBundle.getEndDate())) {
          supportBundleComponent.downloadComponentBetweenDates(
              customer,
              universe,
              bundlePath,
              supportBundle.getStartDate(),
              supportBundle.getEndDate());
        }
        // If only the start date is valid, filter from startDate till the end
        else if (supportBundleUtil.isValidDate(supportBundle.getStartDate())) {
          supportBundleComponent.downloadComponentBetweenDates(
              customer,
              universe,
              bundlePath,
              supportBundle.getStartDate(),
              new Date(Long.MAX_VALUE));
        }
        // If only the end date is valid, filter from the beginning till endDate
        else if (supportBundleUtil.isValidDate(supportBundle.getEndDate())) {
          supportBundleComponent.downloadComponentBetweenDates(
              customer, universe, bundlePath, new Date(Long.MIN_VALUE), supportBundle.getEndDate());
        }
        // Default : If no dates are specified, download all the files without date range filtering
        else {
          int default_date_range = config.getInt("yb.support_bundle.default_date_range");
          Date defaultEndDate = supportBundleUtil.getTodaysDate();
          Date defaultStartDate =
              supportBundleUtil.getDateNDaysAgo(defaultEndDate, default_date_range);
          supportBundleComponent.downloadComponentBetweenDates(
              customer, universe, bundlePath, defaultStartDate, defaultEndDate);
        }
      } catch (ParseException e) {
        throw new RuntimeException(
            String.format("Error while trying to parse the universe files : %s", e.getMessage()));
      }
    }

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
