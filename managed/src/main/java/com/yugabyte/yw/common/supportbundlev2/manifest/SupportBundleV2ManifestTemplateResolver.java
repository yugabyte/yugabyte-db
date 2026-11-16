// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundlev2.manifest;

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.supportbundlev2.manifest.SupportBundleV2Manifest.Spec;
import java.time.Instant;
import java.time.ZoneOffset;
import java.time.format.DateTimeFormatter;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import org.apache.commons.lang3.StringUtils;

/**
 * Resolves the {@code ${...}} tokens the manifest uses and reads typed values out of a component's
 * free form {@code params} map.
 *
 * <p>Port of the {@code defaultTemplateVariables} / {@code resolveTemplateString} / {@code
 * resolveConfig*} helpers in {@code yba-cli/cmd/universe/supportbundlev2/supportbundlev2.go}. A
 * missing or unparseable value falls back to the accessor's default, as in Go. An int value out of
 * range is a 500 instead.
 *
 * <p>Tokens the manifest leaves unresolved on purpose, {@code ${nodeName}} and {@code
 * ${bundleUuid}}, are absent from the variable map so they survive into the emitted spec. The task
 * substitutes them per node.
 */
public final class SupportBundleV2ManifestTemplateResolver {

  /**
   * Templates can reference other templates ({@code nodeDataDirTemplate} expands {@code
   * ${mountPath}}), so substitution repeats until it reaches a fixed point. The cap keeps a
   * manifest with a token cycle from looping forever.
   */
  private static final int MAX_TEMPLATE_PASSES = 5;

  private static final DateTimeFormatter TIMESTAMP_UTC =
      DateTimeFormatter.ofPattern("yyyyMMdd'T'HHmmss'Z'").withZone(ZoneOffset.UTC);

  private SupportBundleV2ManifestTemplateResolver() {}

  /** Returns a fresh mutable map; builders add their own {@code params.*} entries to it. */
  public static Map<String, String> defaultTemplateVariables(Spec spec) {
    Map<String, String> vars = new LinkedHashMap<>();
    vars.put("defaults.mountPath", nullToEmpty(spec.getDefaults().getMountPath()));
    vars.put(
        "defaults.nodeDataDirTemplate", nullToEmpty(spec.getDefaults().getNodeDataDirTemplate()));
    vars.put("defaults.ybHomeDir", nullToEmpty(spec.getDefaults().getYbHomeDir()));
    vars.put("defaults.remoteTmpDir", nullToEmpty(spec.getDefaults().getRemoteTmpDir()));
    vars.put(
        "defaults.nodeAgentInstallPath", nullToEmpty(spec.getDefaults().getNodeAgentInstallPath()));
    vars.put(
        "collectionTimeRange.startDate", nullToEmpty(spec.getCollectionTimeRange().getStartDate()));
    vars.put(
        "collectionTimeRange.endDate", nullToEmpty(spec.getCollectionTimeRange().getEndDate()));
    // Shorthand used by defaults.nodeDataDirTemplate ("${mountPath}/yb-data").
    vars.put("mountPath", nullToEmpty(spec.getDefaults().getMountPath()));

    vars.put(
        "defaults.nodeDataDirTemplate",
        resolveTemplateString(spec.getDefaults().getNodeDataDirTemplate(), vars));

    String timestampUtc = spec.getRequestBuildTimeUtc();
    if (StringUtils.isBlank(timestampUtc)) {
      timestampUtc = TIMESTAMP_UTC.format(Instant.now());
    }
    vars.put("currentTimestampUTC", timestampUtc);
    return vars;
  }

  public static String resolveTemplateString(String input, Map<String, String> vars) {
    String resolved = nullToEmpty(input);
    for (int pass = 0; pass < MAX_TEMPLATE_PASSES; pass++) {
      String previous = resolved;
      for (Map.Entry<String, String> var : vars.entrySet()) {
        resolved = resolved.replace("${" + var.getKey() + "}", nullToEmpty(var.getValue()));
      }
      if (previous.equals(resolved)) {
        break;
      }
    }
    return resolved;
  }

  public static String resolveConfigString(
      Map<String, Object> params, String key, Map<String, String> vars) {
    Object raw = rawValue(params, key);
    if (raw == null) {
      return "";
    }
    return resolveTemplateString(String.valueOf(raw), vars);
  }

