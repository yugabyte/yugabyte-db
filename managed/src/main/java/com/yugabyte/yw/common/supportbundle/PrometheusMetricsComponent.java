package com.yugabyte.yw.common.supportbundle;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.commissioner.tasks.params.SupportBundleTaskParams;
import com.yugabyte.yw.common.SupportBundleUtil;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.forms.SupportBundleFormData;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.metrics.MetricQueryResponse;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.BundleDetails.PrometheusMetricsType;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.io.File;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.text.SimpleDateFormat;
import java.time.Duration;
import java.util.Date;
import java.util.HashMap;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.io.FileUtils;
import play.libs.Json;

@Slf4j
@Singleton
public class PrometheusMetricsComponent implements SupportBundleComponent {

  private final MetricQueryHelper metricQueryHelper;
  private final SupportBundleUtil supportBundleUtil;
  public final String PROMETHEUS_DUMP_FOLDER = "promdump";
  public final String pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'";
  public final SimpleDateFormat dateFormat = new SimpleDateFormat(pattern);
  public final String TOO_MANY_SAMPLES_ERROR_MSG =
      "query processing would load too many samples into memory in query execution";
  public final String TOO_MANY_RESOLUTIONS_ERROR_MSG = "exceeded maximum resolution";

  @Inject private RuntimeConfGetter confGetter;

  @Inject
  public PrometheusMetricsComponent(
      MetricQueryHelper metricQueryHelper, SupportBundleUtil supportBundleUtil) {
    this.metricQueryHelper = metricQueryHelper;
    this.supportBundleUtil = supportBundleUtil;
  }

  public String getFileName(String type, Date startDate, Date endDate) {
    return String.format(
            "%s.%s-%s",
            type,
            dateFormat.format(startDate).replace(":", "_"),
            dateFormat.format(endDate).replace(":", "_"))
        + ".json";
  }

  public void exportMetric(
      SupportBundleFormData data, String query, String type, String exportDestDir)
      throws Exception {
    try {
      // get the default batch duration from the global runtime-config
      int batchDuration =
          confGetter.getGlobalConf(GlobalConfKeys.supportBundlePromDumpBatchDurationInMins);
      Duration defaultBatchDuration = Duration.ofMinutes(batchDuration);

      log.debug(
          "exportMetric: querying metric '{}' from {} to {}",
          query,
          data.promDumpStartDate,
          data.promDumpEndDate);

      // batchwise collection of prom-dump
      Date batchStartTS = data.promDumpStartDate;
      // end timestamp of the current batch will be batchStartTS +
      // defaultBatchDuration
      Date batchEndTS = Date.from(batchStartTS.toInstant().plus(defaultBatchDuration));
      int batchNumber = 1;
      int freq = 0;
      while (!batchEndTS.after(data.promDumpEndDate)) {
        log.debug(
            "exportMetric:[{}] batch {} ({} to {})", type, batchNumber, batchStartTS, batchEndTS);

        // populate the query params
        HashMap<String, String> queryParams = new HashMap<>();
        queryParams.put("query", query);
        queryParams.put("start", dateFormat.format(batchStartTS));
        queryParams.put("end", dateFormat.format(batchEndTS));
        queryParams.put(
            "step",
            confGetter.getGlobalConf(GlobalConfKeys.supportBundlePromDumpStepInSecs).toString());

        JsonNode response = metricQueryHelper.queryRange(queryParams);
        MetricQueryResponse metricResponse = Json.fromJson(response, MetricQueryResponse.class);
        if (metricResponse.error != null && metricResponse.data == null) {
          if (metricResponse.error.contains(TOO_MANY_SAMPLES_ERROR_MSG)
              || metricResponse.error.contains(TOO_MANY_RESOLUTIONS_ERROR_MSG)) {
            freq++;
            // newBatchDuration = oldBatchDuration / 2
            Duration newBatchDuration =
                Duration.ofSeconds(defaultBatchDuration.getSeconds() / (2 * freq));
            if (newBatchDuration.getSeconds() <= 1) {
              throw new RuntimeException(metricResponse.error);
            }
            batchEndTS = Date.from(batchStartTS.toInstant().plus(newBatchDuration));
            if (batchEndTS.after(data.promDumpEndDate)) {
              batchEndTS = data.promDumpEndDate;
              defaultBatchDuration =
                  Duration.between(batchStartTS.toInstant(), batchEndTS.toInstant());
              continue;
            }
            log.warn(
                "exportMetric: too many samples in result set. Reducing batch[{}] duration from {}"
                    + " to {} and trying again.",
                batchNumber,
                defaultBatchDuration,
                newBatchDuration);
            continue;
          }
          throw new RuntimeException(metricResponse.error);
        }

        ObjectMapper objectMapper = new ObjectMapper();

        // check if YBA node has enough space for the prometheus dump of the current batch.
        long YbaDiskSpaceFreeInBytes =
            Files.getFileStore(Paths.get(exportDestDir)).getUsableSpace();
        long currBatchPromDumpSize =
            objectMapper.writeValueAsString(metricResponse.data.result).getBytes().length;

        // log the error message if YBA node doesn't have enough space to export the prom dump
        // and continue with the next batch/export
        if (Long.compare(currBatchPromDumpSize, YbaDiskSpaceFreeInBytes) > 0) {
          String errMsg =
              String.format(
                  "Cannot export prometheus dump for metric[%s][batch:%d] due to insuffient"
                      + " space. Prom dump size in bytes: '%d', YBA space free in bytes: '%d'.",
                  type, batchNumber, currBatchPromDumpSize, YbaDiskSpaceFreeInBytes);
          log.error(errMsg);
          return;
        }

        // generate filename for the export
        if (!metricResponse.data.result.isEmpty()) {
          File outputFile = new File(exportDestDir, getFileName(type, batchStartTS, batchEndTS));
          batchNumber++;

          // gather and save the promethues metrics.
          objectMapper.writeValue(outputFile, metricResponse.data.result);
        } else {
          log.debug(
              "Empty query result for the type {} in the duration [{}-{}]",
              type,
              batchStartTS,
              batchEndTS);
        }

        // update the start timestamp for the next batch
        batchStartTS = Date.from(batchEndTS.toInstant().plusSeconds(1));
        if (batchStartTS.after(data.promDumpEndDate)) {
          return;
        }
        batchEndTS = Date.from(batchStartTS.toInstant().plus(defaultBatchDuration));
        if (batchEndTS.after(data.promDumpEndDate)) {
          batchEndTS = data.promDumpEndDate;
        }
        freq = 0;
      }
    } finally {
      // delete the directory if it is empty
      if (FileUtils.isEmptyDirectory(new File(exportDestDir))) {
        log.debug(
            "Prometheus metrics dump is not available for the type {} in the given duration", type);
        FileUtils.deleteDirectory(new File(exportDestDir));
      }
    }
  }

