package com.yugabyte.yw.common.diagnostics;

import static org.threeten.bp.temporal.ChronoUnit.SECONDS;

import com.google.api.gax.retrying.RetrySettings;
import com.google.cloud.WriteChannel;
import com.google.cloud.storage.BlobId;
import com.google.cloud.storage.BlobInfo;
import com.google.cloud.storage.Storage;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.GCPUtil;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import java.io.FileInputStream;
import java.io.InputStream;
import java.io.StringWriter;
import java.nio.channels.Channels;
import java.nio.charset.StandardCharsets;
import java.time.OffsetDateTime;
import java.time.ZoneId;
import java.time.format.DateTimeFormatter;
import java.util.zip.GZIPOutputStream;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.io.input.CharSequenceInputStream;
import org.threeten.bp.Duration;

@Slf4j
class GCSDestination implements ThreadDumpPublisher.Destination {
  private final String basePath;
  private final String bucket;
  private final String credentialsPath;

  GCSDestination(RuntimeConfigFactory runtimeConfigFactory, String instanceName) {
    Config staticConf = runtimeConfigFactory.staticApplicationConf();
    String namespace =
        staticConf.hasPath("yb.diag.namespace") ? staticConf.getString("yb.diag.namespace") : null;

    bucket = staticConf.getString("yb.diag.thread_dumps.gcs.bucket");
    credentialsPath = staticConf.getString("yb.diag.thread_dumps.gcs.credentials");
    basePath =
        String.join(
            "/",
            "thread-dumps",
            "yba",
            namespace == null ? instanceName : namespace + "/" + instanceName);
  }

  /**
   * Posts a gzipped thread dump to the specified bucket under
   * /thread-dumps/yba/{instance}/{date}/{hour}/{UTC time}
   *
   * @param threadDump char stream with the thread dump contents
   */
  @Override
  public boolean publish(StringWriter threadDump) {
    OffsetDateTime now = OffsetDateTime.now(ZoneId.of("UTC"));
    BlobId blobId =
        BlobId.of(
            bucket,
            String.join(
                "/",
                basePath,
                DateTimeFormatter.ISO_LOCAL_DATE.format(now),
                now.getHour() + ":00",
                DateTimeFormatter.ISO_LOCAL_TIME.format(now)));
    BlobInfo blobInfo =
        BlobInfo.newBuilder(blobId).setContentType("text/plain").setContentEncoding("gzip").build();

    try (InputStream creds = new FileInputStream(credentialsPath)) {
      Storage storage =
          GCPUtil.getStorageService(
              creds,
              RetrySettings.newBuilder()
                  .setMaxAttempts(3)
                  .setInitialRetryDelay(Duration.of(3, SECONDS))
                  .setMaxRetryDelay(Duration.of(25, SECONDS))
                  .setTotalTimeout(Duration.of(60, SECONDS))
                  .setInitialRpcTimeout(Duration.of(10, SECONDS))
                  .setMaxRpcTimeout(Duration.of(25, SECONDS))
                  .setRetryDelayMultiplier(2.0)
                  .build());
      try (WriteChannel writer = storage.writer(blobInfo);
          GZIPOutputStream gzipOutputStream =
              new GZIPOutputStream(Channels.newOutputStream(writer));
          InputStream is =
              new CharSequenceInputStream(threadDump.getBuffer(), StandardCharsets.UTF_8)) {
        log.warn("Sending to {}", blobId.getName());
        gzipOutputStream.write(is.readAllBytes());
      }
    } catch (Exception e) {
      log.error(String.format("Could not publish thread dump to %s", bucket), e);
      return false;
    }

    return true;
  }
}
