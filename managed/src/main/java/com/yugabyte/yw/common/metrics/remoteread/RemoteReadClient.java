// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.metrics.remoteread;

import static com.yugabyte.yw.metrics.MetricQueryHelper.WS_CLIENT_KEY;
import static com.yugabyte.yw.models.helpers.CommonUtils.RESULT_FAILURE;
import static com.yugabyte.yw.models.helpers.CommonUtils.RESULT_SUCCESS;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.google.protobuf.CodedInputStream;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.WSClientRefresher;
import com.yugabyte.yw.common.metrics.tsdb.ByteBufferBitInput;
import com.yugabyte.yw.common.metrics.tsdb.ChunkDecompressor;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import io.prometheus.metrics.core.metrics.Summary;
import io.prometheus.metrics.model.registry.PrometheusRegistry;
import java.io.IOException;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.time.Duration;
import java.time.Instant;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.function.BiConsumer;
import java.util.function.Consumer;
import java.util.stream.Collectors;
import java.util.zip.CRC32C;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.tuple.Pair;
import org.xerial.snappy.Snappy;
import play.libs.ws.WSClient;
import prometheus.Remote;
import prometheus.Remote.ChunkedReadResponse;
import prometheus.Remote.Query;
import prometheus.Remote.ReadRequest;
import prometheus.Remote.ReadRequest.ResponseType;
import prometheus.Types.Chunk;
import prometheus.Types.Chunk.Encoding;
import prometheus.Types.ChunkedSeries;
import prometheus.Types.Label;
import prometheus.Types.LabelMatcher;
import prometheus.Types.LabelMatcher.Type;

/**
 * Client for Prometheus remote read API. Uses {@link ApiHelper} for HTTP and local tsdb chunk
 * decompression. Requires Prometheus remote read proto-generated classes (prometheus.Remote.*,
 * prometheus.Types.*) on the classpath.
 */
@Singleton
@Slf4j
public class RemoteReadClient {

  private static final Summary REMOTE_READ_MILLIS =
      Summary.builder()
          .name("yw_remote_read_millis")
          .help("Metric remote read latency")
          .quantile(0.5, 0.05)
          .quantile(0.9, 0.01)
          .labelNames(KnownAlertLabels.RESULT.labelName())
          .register(PrometheusRegistry.defaultRegistry);

  private static final String REMOTE_READ_PATH = "/api/v1/read";

  private static final Duration DEFAULT_REQUEST_TIMEOUT = Duration.ofSeconds(30);

  private final WSClientRefresher wsClientRefresher;

  @Inject
  public RemoteReadClient(WSClientRefresher wsClientRefresher) {
    this.wsClientRefresher = wsClientRefresher;
  }

  protected ApiHelper getApiHelper() {
    WSClient wsClient = wsClientRefresher.getClient(WS_CLIENT_KEY);
    return new ApiHelper(wsClient, wsClientRefresher.getMaterializer());
  }

  public void readMetrics(
      String url,
      Instant from,
      Instant to,
      Map<String, String> labels,
      BiConsumer<Map<String, String>, List<Pair<Long, Double>>> metricConsumer) {
    readMetricsBinary(
        normalizeUrl(url),
        from,
        to,
        labels,
        inputStream -> {
          try {
            parseChunkedReadResponse(inputStream, metricConsumer);
          } catch (Exception e) {
            throw new RuntimeException("Failed to parse metrics response: " + e.getMessage(), e);
          }
        });
  }

  public void readMetricsBinary(
      String url,
      Instant from,
      Instant to,
      Map<String, String> labels,
      Consumer<InputStream> inputStreamConsumer) {
    long startTime = System.currentTimeMillis();
    Map<String, String> requestHeaders = buildRequestHeaders();

    Remote.ReadRequest.Builder readRequestBuilder =
        Remote.ReadRequest.newBuilder().addAcceptedResponseTypes(ResponseType.STREAMED_XOR_CHUNKS);
    Query.Builder readQueryBuilder =
        Query.newBuilder()
            .setStartTimestampMs(from.toEpochMilli())
            .setEndTimestampMs(to.toEpochMilli());
    labels.forEach(
        (key, value) ->
            readQueryBuilder.addMatchers(
                LabelMatcher.newBuilder().setName(key).setType(Type.EQ).setValue(value).build()));
    readRequestBuilder.addQueries(readQueryBuilder.build());

    ReadRequest readRequest = readRequestBuilder.build();
    byte[] compressedMessage;
    try {
      compressedMessage = Snappy.compress(readRequest.toByteArray());
    } catch (IOException e) {
      throw new RuntimeException("Failed to compress remote read request " + readRequest, e);
    }

    try {
      getApiHelper()
          .postBinaryRequest(
              normalizeUrl(url),
              compressedMessage,
              "application/x-protobuf",
              requestHeaders,
              DEFAULT_REQUEST_TIMEOUT,
              inputStreamConsumer);
      REMOTE_READ_MILLIS
          .labelValues(RESULT_SUCCESS)
          .observe(System.currentTimeMillis() - startTime);
    } catch (Exception e) {
      log.warn("Failed to read metrics.", e);
      REMOTE_READ_MILLIS
          .labelValues(RESULT_FAILURE)
          .observe(System.currentTimeMillis() - startTime);
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
    }
  }

