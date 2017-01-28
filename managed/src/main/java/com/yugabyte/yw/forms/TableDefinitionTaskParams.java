// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.yugabyte.yw.models.helpers.TableDetails;
import org.yb.Common;

public class TableDefinitionTaskParams extends TableTaskParams {
  public String tableName = null;
  public Common.TableType tableType = null;
  public TableDetails tableDetails = null;
}
