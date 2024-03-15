// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.databind.PropertyNamingStrategy;
import com.fasterxml.jackson.databind.annotation.JsonNaming;
import javax.validation.constraints.NotNull;
import lombok.Data;
import org.yb.CommonTypes.TableType;

@Data
@JsonNaming(PropertyNamingStrategy.SnakeCaseStrategy.class)
public class RunQueryFormData {

  @NotNull private String query;

  @NotNull private String dbName;

  private String nodeName;

  private TableType tableType = TableType.PGSQL_TABLE_TYPE;
}
