// Copyright (c) Yugabyte, Inc.
package com.yugabyte.troubleshoot.ts.models;

import com.fasterxml.jackson.databind.JsonNode;
import io.ebean.Model;
import io.ebean.annotation.DbJsonB;
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

  private UUID customerId;
  private UUID universeId;
  private UUID clusterId;
  private String nodeName;
  private String cloud;
  private String region;
  private String az;

  private String dbId;
  private String dbName;
  private long queryId;
  private String query;
  private long calls;
  private double totalTime;
  private long rows;
  @DbJsonB private JsonNode ybLatencyHistogram;
}
