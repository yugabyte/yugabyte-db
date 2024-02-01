// Copyright (c) Yugabyte, Inc.
package com.yugabyte.troubleshoot.ts.models;

import io.ebean.Model;
import java.time.Instant;
import java.util.UUID;
import javax.persistence.Entity;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.experimental.Accessors;

@Entity
@Data
@EqualsAndHashCode(callSuper = false)
@Accessors(chain = true)
public class PgStatStatements extends Model {
  private Instant scheduledTimestamp;
  private Instant actualTimestamp;

  private UUID universeId;
  private String nodeName;
  private long queryId;

  private double rps;
  private double rowsAvg;
  private double avgLatency;
  private Double meanLatency;
  private Double p90Latency;
  private Double p99Latency;
  private Double maxLatency;
}
