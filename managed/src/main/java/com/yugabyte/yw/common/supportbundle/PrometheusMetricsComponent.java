package com.yugabyte.yw.common.supportbundle;

import com.fasterxml.jackson.core.JsonEncoding;
import com.fasterxml.jackson.core.JsonFactory;
import com.fasterxml.jackson.core.JsonGenerator;
import com.fasterxml.jackson.core.util.DefaultPrettyPrinter;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.google.protobuf.ByteString;
import com.google.protobuf.CodedOutputStream;
import com.yugabyte.yw.commissioner.tasks.params.SupportBundleTaskParams;
import com.yugabyte.yw.common.SupportBundleUtil;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.metrics.remoteread.RemoteReadClient;
import com.yugabyte.yw.common.metrics.tsdb.ChunkWriter;
import com.yugabyte.yw.common.metrics.tsdb.OutputStreamBitOutput;
import com.yugabyte.yw.forms.SupportBundleFormData;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.metrics.MetricQueryResponse;
import com.yugabyte.yw.metrics.MetricUrlProvider;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.BundleDetails.PromExportType;
import com.yugabyte.yw.models.helpers.BundleDetails.PrometheusMetricsFormat;
import com.yugabyte.yw.models.helpers.BundleDetails.PrometheusMetricsType;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.io.BufferedOutputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.nio.ByteBuffer;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.Duration;
import java.time.Instant;
import java.util.ArrayList;
import java.util.Date;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.TimeUnit;
import java.util.zip.CRC32C;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.io.FileUtils;
import org.apache.commons.lang3.tuple.Pair;
import play.libs.Json;
import prometheus.Remote;
import prometheus.Types;

@Slf4j
@Singleton
public class PrometheusMetricsComponent implements SupportBundleComponent {

  /**
   * Approximate bytes per time series per 15-minute batch (JSON, downsampled). Used when estimating
   * from PromQL metrics count.
   */
  private static final long BYTES_PER_SERIES_PER_15MIN = 1000L;

  private final MetricQueryHelper metricQueryHelper;
  private final SupportBundleUtil supportBundleUtil;
  private final RemoteReadClient remoteReadClient;
  private final MetricUrlProvider metricUrlProvider;
  private final ObjectMapper objectMapper;

  public final String PROMETHEUS_DUMP_FOLDER = "promdump";
  public final String TOO_MANY_SAMPLES_ERROR_MSG =
      "query processing would load too many samples into memory in query execution";
  public final String TOO_MANY_RESOLUTIONS_ERROR_MSG = "exceeded maximum resolution";

  @Inject private RuntimeConfGetter confGetter;

