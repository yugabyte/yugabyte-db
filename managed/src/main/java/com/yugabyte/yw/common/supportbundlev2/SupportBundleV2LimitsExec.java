// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundlev2;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;

/**
 * Builds the command line for {@code bin/yb-sb-limits-exec.sh}, which runs one support-bundle
 * command on a DB node under best-effort CPU and memory limits, and reads back the {@code
 * WRAPPER_LIMITS} line it prints after the command.
 */
public final class SupportBundleV2LimitsExec {

  public static final String SCRIPT_NAME = "yb-sb-limits-exec.sh";

  /** Every bundle command on a node runs under this slice, which holds the limits they share. */
  public static final String SLICE = "ybsupportbundle.slice";

  static final String TRAILER_PREFIX = "WRAPPER_LIMITS ";
  static final String NONE = "none";

  /**
   * What to do for CPU where cpu isn't delegated to the user's systemd. FREEZE runs the command in
   * the slice's throttled child, which the node's controller freezes; NONE never freezes it.
   */
  public enum CpuFallback {
    FREEZE,
    NONE
  }

  /** Zero leaves a limit unset. */
  public record Limits(
      int cpuPercent,
      int memoryHighPercent,
      int memoryMaxPercent,
      long runtimeMaxSecs,
      String unitName,
      CpuFallback cpuFallback,
      boolean noCgroup,
      String sliceName) {}

  /** The limits the script reports in force; {@code "none"} where none applied. */
  public record Applied(
      String cpuMode,
      String cpuMax,
      String memoryHigh,
      String memoryMax,
      String unit,
      String reason,
      String slice) {}

  private SupportBundleV2LimitsExec() {}

  /** Runs the staged script through bash, so it works when /tmp is mounted noexec. */
  public static List<String> argv(String remoteScriptPath, Limits limits, List<String> command) {
    if (command == null || command.isEmpty()) {
      throw new IllegalArgumentException("A command is required");
    }
    List<String> argv = new ArrayList<>();
    argv.add("bash");
    argv.add(remoteScriptPath);
    addIfPositive(argv, "--cpu_percent", limits.cpuPercent());
    addIfPositive(argv, "--memory_high_percent", limits.memoryHighPercent());
    addIfPositive(argv, "--memory_max_percent", limits.memoryMaxPercent());
    addIfPositive(argv, "--runtime_max_secs", limits.runtimeMaxSecs());
    if (limits.unitName() != null && !limits.unitName().isEmpty()) {
      argv.add("--unit");
      argv.add(limits.unitName());
    }
    if (limits.sliceName() != null && !limits.sliceName().isEmpty()) {
      argv.add("--slice");
      argv.add(limits.sliceName());
    }
    argv.add("--cpu_fallback");
    argv.add(limits.cpuFallback() == CpuFallback.FREEZE ? "freeze" : NONE);
    if (limits.noCgroup()) {
      argv.add("--no_cgroup");
    }
    // The script ends its options at --exec, not --; see the script's header.
    argv.add("--exec");
    argv.addAll(command);
    return argv;
  }

  public static Optional<Applied> parseTrailer(String output) {
    int start = trailerStart(output);
    if (start < 0) {
      return Optional.empty();
    }
    int end = output.indexOf('\n', start);
    String line =
        output.substring(start + TRAILER_PREFIX.length(), end < 0 ? output.length() : end).trim();
    Map<String, String> fields = new HashMap<>();
    for (String token : line.split(" +")) {
      int eq = token.indexOf('=');
      if (eq > 0) {
        fields.put(token.substring(0, eq), token.substring(eq + 1));
      }
    }
    return Optional.of(
        new Applied(
            fields.getOrDefault("cpu_mode", NONE),
            fields.getOrDefault("cpu_max", NONE),
            fields.getOrDefault("memory_high", NONE),
            fields.getOrDefault("memory_max", NONE),
            fields.getOrDefault("unit", NONE),
            fields.getOrDefault("reason", NONE),
            fields.getOrDefault("slice", NONE)));
  }

  /** Returns the command's own output: the trailer and the newline printed before it removed. */
  public static String stripTrailer(String output) {
    int start = trailerStart(output);
    if (start < 0) {
      return output;
    }
    return output.substring(0, Math.max(start - 1, 0));
  }

  // Only a last line counts: a WRAPPER_LIMITS line followed by more output is the command's own.
  private static int trailerStart(String output) {
    if (output == null) {
      return -1;
    }
    int start = output.lastIndexOf(TRAILER_PREFIX);
    while (start > 0 && output.charAt(start - 1) != '\n') {
      start = output.lastIndexOf(TRAILER_PREFIX, start - 1);
    }
    if (start < 0) {
      return -1;
    }
    int end = output.indexOf('\n', start);
    return end < 0 || output.substring(end).isBlank() ? start : -1;
  }

  private static void addIfPositive(List<String> argv, String flag, long value) {
    if (value > 0) {
      argv.add(flag);
      argv.add(Long.toString(value));
    }
  }
}
