package com.yugabyte.yw.common.supportbundle;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.commissioner.tasks.params.SupportBundleTaskParams;
import com.yugabyte.yw.common.SupportBundleUtil;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.pa.PerfAdvisorClient;
import com.yugabyte.yw.common.pa.PerfAdvisorService;
import com.yugabyte.yw.forms.SupportBundleFormData;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.PACollector;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.io.File;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Collections;
import java.util.Date;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.io.FileUtils;

@Slf4j
@Singleton
public class PerfAdvisorComponent implements SupportBundleComponent {

  private final PerfAdvisorClient perfAdvisorClient;
  private final PerfAdvisorService perfAdvisorService;
  private final SupportBundleUtil supportBundleUtil;
  public final String PA_DUMP_FOLDER = "pa";

  private static final long POLL_INTERVAL_MS = TimeUnit.SECONDS.toMillis(10);
  private static final long POLL_TIMEOUT_MS = TimeUnit.MINUTES.toMillis(30);

  @Inject private RuntimeConfGetter confGetter;

  @Inject
  public PerfAdvisorComponent(
      PerfAdvisorClient perfAdvisorClient,
      PerfAdvisorService perfAdvisorService,
      SupportBundleUtil supportBundleUtil) {
    this.perfAdvisorClient = perfAdvisorClient;
    this.perfAdvisorService = perfAdvisorService;
    this.supportBundleUtil = supportBundleUtil;
  }

  @Override
  public void downloadComponent(
      SupportBundleTaskParams supportBundleTaskParams,
      Customer customer,
      Universe universe,
      Path bundlePath,
      NodeDetails node)
      throws Exception {
    log.info("Gathering PA support bundle for customer '{}'.", customer.getUuid());
    SupportBundleFormData data = supportBundleTaskParams.bundleData;

    // create PA_DUMP_FOLDER folder inside the support_bundle/YBA folder.
    Path destDir = Paths.get(bundlePath.toString() + "/" + PA_DUMP_FOLDER);
    Files.createDirectories(destDir);

    dateValidation(data);

    UUID paCollectorUuid = universe.getUniverseDetails().getPaCollectorUuid();
    if (paCollectorUuid == null) {
      log.warn(
          "Universe {} is not registered with any Performance Advisor collector. Skipping PA"
              + " support bundle.",
          universe.getUniverseUUID());
      return;
    }

    PACollector collector = perfAdvisorService.getOrBadRequest(customer.getUuid(), paCollectorUuid);

    log.info("Scheduling PA support bundle for universe {}", universe.getUniverseUUID());
    PerfAdvisorClient.SupportBundle bundle =
        perfAdvisorClient.scheduleSupportBundle(
            collector,
            universe.getUniverseUUID(),
            data.paDumpStartDate.toInstant(),
            data.paDumpEndDate.toInstant(),
            data.paMetricsFormat);

    long startTime = System.currentTimeMillis();
    while (System.currentTimeMillis() - startTime < POLL_TIMEOUT_MS) {
      bundle =
          perfAdvisorClient.getSupportBundle(collector, universe.getUniverseUUID(), bundle.getId());
      switch (bundle.getState()) {
        case COMPLETED:
          log.info("PA support bundle {} is ready for download.", bundle.getId());
          File bundleFile =
              perfAdvisorClient.downloadSupportBundle(
                  collector, universe.getUniverseUUID(), bundle.getId(), destDir.toFile());
          log.info("Successfully downloaded PA support bundle {}.", bundle.getId());
          perfAdvisorClient.deleteSupportBundle(
              collector, universe.getUniverseUUID(), bundle.getId());
          log.info("Successfully deleted PA support bundle {}.", bundle.getId());
          Util.extractFilesFromTarGZ(bundleFile.toPath(), destDir.toString());
          log.info("Successfully extracted PA support bundle file {} to {}", bundleFile, destDir);
          FileUtils.delete(bundleFile);
          log.info("Deleted downloaded bundle file {}", bundleFile);
          return;
        case FAILED:
          throw new RuntimeException(
              String.format(
                  "PA support bundle creation failed for bundle %s: %s",
                  bundle.getId(), bundle.getErrorMessage()));
        case SCHEDULED, IN_PROGRESS:
          log.info(
              "PA support bundle {} is in state {}. Waiting...", bundle.getId(), bundle.getState());
          Thread.sleep(POLL_INTERVAL_MS);
          break;
      }
    }
    throw new TimeoutException("Timed out waiting for PA support bundle creation to complete.");
  }

  @Override
  public void downloadComponentBetweenDates(
      SupportBundleTaskParams supportBundleTaskParams,
      Customer customer,
      Universe universe,
      Path bundlePath,
      Date startDate,
      Date endDate,
      NodeDetails node)
      throws Exception {
    this.downloadComponent(supportBundleTaskParams, customer, universe, bundlePath, node);
  }

  @Override
  public Map<String, Long> getFilesListWithSizes(
      Customer customer,
      SupportBundleFormData bundleData,
      Universe universe,
      Date startDate,
      Date endDate,
      NodeDetails node)
      throws Exception {

    dateValidation(bundleData);

    UUID paCollectorUuid = universe.getUniverseDetails().getPaCollectorUuid();
    if (paCollectorUuid == null) {
      log.warn(
          "Universe {} is not registered with any Performance Advisor collector. Skipping PA"
              + " support bundle.",
          universe.getUniverseUUID());
      return Collections.emptyMap();
    }

    PACollector collector = perfAdvisorService.getOrBadRequest(customer.getUuid(), paCollectorUuid);
    return perfAdvisorClient.estimateSupportBundleSize(
        collector,
        universe.getUniverseUUID(),
        bundleData.paDumpStartDate.toInstant(),
        bundleData.paDumpEndDate.toInstant(),
        bundleData.paMetricsFormat);
  }

  // validate the start & end dates of prometheus metrics dump
  // 1. If both the dates are given; Continue
  // 2. If no dates are specified, download all the exports from last 'x' duration
  private void dateValidation(SupportBundleFormData data) {
    boolean startDateIsValid = supportBundleUtil.isValidDate(data.paDumpStartDate);
    boolean endDateIsValid = supportBundleUtil.isValidDate(data.paDumpEndDate);
    if (!startDateIsValid && !endDateIsValid) {
      int defaultPromDumpRange =
          confGetter.getGlobalConf(GlobalConfKeys.supportBundleDefaultPromDumpRange);
      log.debug(
          "'paDumpStartDate' and 'paDumpEndDate' are not valid. Defaulting the duration to {}",
          defaultPromDumpRange);
      data.paDumpEndDate = data.endDate;
      data.paDumpStartDate =
          supportBundleUtil.getDateNMinutesAgo(data.endDate, defaultPromDumpRange);
    }
  }
}
