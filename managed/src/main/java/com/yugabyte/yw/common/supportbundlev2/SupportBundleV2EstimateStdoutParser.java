// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundlev2;

import java.util.Optional;
import org.apache.commons.lang3.StringUtils;

/**
 * Parses stdout markers emitted by support bundle v2 estimate bash entrypoints.
 *
 * <p>The estimate entrypoints reuse the collection scripts in a measure-only mode and report
 * through {@code ESTIMATE_STATUS} / {@code ESTIMATE_SIZE} lines, mirroring the {@code
 * BUNDLE_STATUS} protocol documented on {@link AbstractScriptTarNodeComponent}. An empty {@link
 * Optional} means "no usable number", which callers report as an unknown size rather than as zero
 * bytes -- the two are different answers for an operator sizing a bundle.
 */
final class SupportBundleV2EstimateStdoutParser {

  private static final String ESTIMATE_STATUS_PREFIX = "ESTIMATE_STATUS=";
  private static final String ESTIMATE_SIZE_PREFIX = "ESTIMATE_SIZE=";

  private SupportBundleV2EstimateStdoutParser() {}

  /**
   * {@code EMPTY} is a definite zero and wins over a missing size, since a script that matched no
   * files exits before printing one. Anything else without a well-formed {@code ESTIMATE_SIZE} is
   * unknown: a size that fails to parse returns empty immediately rather than falling back to a
   * later line, so a garbled number is never silently rounded into the total.
   */
  static Optional<Long> parseEstimateSize(String output) {
    if (StringUtils.isBlank(output)) {
      return Optional.empty();
    }
    String status = null;
    Long size = null;
    for (String line : output.split("\\R")) {
      String trimmed = line.trim();
      if (trimmed.startsWith(ESTIMATE_STATUS_PREFIX)) {
        status = trimmed.substring(ESTIMATE_STATUS_PREFIX.length()).trim();
      } else if (trimmed.startsWith(ESTIMATE_SIZE_PREFIX)) {
        String raw = trimmed.substring(ESTIMATE_SIZE_PREFIX.length()).trim();
        if (StringUtils.isNotBlank(raw)) {
          try {
            size = Long.parseLong(raw);
          } catch (NumberFormatException ignored) {
            return Optional.empty();
          }
        }
      }
    }
    if ("EMPTY".equals(status)) {
      return Optional.of(0L);
    }
    if (!"OK".equals(status) || size == null) {
      return Optional.empty();
    }
    return Optional.of(size);
  }
}
