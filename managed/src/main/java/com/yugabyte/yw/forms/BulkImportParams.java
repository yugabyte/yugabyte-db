// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import play.data.validation.Constraints;

public class BulkImportParams extends TableManagerParams {

  // S3 bucket containing data to be bulk imported (should be of format s3://<path>/)
  @Constraints.Required
  public String s3Bucket;

  // Number of task nodes for the EMR job. Optional.
  public int instanceCount = 0;

  @Constraints.Required
  public String getKeyspace() {
    return super.getKeyspace();
  }

  public void setKeyspace(String keyspace) {
    super.setKeyspace(keyspace);
  }

  @Constraints.Required
  public String getTableName() {
    return super.getTableName();
  }

  public void setTableName(String tableName) {
    super.setTableName(tableName);
  }
}
