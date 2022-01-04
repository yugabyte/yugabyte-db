// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import org.yb.CommonTypes.TableType;
import play.data.validation.Constraints;

public class RunQueryFormData {
  @Constraints.Required() public String query;

  @Constraints.Required() public String db_name;

  public TableType tableType = TableType.PGSQL_TABLE_TYPE;
}
