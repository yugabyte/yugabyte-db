package com.yugabyte.yw.forms;

import play.data.validation.Constraints;

import java.util.UUID;

public class TableManagerParams extends UniverseTaskParams {
  @Constraints.Required
  public String keyspace;

  @Constraints.Required
  public String tableName;

  public UUID tableUUID;

  public boolean sse = false;
}
