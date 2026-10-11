// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models.helpers.paendpoint;

/**
 * What kind of destination a Perf Advisor Endpoint points at.
 *
 * <p>The kinds carry the same field set - two endpoints, their credentials and the identifiers the
 * destination expects - and differ only in which values are valid, so this discriminates validation
 * rather than the shape of the configuration.
 */
public enum PerfAdvisorEndpointType {
  /** A customer-run Perf Advisor, or the BYOC ingest gateway in front of one. */
  BYOC,

  /**
   * A Yugabyte-hosted Perf Advisor. Accepted by the schema so clients generated today already know
   * the value, and rejected by validation until the hosted service exists.
   */
  PA_ONLINE
}
