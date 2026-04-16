package com.yugabyte.troubleshoot.ts.models;

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
public class ActiveSessionHistoryQueryStateId {
  @CsvField(pos = 1, converterType = UuidConverter.class)
  private UUID universeId;

  @CsvField(pos = 2)
  private String nodeName;
}
