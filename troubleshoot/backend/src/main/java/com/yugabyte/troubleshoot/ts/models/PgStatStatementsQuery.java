// Copyright (c) Yugabyte, Inc.
package com.yugabyte.troubleshoot.ts.models;

import io.ebean.Model;
import java.time.Instant;
import javax.persistence.EmbeddedId;
import javax.persistence.Entity;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.experimental.Accessors;

@Entity
@Data
@EqualsAndHashCode(callSuper = false)
@Accessors(chain = true)
public class PgStatStatementsQuery extends Model implements ModelWithId<PgStatStatementsQueryId> {

  @EmbeddedId private PgStatStatementsQueryId id;

  private Instant scheduledTimestamp;

  private String query;
  private String dbId;
  private String dbName;
}
