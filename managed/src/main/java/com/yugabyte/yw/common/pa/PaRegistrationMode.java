// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.pa;

import com.yugabyte.yw.common.pa.PerfAdvisorClient.CollectionMode;

/**
 * How a universe is registered with a Perf Advisor Collector, as one choice rather than the two
 * independent PA fields it maps onto.
 *
 * <p>PA ignores {@code metricsExportToPrometheusEnabled} for an ONLINE universe - nothing is stored
 * on this side, so there is no local Prometheus shard to remote-write into - which is what makes
 * these three mutually exclusive instead of a 2x2 with two meaningless cells.
 */
public enum PaRegistrationMode {
  /** Collected and stored locally; no metrics in YBA's Prometheus. */
  BASIC(CollectionMode.LOCAL, false),

  /** Collected and stored locally, and remote-written into YBA's Prometheus. */
  ADVANCED(CollectionMode.LOCAL, true),

  /** Collected locally and forwarded to an external Perf Advisor; nothing is kept here. */
  ONLINE(CollectionMode.ONLINE, false);

  private final CollectionMode collectionMode;
  private final boolean metricsExportToPrometheusEnabled;

  PaRegistrationMode(CollectionMode collectionMode, boolean metricsExportToPrometheusEnabled) {
    this.collectionMode = collectionMode;
    this.metricsExportToPrometheusEnabled = metricsExportToPrometheusEnabled;
  }

  public CollectionMode getCollectionMode() {
    return collectionMode;
  }

  public boolean isMetricsExportToPrometheusEnabled() {
    return metricsExportToPrometheusEnabled;
  }

  /** Whether a destination has to be chosen, and PA will reject the registration without one. */
  public boolean requiresExportConfig() {
    return this == ONLINE;
  }

  /** Whether the universe's data can be read back from this YBA's own Perf Advisor. */
  public boolean storesLocally() {
    return this != ONLINE;
  }

  /**
   * The mode a universe already registered with PA is in. Registrations made before online mode
   * existed carry no collection mode, and PA defaults those to LOCAL.
   */
  public static PaRegistrationMode of(PerfAdvisorClient.UniverseMetadata metadata) {
    if (metadata.getCollectionMode() == CollectionMode.ONLINE) {
      return ONLINE;
    }
    return metadata.isMetricsExportToPrometheusEnabled() ? ADVANCED : BASIC;
  }

  /** Maps the pre-online {@code advancedObservability} boolean onto the equivalent mode. */
  public static PaRegistrationMode of(boolean advancedObservability) {
    return advancedObservability ? ADVANCED : BASIC;
  }
}
