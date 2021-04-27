package com.yugabyte.yw.forms;

import java.util.UUID;

public class TableManagerParams extends UniverseTaskParams {
  private String keyspace;

  private String tableName;

  public UUID tableUUID;

  public boolean sse = false;

  public String getKeyspace() {
    return keyspace;
  }

  public void setKeyspace(String keyspace) {
    this.keyspace = keyspace;
  }

  public String getTableName() {
    return tableName;
  }

  public void setTableName(String tableName) {
    this.tableName = tableName;
  }
}
