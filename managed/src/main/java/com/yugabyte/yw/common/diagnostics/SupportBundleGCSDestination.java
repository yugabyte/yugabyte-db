package com.yugabyte.yw.common.diagnostics;

import com.google.cloud.storage.BlobId;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;
import java.nio.ByteBuffer;
import lombok.extern.slf4j.Slf4j;

/**
 * GCS destination for support bundle uploads. Extends BaseGCSDestination to eliminate code
 * duplication.
 */
@Slf4j
class SupportBundleGCSDestination extends BaseGCSDestination
    implements BasePublisher.Destination<File> {

  SupportBundleGCSDestination(RuntimeConfigFactory runtimeConfigFactory) {
    super(runtimeConfigFactory, "yb.diag.support_bundles.gcs");
  }

  @Override
  protected String getContentType() {
    return "application/gzip";
  }

  /**
   * Uploads a support bundle file to GCS.
   *
   * @param file The support bundle file to upload
   * @param blobPath The blob path for the upload
   * @return true if upload was successful, false otherwise
   */
  @Override
  public boolean publish(File file, String blobPath) {
    BlobId blobId = BlobId.of(bucket, blobPath);

    return uploadToGCS(
        blobId,
        file.getName(),
        writer -> {
          try (InputStream fileStream = new FileInputStream(file)) {
            // Read file content into ByteBuffer
            byte[] buffer = new byte[8192];
            int bytesRead;
            while ((bytesRead = fileStream.read(buffer)) != -1) {
              ByteBuffer byteBuffer = ByteBuffer.wrap(buffer, 0, bytesRead);
              writer.write(byteBuffer);
            }
          }
        });
  }
}
