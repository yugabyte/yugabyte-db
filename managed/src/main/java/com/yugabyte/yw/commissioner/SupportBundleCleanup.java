package com.yugabyte.yw.commissioner;

import akka.actor.ActorSystem;
import com.google.common.annotations.VisibleForTesting;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.SupportBundleUtil;
import com.yugabyte.yw.models.SupportBundle;
import com.yugabyte.yw.models.SupportBundle.SupportBundleStatusType;
import java.util.List;
import java.util.Date;
import java.util.UUID;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.text.ParseException;
import lombok.extern.slf4j.Slf4j;
import scala.concurrent.ExecutionContext;
import scala.concurrent.duration.Duration;

@Singleton
@Slf4j
public class SupportBundleCleanup {

  private AtomicBoolean running = new AtomicBoolean(false);

  private final ActorSystem actorSystem;

  private final ExecutionContext executionContext;

  // In hours
  private final int YB_SUPPORT_BUNDLE_CLEANUP_INTERVAL = 24;

  private final Config config;

  private SupportBundleUtil supportBundleUtil;

  @Inject
  public SupportBundleCleanup(
      ActorSystem actorSystem,
      ExecutionContext executionContext,
      Config config,
      SupportBundleUtil supportBundleUtil) {
    this.actorSystem = actorSystem;
    this.executionContext = executionContext;
    this.config = config;
    this.supportBundleUtil = supportBundleUtil;
  }

  public void setRunningState(AtomicBoolean state) {
    this.running = state;
  }

  public void start() {
    this.actorSystem
        .scheduler()
        .schedule(
            Duration.create(0, TimeUnit.HOURS),
            Duration.create(YB_SUPPORT_BUNDLE_CLEANUP_INTERVAL, TimeUnit.HOURS),
            this::scheduleRunner,
            this.executionContext);
  }

  @VisibleForTesting
  void scheduleRunner() {
    if (!running.compareAndSet(false, true)) {
      log.info("Previous Support Bundle Cleanup is still in progress");
      return;
    }

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
    } finally {
      running.set(false);
    }
  }

  public synchronized void deleteSupportBundleIfOld(SupportBundle supportBundle)
      throws ParseException {
    int default_delete_days = config.getInt("yb.support_bundle.default_retention_days");

    if (supportBundle.getStatus() == SupportBundleStatusType.Failed) {
      // Deletes row from the support_bundle db table
      SupportBundle.delete(supportBundle.getBundleUUID());

      log.info(
          "Automatically deleted Support Bundle with UUID: "
              + supportBundle.getBundleUUID().toString()
              + ", with status = Failed");
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
            "Automatically deleted Support Bundle with UUID: "
                + supportBundle.getBundleUUID().toString()
                + ", with status = Success");
      }
    }
  }

  public void handleSupportBundleError(UUID bundleUUID, Exception e) {
    log.error(String.format("Error trying to delete bundle: %s", bundleUUID.toString()), e);
  }
}
