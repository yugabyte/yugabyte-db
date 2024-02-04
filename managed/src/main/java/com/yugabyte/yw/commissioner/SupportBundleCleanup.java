package com.yugabyte.yw.commissioner;

import com.google.common.annotations.VisibleForTesting;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.SupportBundleUtil;
import com.yugabyte.yw.models.SupportBundle;
import com.yugabyte.yw.models.SupportBundle.SupportBundleStatusType;
import java.text.ParseException;
import java.time.Duration;
import java.util.Date;
import java.util.List;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;

@Singleton
@Slf4j
public class SupportBundleCleanup {

  private final PlatformScheduler platformScheduler;
  // In hours
  private final int YB_SUPPORT_BUNDLE_CLEANUP_INTERVAL = 24;

  private final Config config;

  private final SupportBundleUtil supportBundleUtil;

  @Inject
  public SupportBundleCleanup(
      PlatformScheduler platformScheduler, Config config, SupportBundleUtil supportBundleUtil) {
    this.platformScheduler = platformScheduler;
    this.config = config;
    this.supportBundleUtil = supportBundleUtil;
  }

  public void start() {
    platformScheduler.schedule(
        getClass().getSimpleName(),
        Duration.ZERO,
        Duration.ofHours(YB_SUPPORT_BUNDLE_CLEANUP_INTERVAL),
        this::scheduleRunner);
  }

  @VisibleForTesting
  void scheduleRunner() {
    log.info("Running Support Bundle Cleanup");
    try {
      List<SupportBundle> supportBundleList = SupportBundle.getAll();

      supportBundleList.forEach(
          (supportBundle) -> {
            try {
              deleteSupportBundleIfOld(supportBundle);
            } catch (Exception e) {
              handleSupportBundleError(supportBundle.getBundleUUID(), e);
            }
          });
    } catch (Exception e) {
      log.error("Error running support bundle cleanup", e);
    }
  }

  public synchronized void deleteSupportBundleIfOld(SupportBundle supportBundle)
      throws ParseException {
    int default_delete_days = config.getInt("yb.support_bundle.retention_days");

    if (supportBundle.getStatus() == SupportBundleStatusType.Failed) {
      // Delete the actual archive file
      supportBundleUtil.deleteFile(supportBundle.getPathObject());
      // Deletes row from the support_bundle db table
      SupportBundle.delete(supportBundle.getBundleUUID());

      log.info(
          "Automatically deleted Support Bundle with UUID: {}, with status = Failed",
          supportBundle.getBundleUUID());
    } else if (supportBundle.getStatus() == SupportBundleStatusType.Running) {
      return;
    } else {
      // Case where support bundle status = Success
      String bundleFileName = supportBundle.getPathObject().getFileName().toString();
      Date bundleDate = supportBundleUtil.getDateFromBundleFileName(bundleFileName);

      Date dateToday = supportBundleUtil.getTodaysDate();
      Date dateNDaysAgo = supportBundleUtil.getDateNDaysAgo(dateToday, default_delete_days);

      if (bundleDate.before(dateNDaysAgo)) {
        // Deletes row from the support_bundle db table
        SupportBundle.delete(supportBundle.getBundleUUID());
        // Delete the actual archive file
        supportBundleUtil.deleteFile(supportBundle.getPathObject());

        log.info(
            "Automatically deleted Support Bundle with UUID: {}, with status = success",
            supportBundle.getBundleUUID());
      }
    }
  }

  public void handleSupportBundleError(UUID bundleUUID, Exception e) {
    log.error(String.format("Error trying to delete bundle: %s", bundleUUID.toString()), e);
  }

  public void markAllRunningSupportBundlesFailed() {
    SupportBundle.getAll()
        .forEach(
            sb -> {
              if (SupportBundleStatusType.Running.equals(sb.getStatus())) {
                sb.setStatus(SupportBundleStatusType.Failed);
                sb.update();
              }
            });
  }
}
