package com.yugabyte.troubleshoot.ts.models;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.yugabyte.troubleshoot.ts.cvs.UuidConverter;
import java.util.UUID;
import javax.persistence.Embeddable;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.experimental.Accessors;
import net.sf.jsefa.csv.annotation.CsvDataType;
import net.sf.jsefa.csv.annotation.CsvField;

@Embeddable
@Data
@Accessors(chain = true)
@EqualsAndHashCode(callSuper = false)
@CsvDataType
public class PgStatStatementsQueryId {
  @CsvField(pos = 1, converterType = UuidConverter.class)
  private UUID universeId;

  @CsvField(pos = 2)
  private String dbId;

  @CsvField(pos = 3)
  @JsonIgnore
  private long queryId;

  @JsonProperty("queryId")
  public String getQueryIdAsString() {
    return String.valueOf(queryId);
  }
}
