// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import org.yb.Common.TableType;
import play.data.validation.Constraints;

import static org.yb.Common.TableType.PGSQL_TABLE_TYPE;


public class RunQueryFormData {
  @Constraints.Required()
  public String query;

  @Constraints.Required()
  public String db_name;

  public TableType tableType = PGSQL_TABLE_TYPE;
}
