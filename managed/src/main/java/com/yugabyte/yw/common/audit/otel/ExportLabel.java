// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.audit.otel;

import com.google.common.collect.ImmutableSet;
import com.yugabyte.yw.common.SwamperHelper.LabelType;
import java.util.Arrays;
import java.util.Set;
import java.util.stream.Collectors;

/**
 * Identity attributes stamped on every exported log record and metric datapoint. Names are taken
 * from {@link LabelType} wherever swamper carries the same fact, so both planes stay queryable with
 * one label name; the rest are export-plane only. Second constructor arg is the legacy {@code
 * yugabyte.} name, emitted alongside while {@code emit_legacy_export_attributes} is set, or null if
 * the attribute never had one.
 */
public enum ExportLabel {
  UNIVERSE_UUID(LabelType.UNIVERSE_UUID, "universe_uuid"),
  NODE_NAME(LabelType.NODE_NAME, "node_name"),
  NODE_ADDRESS(LabelType.NODE_ADDRESS, null),
  NODE_IDENTIFIER(LabelType.NODE_IDENTIFIER, null),
  NODE_PREFIX(LabelType.NODE_PREFIX, null),
  NODE_REGION(LabelType.NODE_REGION, "region"),
  NODE_AZ(LabelType.NODE_AZ, "zone"),
  NODE_CLUSTER_TYPE(LabelType.NODE_CLUSTER_TYPE, "node_type"),
  NODE_CLOUD("node_cloud", "cloud"),
  EXPORT_PURPOSE("export_purpose", "purpose");

  private static final String ATTR_PREFIX_YUGABYTE = "yugabyte.";

  // EXPORTED_INSTANCE is a swamper back-compat artifact; EXPORT_TYPE reaches the export plane as a
  // prometheus scrape label instead.
  public static final Set<LabelType> NOT_EXPORTED =
      ImmutableSet.of(LabelType.EXPORTED_INSTANCE, LabelType.EXPORT_TYPE);

  // Unresolvable in the K8s sidecar: its config is per helm release, not per pod.
  public static final Set<ExportLabel> K8S_UNAVAILABLE =
      ImmutableSet.of(NODE_ADDRESS, NODE_IDENTIFIER);

  private final LabelType swamperLabel;
  private final String attributeName;
  private final String legacyAttributeName;

  ExportLabel(LabelType swamperLabel, String legacySuffix) {
    this.swamperLabel = swamperLabel;
    this.attributeName = swamperLabel.getLabelName();
    this.legacyAttributeName = legacySuffix == null ? null : ATTR_PREFIX_YUGABYTE + legacySuffix;
  }

  ExportLabel(String attributeName, String legacySuffix) {
    this.swamperLabel = null;
    this.attributeName = attributeName;
    this.legacyAttributeName = legacySuffix == null ? null : ATTR_PREFIX_YUGABYTE + legacySuffix;
  }

  public String getAttributeName() {
    return attributeName;
  }

  public String getLegacyAttributeName() {
    return legacyAttributeName;
  }

  public LabelType getSwamperLabel() {
    return swamperLabel;
  }

  public static Set<LabelType> alignedSwamperLabels() {
    return Arrays.stream(values())
        .map(ExportLabel::getSwamperLabel)
        .filter(label -> label != null)
        .collect(Collectors.toSet());
  }
}
