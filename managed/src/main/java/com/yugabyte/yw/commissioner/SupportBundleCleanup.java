package com.yugabyte.yw.commissioner;

import com.google.common.annotations.VisibleForTesting;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.SupportBundleUtil;
import com.yugabyte.yw.models.SupportBundle;
import com.yugabyte.yw.models.SupportBundle.SupportBundleStatusType;
import com.yugabyte.yw.models.SupportBundleV2;
import com.yugabyte.yw.models.SupportBundleV2StatusType;
import java.text.ParseException;
import java.time.Duration;
import java.util.Date;
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
      SupportBundle.getAll()
          .forEach(
              supportBundle -> {
                try {
                  deleteSupportBundleIfOld(supportBundle);
                } catch (Exception e) {
                  handleSupportBundleError(supportBundle.getBundleUUID(), e);
                }
              });
      SupportBundleV2.getAll()
          .forEach(
              supportBundle -> {
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
    int defaultDeleteDays = config.getInt("yb.support_bundle.retention_days");
    SupportBundleStatusType status = supportBundle.getStatus();
    if (status == SupportBundleStatusType.Failed || status == SupportBundleStatusType.Aborted) {
      supportBundleUtil.deleteSupportBundle(supportBundle);
      log.info(
          "Automatically deleted Support Bundle with UUID: {}, with status = {}",
          supportBundle.getBundleUUID(),
          status);
    } else if (status == SupportBundleStatusType.Running) {
      return;
    } else {
      String bundleFileName = supportBundle.getPathObject().getFileName().toString();
      Date bundleDate = supportBundleUtil.getDateFromBundleFileName(bundleFileName);

      Date dateToday = supportBundleUtil.getTodaysDate();
      Date dateNDaysAgo = supportBundleUtil.getDateNDaysAgo(dateToday, defaultDeleteDays);

      if (bundleDate.before(dateNDaysAgo)) {
        supportBundleUtil.deleteSupportBundle(supportBundle);
        log.info(
            "Automatically deleted Support Bundle with UUID: {}, with status = success",
            supportBundle.getBundleUUID());
      }
    }
  }

  public void deleteSupportBundleIfOld(SupportBundleV2 supportBundle) throws ParseException {
    int defaultDeleteDays = config.getInt("yb.support_bundle.retention_days");
    SupportBundleV2StatusType status = supportBundle.getStatus();
    if (status == SupportBundleV2StatusType.Failed || status == SupportBundleV2StatusType.Aborted) {
      supportBundleUtil.deleteSupportBundleV2(supportBundle);
      log.info(
          "Automatically deleted Support Bundle with UUID: {}, with status = {}",
          supportBundle.getBundleUUID(),
          status);
    } else if (status == SupportBundleV2StatusType.Running) {
      return;
    } else {
      // Keyed off the same column the API reports as creation_date, so that the advertised
      // expiration_date and the actual deletion agree.
      Date bundleDate = supportBundle.getCreationDate();

      Date dateToday = supportBundleUtil.getTodaysDate();
      Date dateNDaysAgo = supportBundleUtil.getDateNDaysAgo(dateToday, defaultDeleteDays);

      if (bundleDate.before(dateNDaysAgo)) {
        supportBundleUtil.deleteSupportBundleV2(supportBundle);
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
    SupportBundleV2.getAll()
        .forEach(
            sb -> {
              if (SupportBundleV2StatusType.Running.equals(sb.getStatus())) {
                sb.setStatus(SupportBundleV2StatusType.Failed);
                sb.update();
              }
            });
  }
}
