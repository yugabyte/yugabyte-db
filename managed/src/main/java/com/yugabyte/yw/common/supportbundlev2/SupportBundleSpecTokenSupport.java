// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundlev2;

import java.util.Collections;
import java.util.List;
import java.util.UUID;
import java.util.stream.Collectors;
import org.apache.commons.lang3.StringUtils;

/**
 * Substitutes per-run tokens in support bundle v2 script spec paths and params.
 *
 * <p>Manifest specs are written once and reused for every node and every bundle, so the values that
 * vary per run appear as {@code ${...}} tokens the caller expands here. Only these two tokens are
 * handled; everything else that varies (dates, ports, gflags) is resolved by {@link
 * SupportBundleScriptParamResolver}, which needs the universe and runtime config this class does
 * not take.
 */
final class SupportBundleSpecTokenSupport {

  static final String NODE_NAME_TOKEN = "${nodeName}";
  static final String BUNDLE_UUID_TOKEN = "${bundleUuid}";

  private SupportBundleSpecTokenSupport() {}

  /**
   * A null {@code nodeName} expands to the empty string rather than leaving the token in place: a
   * literal {@code ${nodeName}} would reach the node as part of a path and fail there instead of
   * here. A null {@code bundleUuid} leaves its token alone, because the estimate path has no bundle
   * yet and never emits specs containing it.
   */
  static String substituteSpecTokens(String value, String nodeName, UUID bundleUuid) {
    if (StringUtils.isBlank(value)) {
      return value;
    }
    String resolved = value.replace(NODE_NAME_TOKEN, nodeName == null ? "" : nodeName);
    if (bundleUuid != null) {
      resolved = resolved.replace(BUNDLE_UUID_TOKEN, bundleUuid.toString());
    }
    return resolved;
  }

  static List<String> substituteSpecTokens(List<String> params, String nodeName, UUID bundleUuid) {
    if (params == null) {
      return Collections.emptyList();
    }
    return params.stream()
        .map(p -> substituteSpecTokens(p, nodeName, bundleUuid))
        .collect(Collectors.toList());
  }
}
