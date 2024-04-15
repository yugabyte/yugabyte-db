package com.yugabyte.troubleshoot.ts.models;

import com.yugabyte.troubleshoot.ts.cvs.InstantConverter;
import com.yugabyte.troubleshoot.ts.cvs.UuidConverter;
import io.ebean.Model;
import java.time.Instant;
import java.util.UUID;
import javax.persistence.Entity;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.experimental.Accessors;
import net.sf.jsefa.common.converter.LongConverter;
import net.sf.jsefa.csv.annotation.CsvDataType;
import net.sf.jsefa.csv.annotation.CsvField;

@Entity
@Data
@EqualsAndHashCode(callSuper = false)
@Accessors(chain = true)
@CsvDataType
public class ActiveSessionHistory extends Model {

  @CsvField(pos = 1, converterType = InstantConverter.class)
  private Instant sampleTime;

  @CsvField(pos = 2, converterType = UuidConverter.class)
  private UUID universeId;

  @CsvField(pos = 3)
  private String nodeName;

  @CsvField(pos = 4, converterType = UuidConverter.class)
  private UUID rootRequestId;

  @CsvField(pos = 5, converterType = LongConverter.class)
  private Long rpcRequestId;

  @CsvField(pos = 6)
  private String waitEventComponent;

  @CsvField(pos = 7)
  private String waitEventClass;

  @CsvField(pos = 8)
  private String waitEvent;

  @CsvField(pos = 9, converterType = UuidConverter.class)
  private UUID topLevelNodeId;

  @CsvField(pos = 10)
  private Long queryId;

  @CsvField(pos = 11)
  private Long ysqlSessionId;

  @CsvField(pos = 12)
  private String clientNodeIp;

  @CsvField(pos = 13)
  private String waitEventAux;

  @CsvField(pos = 14)
  private int sampleWeight;
}