  @Override
  public void downloadComponent(
      SupportBundleTaskParams supportBundleTaskParams,
      Customer customer,
      Universe universe,
      Path bundlePath,
      NodeDetails node)
      throws Exception {
    log.info("Gathering prometheus metrics data for customer '{}'.", customer.getUuid());
    SupportBundleFormData data = supportBundleTaskParams.bundleData;

    // create PROMETHEUS_DUMP_FOLDER folder inside the support_bundle/YBA folder.
    String destDir = bundlePath.toString() + "/" + PROMETHEUS_DUMP_FOLDER;
    Files.createDirectories(Paths.get(destDir));

    // nodePrefix is necessary for exports of type: master_export, node_export, tserver_export,
    // cql_export, ysql_export
    String nodePrefix = universe.getUniverseDetails().nodePrefix;
    log.debug("Found node prefix '{}' for Universe '{}'", nodePrefix, universe.getName());

    log.debug("Collecting the following Prometheus metrics: {}", data.prometheusMetricsTypes);

    // validate the start & end dates of prometheus metrics dump
    // 1. If both the dates are given; Continue
    // 2. If no dates are specified, download all the exports from last 'x' duration
    boolean startDateIsValid = supportBundleUtil.isValidDate(data.promDumpStartDate);
    boolean endDateIsValid = supportBundleUtil.isValidDate(data.promDumpEndDate);
    if (!startDateIsValid && !endDateIsValid) {
      int defaultPromDumpRange =
          confGetter.getGlobalConf(GlobalConfKeys.supportBundleDefaultPromDumpRange);
      log.debug(
          "'promDumpStartDate' and 'promDumpEndDate' are not valid. Defaulting the duration to {}",
          defaultPromDumpRange);
      data.promDumpEndDate = data.endDate;
      data.promDumpStartDate =
          supportBundleUtil.getDateNMinutesAgo(data.promDumpEndDate, defaultPromDumpRange);
    }

    // loop through the requested metric types
    for (PrometheusMetricsType type : data.prometheusMetricsTypes) {
      // create <type> folder inside the support_bundle/YBA/promdump folder.
      String exportDestDir = destDir + "/" + type.name().toLowerCase();
      log.info("Attempting to create output directory for the export: {}.", type);
      Files.createDirectories(Paths.get(exportDestDir));

      // generate the promQL query
      // Ex query: "{export_type=\"master_export\",node_prefix=\"universe-test\"}"
      String query;
      if (type == PrometheusMetricsType.PLATFORM || type == PrometheusMetricsType.PROMETHEUS) {
        query =
            String.format("{job=\"%s\",node_prefix=\"%s\"}", type.name().toLowerCase(), nodePrefix);
      } else {
        query =
            String.format(
                "{export_type=\"%s\",node_prefix=\"%s\"}", type.name().toLowerCase(), nodePrefix);
      }
      exportMetric(data, query, type.name().toLowerCase(), exportDestDir);
    }
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
}
