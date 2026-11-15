// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundle;

import java.util.Locale;
import java.util.UUID;
import org.slf4j.Logger;

/**
 * Tracks and logs end-to-end support bundle task timing for v1/v2 comparative analysis.
 *
 * <p>Both task versions emit the same {@value #LOG_PREFIX} line shape, which is the point: v2 is
 * meant to be no slower than v1, and the only way to show that on a real customer universe is to
 * grep both runs out of the same application log and compare. Keep the key=value shape stable -- it
 * is parsed, not just read.
 *
 * <p>Two phases are reported. {@code collection} covers gathering the components; {@code total}
 * also includes tarring, upload and bookkeeping, so the difference between them is the overhead
 * outside collection.
 */
public class SupportBundleTaskTiming {

  public static final String LOG_PREFIX = "SUPPORT_BUNDLE_TIMING";

  private final long totalStartMs;
  private long collectionStartMs = -1;
  private boolean collectionLogged;

  private SupportBundleTaskTiming(long totalStartMs) {
    this.totalStartMs = totalStartMs;
  }

  public static SupportBundleTaskTiming start() {
    return new SupportBundleTaskTiming(System.currentTimeMillis());
  }

  /** Optional. Without it the collection phase is measured from task start instead. */
  public void startCollection() {
    collectionStartMs = System.currentTimeMillis();
  }

  /**
   * Emits at most one {@code phase=collection} line per instance. The call sites are {@code
   * finally} blocks, and a duplicate line would be double-counted by anything aggregating these, so
   * the first call -- the one that measured the real window -- wins.
   *
   * <p>Falls back to task start when {@link #startCollection} was never reached, which happens when
   * the task fails during setup. The number is then wider than the true collection window, but it
   * is bounded and labelled, unlike a negative or absent duration.
   */
  public void logCollectionCompleted(
      Logger log,
      String version,
      UUID bundleUUID,
      UUID taskUUID,
      String universeName,
      String status) {
    if (collectionLogged) {
      return;
    }
    collectionLogged = true;
    long startMs = collectionStartMs >= 0 ? collectionStartMs : totalStartMs;
    logTiming(log, "collection", version, bundleUUID, taskUUID, universeName, status, startMs);
  }

  public void logTotalCompleted(
      Logger log,
      String version,
      UUID bundleUUID,
      UUID taskUUID,
      String universeName,
      String status) {
    logTiming(log, "total", version, bundleUUID, taskUUID, universeName, status, totalStartMs);
  }

  static String formatTimingLogLine(
      String phase,
      String version,
      UUID bundleUUID,
      UUID taskUUID,
      String universeName,
      String status,
      long durationMs) {
    return String.format(
        Locale.US,
        "%s phase=%s version=%s bundleUUID=%s taskUUID=%s universe=%s status=%s"
            + " durationMs=%d durationSec=%.3f",
        LOG_PREFIX,
        phase,
        version,
        bundleUUID,
        taskUUID,
        universeName,
        status,
        durationMs,
        durationMs / 1000.0);
  }

  private static void logTiming(
      Logger log,
      String phase,
      String version,
      UUID bundleUUID,
      UUID taskUUID,
      String universeName,
      String status,
      long startMs) {
    long durationMs = System.currentTimeMillis() - startMs;
    log.info(
        formatTimingLogLine(
            phase, version, bundleUUID, taskUUID, universeName, status, durationMs));
  }
}
