// Copyright (c) YugabyteDB, Inc.
package com.yugabyte.yw.forms;

import io.swagger.annotations.ApiModel;
import java.util.ArrayList;

/** This class will be used by Table controller createMultiTableBackup API. */
@ApiModel(description = "Multi-table backup parameters")
public class MultiTableBackupRequestParams extends BackupTableParams {

  public MultiTableBackupRequestParams() {
    super();
    // FIXME: Remove this once the references are fixed.
    tableUUIDList = new ArrayList<>();
  }
}
