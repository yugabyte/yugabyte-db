package com.yugabyte.yw.commissioner.tasks;

import com.google.common.base.Throwables;
import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.SupportBundleTaskParams;
import com.yugabyte.yw.common.AppConfigHelper;
import com.yugabyte.yw.common.SupportBundleUtil;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.operator.KubernetesOperatorStatusUpdater;
import com.yugabyte.yw.common.supportbundle.SupportBundleComponent;
import com.yugabyte.yw.common.supportbundle.SupportBundleComponentFactory;
import com.yugabyte.yw.controllers.handlers.UniverseInfoHandler;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.SupportBundle;
import com.yugabyte.yw.models.SupportBundle.SupportBundleStatusType;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.BundleDetails;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.io.BufferedOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.text.ParseException;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.List;
import java.util.stream.Collectors;
import java.util.zip.GZIPOutputStream;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.compress.archivers.tar.TarArchiveOutputStream;
import org.apache.commons.io.FileUtils;

@Slf4j
public class CreateSupportBundle extends AbstractTaskBase {

  @Inject private UniverseInfoHandler universeInfoHandler;
  @Inject private SupportBundleComponentFactory supportBundleComponentFactory;
  @Inject private SupportBundleUtil supportBundleUtil;
  @Inject private Config config;
  @Inject private KubernetesOperatorStatusUpdater kubernetesStatusUpdater;

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
      supportBundle.setPathObject(gzipPath);
      supportBundle.setStatus(SupportBundleStatusType.Success);
      kubernetesStatusUpdater.markSupportBundleFinished(supportBundle, gzipPath);
    } catch (Exception e) {
      taskParams().supportBundle.setStatus(SupportBundleStatusType.Failed);
      kubernetesStatusUpdater.markSupportBundleFailed(supportBundle);
      Throwables.throwIfUnchecked(e);
      throw new RuntimeException(e);
    } finally {
      supportBundle.update();
    }
  }

  public Path generateBundle(SupportBundle supportBundle)
      throws IOException, RuntimeException, ParseException {
    Customer customer = taskParams().customer;
    Universe universe = taskParams().universe;
    Path bundlePath = generateBundlePath(universe);
    Files.createDirectories(bundlePath);
    Path gzipPath =
        Paths.get(bundlePath.toAbsolutePath().toString().concat(".tar.gz")); // test this path
    log.debug("gzip support bundle path: {}", gzipPath.toString());
    log.debug("Fetching Universe {} logs", universe.getName());

    // Simplified the following 4 cases to extract appropriate start and end date
    // 1. If both of the dates are given and valid
    // 2. If only the start date is valid, filter from startDate till the end
    // 3. If only the end date is valid, filter from the beginning till endDate
    // 4. Default : If no dates are specified, download all the files from last n days
    Date startDate, endDate;
    boolean startDateIsValid = supportBundleUtil.isValidDate(supportBundle.getStartDate());
    boolean endDateIsValid = supportBundleUtil.isValidDate(supportBundle.getEndDate());
    if (!startDateIsValid && !endDateIsValid) {
      int default_date_range = config.getInt("yb.support_bundle.default_date_range");
      endDate = supportBundleUtil.getTodaysDate();
      startDate = supportBundleUtil.getDateNDaysAgo(endDate, default_date_range);
    } else {
      startDate = startDateIsValid ? supportBundle.getStartDate() : new Date(Long.MIN_VALUE);
      endDate = endDateIsValid ? supportBundle.getEndDate() : new Date(Long.MAX_VALUE);
    }

    // Downloads each type of node level support bundle component type into the bundle path
    List<NodeDetails> nodes = universe.getNodes().stream().collect(Collectors.toList());
    for (NodeDetails node : nodes) {
      for (BundleDetails.ComponentType componentType :
          supportBundle.getBundleDetails().getNodeLevelComponents()) {
        SupportBundleComponent supportBundleComponent =
            supportBundleComponentFactory.getComponent(componentType);
        try {
          // Call the downloadComponentBetweenDates() function for all node level components with
          // the node object.
          // Each component verifies if the dates are required and calls the downloadComponent().
          Path nodeComponentsDirPath =
              Paths.get(bundlePath.toAbsolutePath().toString(), node.nodeName);
          Files.createDirectories(nodeComponentsDirPath);
          supportBundleComponent.downloadComponentBetweenDates(
              customer, universe, nodeComponentsDirPath, startDate, endDate, node);
        } catch (Exception e) {
          log.error("Error occurred in support bundle collection", e);
          throw new RuntimeException(
              String.format(
                  "Error while trying to download the node level component files : %s",
                  e.getMessage()));
        }
      }
    }

    // Downloads each type of global level support bundle component type into the bundle path
    for (BundleDetails.ComponentType componentType :
        supportBundle.getBundleDetails().getGlobalLevelComponents()) {
      SupportBundleComponent supportBundleComponent =
          supportBundleComponentFactory.getComponent(componentType);
      try {
        // Call the downloadComponentBetweenDates() function for all global level components with
        // node = null.
        // Each component verifies if the dates are required and calls the downloadComponent().
        Path globalComponentsDirPath = Paths.get(bundlePath.toAbsolutePath().toString(), "YBA");
        Files.createDirectories(globalComponentsDirPath);
        supportBundleComponent.downloadComponentBetweenDates(
            customer, universe, globalComponentsDirPath, startDate, endDate, null);
      } catch (Exception e) {
        log.error("Error occurred in support bundle collection", e);
        throw new RuntimeException(
            String.format(
                "Error while trying to download the global level component files : %s",
                e.getMessage()));
      }
    }

    // Tar the support bundle directory and delete the original folder
    try (FileOutputStream fos = new FileOutputStream(gzipPath.toString()); // need to test this path
        GZIPOutputStream gos = new GZIPOutputStream(new BufferedOutputStream(fos));
        TarArchiveOutputStream tarOS = new TarArchiveOutputStream(gos)) {

      tarOS.setLongFileMode(TarArchiveOutputStream.LONGFILE_POSIX);
      tarOS.setBigNumberMode(TarArchiveOutputStream.BIGNUMBER_POSIX);
      Util.addFilesToTarGZ(bundlePath.toString(), "", tarOS);
      FileUtils.deleteDirectory(new File(bundlePath.toAbsolutePath().toString()));
    }
    log.debug(
        "Finished aggregating logs for support bundle with UUID {}", supportBundle.getBundleUUID());
    return gzipPath;
  }

  private Path generateBundlePath(Universe universe) {
    String storagePath = AppConfigHelper.getStoragePath();
    String datePrefix = new SimpleDateFormat("yyyyMMddHHmmss.SSS").format(new Date());
    String bundleName = "yb-support-bundle-" + universe.getName() + "-" + datePrefix + "-logs";
    Path bundlePath = Paths.get(storagePath + "/" + bundleName);
    return bundlePath;
  }
}