  @Inject
  public PrometheusMetricsComponent(
      MetricQueryHelper metricQueryHelper,
      SupportBundleUtil supportBundleUtil,
      RemoteReadClient remoteReadClient,
      MetricUrlProvider metricUrlProvider,
      ObjectMapper objectMapper) {
    this.metricQueryHelper = metricQueryHelper;
    this.supportBundleUtil = supportBundleUtil;
    this.remoteReadClient = remoteReadClient;
    this.metricUrlProvider = metricUrlProvider;
    this.objectMapper = objectMapper;
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

  /** Filename for a remote-read batch; suffix is .json for PROMQL_JSON, empty for PROM_CHUNK. */
  private String getRemoteReadFileName(
      String type, Instant batchStart, Instant batchEnd, PrometheusMetricsFormat format) {
    String base =
        type
            + "."
            + batchStart.toString().replace(":", "_")
            + "-"
            + batchEnd.toString().replace(":", "_");
    return format == PrometheusMetricsFormat.PROMQL_JSON ? base + ".json" : base;
  }

  /**
   * Downsample points to at most one per step interval. Points are (timestampMs, value). Keeps
   * first point and then any point whose timestamp is at least stepSecs after the last kept point.
   */
  private List<Pair<Long, Double>> downsampleByStep(List<Pair<Long, Double>> points, int stepSecs) {
    if (points.isEmpty() || stepSecs <= 0) {
      return points;
    }
    long stepMs = TimeUnit.SECONDS.toMillis(stepSecs);
    List<Pair<Long, Double>> result = new ArrayList<>();
    long lastKeptTs = Long.MIN_VALUE;
    for (Pair<Long, Double> point : points) {
      long ts = point.getKey();
      if (result.isEmpty() || (ts - lastKeptTs) >= stepMs) {
        result.add(point);
        lastKeptTs = ts;
      }
    }
    return result;
  }

  /**
   * Write a ChunkedReadResponse message in size-prefixed, CRC32C-checked binary format (same as
   * Prometheus remote read stream).
   */
  private void writeChunkedReadResponseMessage(
      OutputStream out, Remote.ChunkedReadResponse response) throws IOException {
    byte[] messageBytes = response.toByteArray();
    CodedOutputStream codedOutputStream = CodedOutputStream.newInstance(out);
    codedOutputStream.writeRawVarint32(messageBytes.length);
    CRC32C crc32c = new CRC32C();
    crc32c.update(messageBytes);
    ByteBuffer crcByteBuffer = ByteBuffer.allocate(8).putLong(crc32c.getValue());
    codedOutputStream.write(crcByteBuffer.array(), 4, 4);
    codedOutputStream.writeRawBytes(messageBytes);
    codedOutputStream.flush();
  }

  /**
   * Export metrics via Prometheus Remote Read API with 1-hour batching. Uses readMetrics for both
   * formats; points are downsampled by supportBundlePromDumpStepInSecs. PROMQL_JSON: pretty JSON
   * array of {metrics, values}. PROM_CHUNK: binary ChunkedReadResponse (XOR chunks, size-prefixed +
   * CRC32C).
   */
  private void exportMetricsViaRemoteRead(
      SupportBundleFormData data, String destDir, Universe universe, PrometheusMetricsFormat format)
      throws Exception {
    String baseUrl = metricUrlProvider.getMetricsInternalUrl();
    Instant rangeEnd = data.promDumpEndDate.toInstant();
    Instant rangeStart = data.promDumpStartDate.toInstant();
    String nodePrefix =
        universe.getUniverseDetails().nodePrefix != null
            ? universe.getUniverseDetails().nodePrefix
            : "";
    int stepSecs =
        (data.stepPromDumpSecs != null && data.stepPromDumpSecs > 0)
            ? data.stepPromDumpSecs
            : confGetter.getGlobalConf(GlobalConfKeys.supportBundlePromDumpStepInSecs);
    int batchMins =
        (data.batchDurationPromDumpMins != null && data.batchDurationPromDumpMins > 0)
            ? data.batchDurationPromDumpMins
            : confGetter.getGlobalConf(GlobalConfKeys.supportBundlePromDumpBatchDurationInMins);
    final boolean useBinary = (format == PrometheusMetricsFormat.PROM_CHUNK);
    final boolean downsample = data.promDumpDownSample;
    final int effectiveStep = downsample ? stepSecs : 0; // 0 = no downsampling in downsampleByStep

    for (PrometheusMetricsType type : data.prometheusMetricsTypes) {
      String typeName = type.name().toLowerCase();
      String exportDestDir = destDir + "/" + typeName;
      Files.createDirectories(Paths.get(exportDestDir));

      Map<String, String> labels = buildRemoteReadLabels(type, typeName, nodePrefix);

      Instant batchEnd = rangeEnd;
      while (batchEnd.isAfter(rangeStart)) {
        Instant batchStart = batchEnd.minus(Duration.ofMinutes(batchMins));
        if (batchStart.isBefore(rangeStart)) {
          batchStart = rangeStart;
        }
        String fileName = getRemoteReadFileName(typeName, batchStart, batchEnd, format);
        Path metricsFile = Paths.get(exportDestDir, fileName);

        log.info(
            "Writing metrics via Remote Read between {} and {} to {} (downsample={}, step {}s)",
            batchStart,
            batchEnd,
            metricsFile,
            downsample,
            downsample ? stepSecs : 0);
        try (FileOutputStream fout = new FileOutputStream(metricsFile.toFile());
            BufferedOutputStream bufos = new BufferedOutputStream(fout)) {
          if (useBinary) {
            writeMetricsBinary(baseUrl, batchStart, batchEnd, labels, effectiveStep, bufos);
          } else {
            writeMetricsJson(baseUrl, batchStart, batchEnd, labels, effectiveStep, bufos);
          }
        }
        batchEnd = batchStart;
      }
    }
  }

  private void writeMetricsBinary(
      String baseUrl,
      Instant batchStart,
      Instant batchEnd,
      Map<String, String> labels,
      int effectiveStep,
      OutputStream output)
      throws IOException {
    List<Pair<Map<String, String>, List<Pair<Long, Double>>>> seriesList = new ArrayList<>();
    remoteReadClient.readMetrics(
        baseUrl,
        batchStart,
        batchEnd,
        labels,
        (metricsLabels, points) -> {
          List<Pair<Long, Double>> stepped = downsampleByStep(points, effectiveStep);
          if (!stepped.isEmpty()) {
            seriesList.add(Pair.of(metricsLabels, stepped));
          }
        });
    Remote.ChunkedReadResponse.Builder responseBuilder = Remote.ChunkedReadResponse.newBuilder();
    for (Pair<Map<String, String>, List<Pair<Long, Double>>> series : seriesList) {
      Map<String, String> metricLabels = series.getLeft();
      List<Pair<Long, Double>> points = series.getRight();
      ByteArrayOutputStream chunkBaos = new ByteArrayOutputStream();
      ChunkWriter chunkWriter = new ChunkWriter(new OutputStreamBitOutput(chunkBaos));
      chunkWriter.write(points);
      chunkWriter.flush();
      Types.ChunkedSeries.Builder seriesBuilder = Types.ChunkedSeries.newBuilder();
      for (Map.Entry<String, String> e : metricLabels.entrySet()) {
        seriesBuilder.addLabels(
            Types.Label.newBuilder().setName(e.getKey()).setValue(e.getValue()).build());
      }
      seriesBuilder.addChunks(
          Types.Chunk.newBuilder()
              .setType(Types.Chunk.Encoding.XOR)
              .setData(ByteString.copyFrom(chunkBaos.toByteArray()))
              .build());
      responseBuilder.addChunkedSeries(seriesBuilder.build());
    }
    writeChunkedReadResponseMessage(output, responseBuilder.build());
  }

  private void writeMetricsJson(
      String baseUrl,
      Instant batchStart,
      Instant batchEnd,
      Map<String, String> labels,
      int effectiveStep,
      OutputStream output)
      throws IOException {
    JsonFactory jfactory = new JsonFactory();
    try (JsonGenerator jGenerator =
        jfactory.createGenerator(output, JsonEncoding.UTF8).setCodec(objectMapper)) {
      jGenerator.setPrettyPrinter(new DefaultPrettyPrinter());
      jGenerator.writeStartArray();
      remoteReadClient.readMetrics(
          baseUrl,
          batchStart,
          batchEnd,
          labels,
          (metricsLabels, points) -> {
            try {
              List<Pair<Long, Double>> stepped = downsampleByStep(points, effectiveStep);
              jGenerator.writeStartObject();
              jGenerator.writeObjectField("metrics", metricsLabels);
              jGenerator.writeFieldName("values");
              jGenerator.writeStartArray();
              for (Pair<Long, Double> point : stepped) {
                jGenerator.writeStartArray();
                jGenerator.writeNumber((long) (point.getKey() / 1000));
                jGenerator.writeString(String.valueOf(point.getValue()));
                jGenerator.writeEndArray();
              }
              jGenerator.writeEndArray();
              jGenerator.writeEndObject();
            } catch (IOException e) {
              throw new RuntimeException("Failed to write metrics", e);
            }
          });
      jGenerator.writeEndArray();
      jGenerator.flush();
    }
  }

  private Map<String, String> buildRemoteReadLabels(
      PrometheusMetricsType type, String typeName, String nodePrefix) {
    Map<String, String> labels = new HashMap<>();
    switch (type) {
      case PROMETHEUS:
        labels.put("job", "prometheus");
        break;
      default:
        labels.put("export_type", typeName);
        labels.put("node_prefix", nodePrefix);
    }
    return labels;
  }

  /**
   * Build PromQL series selector for the given type (same as used for export). Used to run
   * count(selector) for size estimation.
   */
  private String buildPromQLSelector(
      PrometheusMetricsType type, String typeName, String nodePrefix) {
    if (type == PrometheusMetricsType.PLATFORM || type == PrometheusMetricsType.PROMETHEUS) {
      return String.format(
          "{job=\"%s\",node_prefix=\"%s\"}",
          typeName, type == PrometheusMetricsType.PLATFORM ? nodePrefix : "");
    }
    return String.format("{export_type=\"%s\",node_prefix=\"%s\"}", typeName, nodePrefix);
  }

  /**
   * Get the number of time series matching the selector at the given time via PromQL count() using
   * instant query (queryDirect with time). Returns null on failure or if metrics URL is not
   * available.
   */
  private Long getMetricsCountAtTime(String promQLSelector, Date atTime) {
    ArrayList<MetricQueryResponse.Entry> entries =
        metricQueryHelper.queryDirect("count(" + promQLSelector + ")", atTime.toInstant());
    if (entries == null || entries.isEmpty()) {
      log.warn("No entries returned for {}", promQLSelector);
      return 0L;
    }
    MetricQueryResponse.Entry first = entries.get(0);
    if (first.values == null || first.values.isEmpty()) {
      log.warn("No values returned for {}", promQLSelector);
      return 0L;
    }
    return first.values.get(0).getRight().longValue();
  }

  public Duration exportMetric(
      Date startDate,
      Date endDate,
      String query,
      String nodeName,
      String type,
      String metricName,
      String exportDestDir,
      Duration initialBatchDuration,
      SupportBundleFormData bundleData)
      throws Exception {
    try {
      // Get the batch duration from request params, provided value (obtained from previous
      // exportMetric call) or global runtime-config
      Duration effectiveBatchDuration;
      Integer batchMins = bundleData != null ? bundleData.batchDurationPromDumpMins : null;
      if (batchMins != null && batchMins > 0) {
        effectiveBatchDuration = Duration.ofMinutes(batchMins);
      } else if (initialBatchDuration != null) {
        effectiveBatchDuration = initialBatchDuration;
      } else {
        int batchDuration =
            confGetter.getGlobalConf(GlobalConfKeys.supportBundlePromDumpBatchDurationInMins);
        effectiveBatchDuration = Duration.ofMinutes(batchDuration);
      }

      int stepSecs;
      Integer stepOverride = bundleData != null ? bundleData.stepPromDumpSecs : null;
      if (stepOverride != null && stepOverride > 0) {
        stepSecs = stepOverride;
      } else {
        stepSecs = confGetter.getGlobalConf(GlobalConfKeys.supportBundlePromDumpStepInSecs);
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
        queryParams.put("step", String.valueOf(stepSecs));

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
      boolean splitTableLevelMetrics,
      SupportBundleFormData bundleData)
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
                effectiveBatchDuration,
                bundleData);

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
                  effectiveBatchDuration,
                  bundleData);
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
            null,
            bundleData);
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

