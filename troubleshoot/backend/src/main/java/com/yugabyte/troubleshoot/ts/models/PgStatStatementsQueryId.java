package com.yugabyte.troubleshoot.ts.models;

import java.util.UUID;
import javax.persistence.Embeddable;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.experimental.Accessors;

@Embeddable
@Data
@Accessors(chain = true)
@EqualsAndHashCode(callSuper = false)
public class PgStatStatementsQueryId {
  private UUID universeId;
  private long queryId;
}
