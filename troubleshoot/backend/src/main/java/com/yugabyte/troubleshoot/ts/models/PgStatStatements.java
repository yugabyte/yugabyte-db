// Copyright (c) Yugabyte, Inc.
package com.yugabyte.troubleshoot.ts.models;

import com.yugabyte.troubleshoot.ts.cvs.DoubleConverter;
import com.yugabyte.troubleshoot.ts.cvs.InstantConverter;
import com.yugabyte.troubleshoot.ts.cvs.UuidConverter;
import io.ebean.Model;
import java.time.Instant;
import java.util.UUID;
import javax.persistence.Entity;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.experimental.Accessors;
import net.sf.jsefa.csv.annotation.CsvDataType;
import net.sf.jsefa.csv.annotation.CsvField;

@Entity
@Data
@EqualsAndHashCode(callSuper = false)
@Accessors(chain = true)
@CsvDataType
public class PgStatStatements extends Model {
  @CsvField(pos = 1, converterType = InstantConverter.class)
  private Instant scheduledTimestamp;

  @CsvField(pos = 2, converterType = InstantConverter.class)
  private Instant actualTimestamp;

  @CsvField(pos = 3, converterType = UuidConverter.class)
  private UUID universeId;

  @CsvField(pos = 4)
  private String nodeName;

  @CsvField(pos = 5)
  private String dbId;

  @CsvField(pos = 6)
  private long queryId;

  @CsvField(pos = 7, converterType = DoubleConverter.class)
  private double rps = Double.NaN;

  @CsvField(pos = 8, converterType = DoubleConverter.class)
  private double rowsAvg = Double.NaN;

  @CsvField(pos = 9, converterType = DoubleConverter.class)
  private double avgLatency = Double.NaN;

  @CsvField(pos = 10, converterType = DoubleConverter.class)
  private Double meanLatency = Double.NaN;

  @CsvField(pos = 11, converterType = DoubleConverter.class)
  private Double p90Latency = Double.NaN;

  @CsvField(pos = 12, converterType = DoubleConverter.class)
  private Double p99Latency = Double.NaN;

  @CsvField(pos = 13, converterType = DoubleConverter.class)
  private Double maxLatency = Double.NaN;
}
