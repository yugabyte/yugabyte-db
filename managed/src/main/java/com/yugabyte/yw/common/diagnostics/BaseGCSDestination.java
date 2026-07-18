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
import lombok.extern.slf4j.Slf4j;
import org.threeten.bp.Duration;

/**
 * Base class for GCS destinations that eliminates code duplication between GCSDestination and
 * SupportBundleGCSDestination.
 */
@Slf4j
abstract class BaseGCSDestination {
  protected final String bucket;
  protected final String credentialsPath;
  private final String configPrefix;
  private final RuntimeConfigFactory runtimeConfigFactory;

  protected BaseGCSDestination(RuntimeConfigFactory runtimeConfigFactory, String configPrefix) {
    this.configPrefix = configPrefix;
    this.runtimeConfigFactory = runtimeConfigFactory;
    Config staticConf = runtimeConfigFactory.staticApplicationConf();
    bucket = staticConf.getString(configPrefix + ".bucket");
    credentialsPath = staticConf.getString(configPrefix + ".credentials");
  }

  /**
   * Common GCS upload logic that handles Storage service initialization and retry settings.
   * Subclasses implement the specific content writing logic.
   *
   * @param blobId The blob ID for the upload
   * @param resourceName The name of the resource being uploaded (for logging)
   * @param contentWriter A lambda that writes content to the WriteChannel
   * @return true if upload was successful, false otherwise
   */
  protected boolean uploadToGCS(BlobId blobId, String resourceName, ContentWriter contentWriter) {
    if (!enabled()) {
      return true;
    }

    BlobInfo blobInfo =
        BlobInfo.newBuilder(blobId)
            .setContentType(getContentType())
            .setContentEncoding("gzip")
            .build();

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

      try (WriteChannel writer = storage.writer(blobInfo)) {
        log.info("Uploading {} to {}", resourceName, blobId.getName());
        contentWriter.writeContent(writer);
      }
    } catch (Exception e) {
      log.error(String.format("Could not upload %s to %s", resourceName, bucket), e);
      return false;
    }

    return true;
  }

  /**
   * Returns the content type for the file being uploaded. Subclasses should override this to
   * specify their content type.
   */
  protected abstract String getContentType();

  public boolean enabled() {
    return runtimeConfigFactory.globalRuntimeConf().getBoolean(configPrefix + ".enabled");
  }

  /**
   * Functional interface for writing content to a WriteChannel. This allows different content types
   * to be uploaded using the same base logic.
   */
  @FunctionalInterface
  protected interface ContentWriter {
    void writeContent(WriteChannel writer) throws Exception;
  }
}