  private static Map<String, String> buildRequestHeaders() {
    Map<String, String> headers = new java.util.HashMap<>();
    headers.put("Content-Encoding", "snappy");
    headers.put("User-Agent", "yugabyte-anywhere");
    return headers;
  }

  private static void parseChunkedReadResponse(
      InputStream inputStream,
      BiConsumer<Map<String, String>, List<Pair<Long, Double>>> metricConsumer)
      throws IOException {
    CodedInputStream codedInputStream = CodedInputStream.newInstance(inputStream);
    List<prometheus.Types.Label> currentLabels = null;
    Map<String, String> currentChunkLabels = null;
    List<Pair<Long, Double>> collectedPoints = new ArrayList<>();
    while (!codedInputStream.isAtEnd()) {
      int size = codedInputStream.readRawVarint32();
      byte[] crc32Bytes = codedInputStream.readRawBytes(4);
      long crc32 = ByteBuffer.wrap(crc32Bytes).getInt() & 0xFFFFFFFFL;
      byte[] message = codedInputStream.readRawBytes(size);
      CRC32C crc32Actual = new CRC32C();
      crc32Actual.update(message);
      if (crc32 != crc32Actual.getValue()) {
        log.error("CRC {} vs actual {}", crc32, crc32Actual.getValue());
        throw new RuntimeException("CRC mismatch during response parsing.");
      }

      ChunkedReadResponse response = ChunkedReadResponse.parseFrom(message);
      for (ChunkedSeries chunks : response.getChunkedSeriesList()) {
        if (currentLabels == null || !currentLabels.equals(chunks.getLabelsList())) {
          if (currentChunkLabels != null && !collectedPoints.isEmpty()) {
            metricConsumer.accept(currentChunkLabels, collectedPoints);
          }
          currentLabels = chunks.getLabelsList();
          currentChunkLabels =
              chunks.getLabelsList().stream()
                  .collect(Collectors.toMap(Label::getName, Label::getValue));
          collectedPoints = new ArrayList<>();
        }
        for (Chunk chunk : chunks.getChunksList()) {
          if (chunk.getType() != Encoding.XOR) {
            log.warn(
                "Encountered {} chunk, skipping. Chunk labels:{}",
                chunk.getType().name(),
                currentChunkLabels);
            continue;
          }
          ByteBufferBitInput bitInput =
              new ByteBufferBitInput(chunk.getData().asReadOnlyByteBuffer());
          ChunkDecompressor decompressor = new ChunkDecompressor(bitInput);
          collectedPoints.addAll(decompressor.readPoints());
        }
      }
    }
    if (currentChunkLabels != null && !collectedPoints.isEmpty()) {
      metricConsumer.accept(currentChunkLabels, collectedPoints);
    }
  }

  private static boolean isRetryableConnectionException(Throwable t) {
    Throwable cause = t;
    if (t.getCause() != null) {
      cause = t.getCause();
    }
    if (cause instanceof java.io.IOException) {
      return true;
    }
    String msg = (cause != null ? cause.getMessage() : t.getMessage());
    if (msg == null) {
      return false;
    }
    String lower = msg.toLowerCase();
    return lower.contains("connection") || lower.contains("refused") || lower.contains("reset");
  }

  private String normalizeUrl(String url) {
    if (!url.endsWith(REMOTE_READ_PATH)) {
      return url.endsWith("/") ? url + REMOTE_READ_PATH.substring(1) : url + REMOTE_READ_PATH;
    }
    return url;
  }
}