  public static boolean resolveConfigBool(
      Map<String, Object> params, String key, boolean defaultValue, Map<String, String> vars) {
    Object raw = rawValue(params, key);
    if (raw == null) {
      return defaultValue;
    }
    if (raw instanceof Boolean) {
      return (Boolean) raw;
    }
    String value =
        raw instanceof String
            ? resolveTemplateString((String) raw, vars)
            : String.valueOf(raw).trim();
    if ("true".equalsIgnoreCase(value)) {
      return true;
    }
    if ("false".equalsIgnoreCase(value)) {
      return false;
    }
    return defaultValue;
  }

  public static int resolveConfigInt(
      Map<String, Object> params, String key, int defaultValue, Map<String, String> vars) {
    long resolved = resolveConfigLong(params, key, defaultValue, vars);
    try {
      return Math.toIntExact(resolved);
    } catch (ArithmeticException e) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR,
          String.format("Support bundle manifest value %s=%d is out of range", key, resolved));
    }
  }

  public static long resolveConfigLong(
      Map<String, Object> params, String key, long defaultValue, Map<String, String> vars) {
    Object raw = rawValue(params, key);
    if (raw == null) {
      return defaultValue;
    }
    if (raw instanceof Number) {
      return ((Number) raw).longValue();
    }
    String value =
        raw instanceof String
            ? resolveTemplateString((String) raw, vars)
            : String.valueOf(raw).trim();
    try {
      return Long.parseLong(value.trim());
    } catch (NumberFormatException e) {
      return defaultValue;
    }
  }

  /**
   * Accepts a yaml list or a scalar, dropping entries that resolve to empty. A scalar is one
   * element, never a comma list: queries, yb-admin commands and file names contain commas. Same
   * rule as the CLI's resolveConfigStringSliceParam.
   */
  public static List<String> resolveConfigStringList(
      Map<String, Object> params, String key, Map<String, String> vars) {
    Object raw = rawValue(params, key);
    if (raw == null) {
      return new ArrayList<>();
    }

    List<String> candidates = new ArrayList<>();
    if (raw instanceof Iterable) {
      for (Object item : (Iterable<?>) raw) {
        candidates.add(item == null ? "" : String.valueOf(item));
      }
    } else {
      candidates.add(String.valueOf(raw));
    }

    List<String> resolved = new ArrayList<>(candidates.size());
    for (String candidate : candidates) {
      String value = resolveTemplateString(candidate.trim(), vars);
      if (StringUtils.isNotBlank(value)) {
        resolved.add(value);
      }
    }
    return resolved;
  }

  public static Map<String, String> resolveConfigStringMap(
      Map<String, Object> params, String key, Map<String, String> vars) {
    Object raw = rawValue(params, key);
    if (!(raw instanceof Map)) {
      return new LinkedHashMap<>();
    }
    Map<String, String> resolved = new LinkedHashMap<>();
    for (Map.Entry<?, ?> entry : ((Map<?, ?>) raw).entrySet()) {
      resolved.put(
          String.valueOf(entry.getKey()),
          resolveTemplateString(
              entry.getValue() == null ? "" : String.valueOf(entry.getValue()), vars));
    }
    return resolved;
  }

  /**
   * Returns null when the key is absent or holds something that is not recognisably a boolean, so
   * the caller can tell "manifest said false" from "manifest said nothing".
   */
  public static Boolean optionalConfigBool(Map<String, Object> params, String key) {
    Object raw = rawValue(params, key);
    if (raw instanceof Boolean) {
      return (Boolean) raw;
    }
    if (raw instanceof String) {
      String value = ((String) raw).trim();
      if ("true".equalsIgnoreCase(value)) {
        return Boolean.TRUE;
      }
      if ("false".equalsIgnoreCase(value)) {
        return Boolean.FALSE;
      }
    }
    return null;
  }

  /**
   * {@code maxNumRecentCores} becomes {@code max_num_recent_cores} for the generic argv builder.
   */
  public static String camelToSnake(String input) {
    if (input == null) {
      return "";
    }
    StringBuilder builder = new StringBuilder(input.length() + 4);
    for (int i = 0; i < input.length(); i++) {
      char c = input.charAt(i);
      if (i > 0 && c >= 'A' && c <= 'Z') {
        builder.append('_');
      }
      builder.append(c);
    }
    return builder.toString().toLowerCase();
  }

  private static Object rawValue(Map<String, Object> params, String key) {
    return params == null ? null : params.get(key);
  }

  private static String nullToEmpty(String value) {
    return value == null ? "" : value;
  }
}
