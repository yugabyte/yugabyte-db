package com.yugabyte.yw.common.supportbundle;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
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
import java.time.Duration;
import java.util.Date;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.TimeUnit;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.io.FileUtils;
import play.libs.Json;

@Slf4j
@Singleton
public class PrometheusMetricsComponent implements SupportBundleComponent {

  private final MetricQueryHelper metricQueryHelper;
  private final SupportBundleUtil supportBundleUtil;
  public final String PROMETHEUS_DUMP_FOLDER = "promdump";
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
    return getFileName(null, type, null, startDate, endDate);
  }

  public String getFileName(
      String nodeName, String type, String metricName, Date startDate, Date endDate) {
    StringBuilder fileName = new StringBuilder();
    if (nodeName != null) {
      fileName.append(nodeName).append(".");
    }
    fileName.append(type);
    if (metricName != null) {
      fileName.append(".").append(metricName);
    }
    fileName
        .append(".")
        .append(startDate.toInstant().toString().replace(":", "_"))
        .append("-")
        .append(endDate.toInstant().toString().replace(":", "_"))
        .append(".json");
    return fileName.toString();
  }

  public Duration exportMetric(
      Date startDate, Date endDate, String query, String type, String exportDestDir)
      throws Exception {
    return exportMetric(startDate, endDate, query, null, type, null, exportDestDir, null);
  }

  public Duration exportMetric(
      Date startDate,
      Date endDate,
      String query,
      String nodeName,
      String type,
      String metricName,
      String exportDestDir,
      Duration initialBatchDuration)
      throws Exception {
    try {
      // Get the batch duration from provided value or global runtime-config
      Duration effectiveBatchDuration;
      if (initialBatchDuration != null) {
        effectiveBatchDuration = initialBatchDuration;
      } else {
        int batchDuration =
            confGetter.getGlobalConf(GlobalConfKeys.supportBundlePromDumpBatchDurationInMins);
        effectiveBatchDuration = Duration.ofMinutes(batchDuration);
      }

      log.debug("exportMetric: querying metric '{}' from {} to {}", query, startDate, endDate);

      // batchwise collection of prom-dump
      Date batchStartTS = startDate;
      Date batchEndTS = Date.from(batchStartTS.toInstant().plus(effectiveBatchDuration));
      int batchNumber = 1;
      int freq = 0;
      while (!batchEndTS.after(endDate)) {
        // populate the query params
        HashMap<String, String> queryParams = new HashMap<>();
        queryParams.put("query", query);
        queryParams.put("start", batchStartTS.toInstant().toString());
        queryParams.put("end", batchEndTS.toInstant().toString());
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
                Duration.ofSeconds(effectiveBatchDuration.getSeconds() / (2 * freq));
            if (newBatchDuration.getSeconds() <= 1) {
              throw new RuntimeException(metricResponse.error);
            }
            batchEndTS = Date.from(batchStartTS.toInstant().plus(newBatchDuration));
            if (batchEndTS.after(endDate)) {
              batchEndTS = endDate;
              effectiveBatchDuration =
                  Duration.between(batchStartTS.toInstant(), batchEndTS.toInstant());
              continue;
            }
            log.warn(
                "exportMetric: too many samples in result set. Reducing batch[{}] duration from {}"
                    + " to {} and trying again.",
                batchNumber,
                effectiveBatchDuration,
                newBatchDuration);
            // Update the effective batch duration for future use
            effectiveBatchDuration = newBatchDuration;
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
          return effectiveBatchDuration;
        }

        // generate filename for the export
        if (!metricResponse.data.result.isEmpty()) {
          File outputFile =
              new File(
                  exportDestDir, getFileName(nodeName, type, metricName, batchStartTS, batchEndTS));
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
        if (batchStartTS.after(endDate)) {
          return effectiveBatchDuration;
        }
        batchEndTS = Date.from(batchStartTS.toInstant().plus(effectiveBatchDuration));
        if (batchEndTS.after(endDate)) {
          batchEndTS = endDate;
        }
        freq = 0;
      }
      return effectiveBatchDuration;
    } finally {
      // delete the directory if it is empty
      if (FileUtils.isEmptyDirectory(new File(exportDestDir))) {
        log.debug(
            "Prometheus metrics dump is not available for the type {} in the given duration", type);
        FileUtils.deleteDirectory(new File(exportDestDir));
      }
    }
  }

  public void exportNodeLevelMetrics(
      Universe universe,
      Date startDate,
      Date endDate,
      String typeName,
      String exportDestDir,
      boolean splitTableLevelMetrics)
      throws Exception {
    // Query Prometheus for table-level metric names using the label values API
    // Only needed for tserver_export where we split table-level metrics
    List<String> tableLevelMetrics = List.of();
    if (splitTableLevelMetrics) {
      HashMap<String, String> labelQueryParams = new HashMap<>();
      labelQueryParams.put("start", startDate.toInstant().toString());
      labelQueryParams.put("end", endDate.toInstant().toString());
      labelQueryParams.put("match[]", "{table_id=\"sys.catalog.uuid\"}");
      tableLevelMetrics = metricQueryHelper.queryLabelValues("__name__", labelQueryParams);
      log.debug("Found {} table-level metrics: {}", tableLevelMetrics.size(), tableLevelMetrics);
    }

    // Track effective batch duration to reuse across table-level queries
    Duration effectiveBatchDuration = null;

    for (NodeDetails node : universe.getNodes()) {
      String nodeName = node.getNodeName();

      if (splitTableLevelMetrics && !tableLevelMetrics.isEmpty()) {
        // Get all the non-table level metrics for the node
        log.debug("Getting all the non-table level metrics for the node: {}", nodeName);
        String tableLevelMetricsRegex = String.join("|", tableLevelMetrics);
        String nonTableLevelMetricsQuery =
            String.format(
                "{export_type=\"%s\",node_name=\"%s\",__name__!~\"%s\"}",
                typeName, nodeName, tableLevelMetricsRegex);
        effectiveBatchDuration =
            exportMetric(
                new Date(startDate.getTime()),
                new Date(endDate.getTime()),
                nonTableLevelMetricsQuery,
                nodeName,
                typeName,
                null,
                exportDestDir,
                effectiveBatchDuration);

        // Get all table level metrics for the node, reusing the effective batch duration
        log.debug("Getting all the table level metrics for the node: {}", nodeName);
        for (String tableLevelMetric : tableLevelMetrics) {
          String query =
              String.format(
                  "{export_type=\"%s\",node_name=\"%s\",__name__=\"%s\"}",
                  typeName, nodeName, tableLevelMetric);
          effectiveBatchDuration =
              exportMetric(
                  new Date(startDate.getTime()),
                  new Date(endDate.getTime()),
                  query,
                  nodeName,
                  typeName,
                  tableLevelMetric,
                  exportDestDir,
                  effectiveBatchDuration);
        }
      } else {
        // For non-tserver exports or when no table-level metrics exist,
        // export all metrics for the node in one query
        log.debug("Getting all metrics for the node: {}", nodeName);
        String query = String.format("{export_type=\"%s\",node_name=\"%s\"}", typeName, nodeName);
        exportMetric(
            new Date(startDate.getTime()),
            new Date(endDate.getTime()),
            query,
            nodeName,
            typeName,
            null,
            exportDestDir,
            null);
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

    dateValidation(data);

    long startTime = data.promDumpStartDate.getTime(), endTime = data.promDumpEndDate.getTime();
    // loop through the requested metric types
    data.prometheusMetricsTypes.stream()
        .forEach(
            type -> {
              try {
                String typeName = type.name().toLowerCase();
                // create <type> folder inside the support_bundle/YBA/promdump folder.
                String exportDestDir = destDir + "/" + typeName;
                log.info("Attempting to create output directory for the export: {}.", type);
                Files.createDirectories(Paths.get(exportDestDir));

                // generate the promQL query
                // Ex query: "{export_type=\"master_export\",node_prefix=\"universe-test\"}"
                String query;
                if (type == PrometheusMetricsType.PLATFORM
                    || type == PrometheusMetricsType.PROMETHEUS) {
                  query =
                      String.format(
                          "{job=\"%s\",node_prefix=\"%s\"}",
                          typeName, (type == PrometheusMetricsType.PLATFORM ? nodePrefix : ""));
                  exportMetric(
                      new Date(startTime), new Date(endTime), query, typeName, exportDestDir);
                } else {
                  // Only split table-level metrics for tserver_export
                  // Master always has 1 table, other export types don't have table-level metrics
                  boolean splitTableLevelMetrics = (type == PrometheusMetricsType.TSERVER_EXPORT);
                  exportNodeLevelMetrics(
                      universe,
                      new Date(startTime),
                      new Date(endTime),
                      typeName,
                      exportDestDir,
                      splitTableLevelMetrics);
                }
              } catch (Exception e) {
                log.error("Error processing PrometheusMetricsType {}: {}", type, e.getMessage(), e);
              }
            });

    // Collect metrics for the custom PromQL queries.
    data.promQueries.entrySet().stream()
        .forEach(
            queryEntry -> {
              try {
                String query = queryEntry.getValue();
                String queryType = queryEntry.getKey().replace(" ", "_");
                String exportDestDir = destDir + "/" + queryType;
                log.info("Attempting to create output directory for prometheus query {}", query);
                Path path = Files.createDirectories(Paths.get(exportDestDir));
                // Add a manifest which specifies the query and start,end dates.
                ObjectNode manifest = Json.newObject().put("Query", query);
                manifest.put("StartDate", data.promDumpStartDate.toString());
                manifest.put("EndDate", data.promDumpEndDate.toString());
                supportBundleUtil.saveMetadata(
                    customer, path.toAbsolutePath().toString(), manifest, "manifest.json");
                exportMetric(
                    new Date(startTime), new Date(endTime), query, queryType, exportDestDir);
              } catch (Exception e) {
                log.error(
                    "Error processing custom prometheus query {}: {}",
                    queryEntry.getValue(),
                    e.getMessage(),
                    e);
              }
            });
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

  // It's hard to know exact sizes before actually collecting the data.
  // So we add estimates based on date collected from various dev portal LRUs with varying node
  // numbers.
  // Can adjust the estimates later if they are too off.
  public Map<String, Long> getFilesListWithSizes(
      Customer customer,
      SupportBundleFormData bundleData,
      Universe universe,
      Date startDate,
      Date endDate,
      NodeDetails node)
      throws Exception {

    dateValidation(bundleData);
    Long totalMins =
        TimeUnit.MILLISECONDS.toMinutes(
            bundleData.promDumpEndDate.getTime() - bundleData.promDumpStartDate.getTime());
    // By default we collect prom metrics in 15 min batches.
    Long timeMultiplier = (totalMins + 14) / 15;
    Map<String, Long> res = new HashMap<String, Long>();
    Long sum = 0L, numNodes = 1L * universe.getNodes().size();
    for (PrometheusMetricsType type : bundleData.prometheusMetricsTypes) {
      switch (type) {
          // 600KB per db node per 15 mins
        case NODE_EXPORT:
          sum += 600000 * numNodes * timeMultiplier;
          break;
          // 3MB per master per 15 mins
        case MASTER_EXPORT:
          sum += 3000000L * universe.getMasters().size() * timeMultiplier;
          break;
          // 110KB per 15 mins
        case PLATFORM:
          sum += 110000L * timeMultiplier;
          break;
          // 400KB per universe per 15 mins
        case PROMETHEUS:
          sum += 400000L * Universe.find.all().size() * timeMultiplier;
          break;
          // 3MB per tserver per 15 mins
        case TSERVER_EXPORT:
          sum += 3000000L * universe.getTServers().size() * timeMultiplier;
          break;
          // 300KB per node per 15 mins
        case YSQL_EXPORT:
          // Intentional fallthrough
        case CQL_EXPORT:
          sum += 300000L * numNodes * timeMultiplier;
      }
    }
    // Its not possible to estimate the size correctly for custom queries since we don't
    // know what queries will be passed in.
    // Adding 5MB per query per 15 minutes to account for custom queries.
    sum += 5000000L * bundleData.promQueries.size() * timeMultiplier;
    res.put("promSizeEstimate", sum);
    return res;
  }

  // validate the start & end dates of prometheus metrics dump
  // 1. If both the dates are given; Continue
  // 2. If no dates are specified, download all the exports from last 'x' duration
  private void dateValidation(SupportBundleFormData data) {
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
  }
}
