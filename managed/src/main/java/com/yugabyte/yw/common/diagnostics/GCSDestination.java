package com.yugabyte.yw.common.diagnostics;

import com.google.cloud.storage.BlobId;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import java.io.InputStream;
import java.io.StringWriter;
import java.nio.channels.Channels;
import java.nio.charset.StandardCharsets;
import java.util.zip.GZIPOutputStream;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.io.input.CharSequenceInputStream;

@Slf4j
class GCSDestination extends BaseGCSDestination implements BasePublisher.Destination<StringWriter> {

  GCSDestination(RuntimeConfigFactory runtimeConfigFactory) {
    super(runtimeConfigFactory, "yb.diag.thread_dumps.gcs");
  }

  @Override
  protected String getContentType() {
    return "text/plain";
  }

  /**
   * Posts a gzipped thread dump to the specified bucket.
   *
   * @param threadDump char stream with the thread dump contents
   * @param blobPath the blob path for the upload
   */
  @Override
  public boolean publish(StringWriter threadDump, String blobPath) {
    BlobId blobId = BlobId.of(bucket, blobPath);

    return uploadToGCS(
        blobId,
        "thread dump",
        writer -> {
          try (GZIPOutputStream gzipOutputStream =
                  new GZIPOutputStream(Channels.newOutputStream(writer));
              InputStream is =
                  CharSequenceInputStream.builder()
                      .setCharset(StandardCharsets.UTF_8)
                      .setCharSequence(threadDump.getBuffer())
                      .get()) {
            gzipOutputStream.write(is.readAllBytes());
          }
        });
  }
}
