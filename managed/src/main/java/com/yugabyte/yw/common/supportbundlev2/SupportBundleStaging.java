// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundlev2;

import com.yugabyte.yw.common.AppConfigHelper;
import com.yugabyte.yw.common.utils.FileUtils;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;

/**
 * Where a collected archive waits to be untarred: on YBA's storage path, outside the bundle.
 *
 * <p>Inside the bundle, an archive left by a failed copy or gunzip would be packaged with it.
 * {@code java.io.tmpdir} is often too small for a node's logs.
 */
@Slf4j
public final class SupportBundleStaging {

  private static final String STAGING_DIR = "support-bundle-staging";

  private SupportBundleStaging() {}

  public static Path rootFor(UUID bundleUuid) {
    Path base = Paths.get(AppConfigHelper.getStoragePath(), STAGING_DIR);
    return bundleUuid == null ? base : base.resolve(bundleUuid.toString());
  }

  /** A fresh directory for one archive; the caller deletes it. */
  static Path createDir(UUID bundleUuid, String prefix) throws IOException {
    Path root = rootFor(bundleUuid);
    Files.createDirectories(root);
    return Files.createTempDirectory(root, prefix);
  }

  /** At the end of the task, so a bundle that failed or was aborted mid-copy leaves nothing. */
  public static void deleteFor(UUID bundleUuid) {
    if (bundleUuid == null) {
      return;
    }
    Path root = rootFor(bundleUuid);
    if (!Files.exists(root)) {
      return;
    }
    if (FileUtils.deleteDirectory(root.toFile())) {
      log.info("Removed support bundle staging directory '{}'", root);
    } else {
      log.warn("Failed to delete support bundle staging directory '{}'", root);
    }
  }
}