    if (data.promExportType == PromExportType.REMOTE_READ
        && data.prometheusMetricsTypes != null
        && !data.prometheusMetricsTypes.isEmpty()) {
      PrometheusMetricsFormat format =
          data.promMetricsFormat != null
              ? data.promMetricsFormat
              : PrometheusMetricsFormat.PROMQL_JSON;
      exportMetricsViaRemoteRead(data, destDir, universe, format);
      return;
    }

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
                      new Date(startTime),
                      new Date(endTime),
                      query,
                      null,
                      typeName,
                      null,
                      exportDestDir,
                      null,
                      data);
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
                      splitTableLevelMetrics,
                      data);
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
                    new Date(startTime),
                    new Date(endTime),
                    query,
                    null,
                    queryType,
                    null,
                    exportDestDir,
                    null,
                    data);
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
  // We estimate using metrics count at the end of the collection interval (PromQL count()),
  // assuming count does not change over time.
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
    Long timeMultiplier = (totalMins + 14) / 15;
    Map<String, Long> res = new HashMap<String, Long>();
    Date countAtTime = bundleData.promDumpEndDate;
    String nodePrefix =
        universe.getUniverseDetails().nodePrefix != null
            ? universe.getUniverseDetails().nodePrefix
            : "";

    long totalSeriesCount = 0;
    for (PrometheusMetricsType type : bundleData.prometheusMetricsTypes) {
      String typeName = type.name().toLowerCase();
      String selector = buildPromQLSelector(type, typeName, nodePrefix);
      Long count = getMetricsCountAtTime(selector, countAtTime);
      totalSeriesCount += count;
    }

    long sum = totalSeriesCount * BYTES_PER_SERIES_PER_15MIN * timeMultiplier;
    log.debug(
        "Prometheus JSON downsampled estimate from metrics count at {}: {} series, estimate {}"
            + " bytes",
        countAtTime,
        totalSeriesCount,
        sum);

    // Above is the calculation for JSON format with 1 minute downsampling
    // Binary without downsampling is nearly the same size uncompressed
    // Binary with downsampling - 2 times less
    if (bundleData.promExportType == PromExportType.REMOTE_READ) {
      if (bundleData.promMetricsFormat == PrometheusMetricsFormat.PROM_CHUNK
          && bundleData.promDumpDownSample) {
        sum = (long) (sum * 0.5);
      }
      if (bundleData.promMetricsFormat == PrometheusMetricsFormat.PROMQL_JSON
          && !bundleData.promDumpDownSample) {
        sum = sum * 3;
      }
    }

    // It's not possible to estimate the size correctly for custom queries since we don't
    // know what queries will be passed in.
    // Adding 5MB per query per 15 minutes to account for custom queries.
    // Custom queries are only applicable to JSON with downsampling - so no need to adjust.
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
