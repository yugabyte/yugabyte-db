// Copyright (c) Yugabyte, Inc.
package com.yugabyte.troubleshoot.ts.models;

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
public class ActiveSessionHistoryQueryState extends Model
    implements ModelWithId<ActiveSessionHistoryQueryStateId> {

  @CsvField(pos = 1)
  @EmbeddedId
  private ActiveSessionHistoryQueryStateId id;

  @CsvField(pos = 2, converterType = InstantConverter.class)
  private Instant lastSampleTime;
}
