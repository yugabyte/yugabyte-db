// Copyright (c) Yugabyte, Inc.
package com.yugabyte.troubleshoot.ts.models;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.yugabyte.troubleshoot.ts.cvs.InstantConverter;
import io.ebean.Model;
import java.time.Instant;
import javax.persistence.EmbeddedId;
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
public class PgStatStatementsQuery extends Model implements ModelWithId<PgStatStatementsQueryId> {

  @CsvField(pos = 1)
  @EmbeddedId
  private PgStatStatementsQueryId id;

  @CsvField(pos = 2, converterType = InstantConverter.class)
  @JsonFormat(pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'", timezone = "UTC")
  private Instant scheduledTimestamp;

  @CsvField(pos = 3)
  private String query;

  @CsvField(pos = 4)
  private String dbName;

  @CsvField(pos = 5, converterType = InstantConverter.class)
  @JsonFormat(pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'", timezone = "UTC")
  private Instant lastActive;
}
