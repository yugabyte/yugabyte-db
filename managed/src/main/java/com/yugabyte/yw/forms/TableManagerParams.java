package com.yugabyte.yw.forms;

import io.swagger.annotations.ApiModelProperty;
import java.util.UUID;

public class TableManagerParams extends UniverseTaskParams {

  @ApiModelProperty(value = "Key space")
  private String keyspace;

  @ApiModelProperty(value = "Table name")
  private String tableName;

  @ApiModelProperty(value = "Table UUID")
  public UUID tableUUID;

  @ApiModelProperty(value = "Is SSE")
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
