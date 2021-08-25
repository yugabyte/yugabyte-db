// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import play.data.validation.Constraints;

@ApiModel(description = "Bulk import parameters")
public class BulkImportParams extends TableManagerParams {

  // S3 bucket containing data to be bulk imported (should be of format s3://<path>/)
  @Constraints.Required
  @ApiModelProperty(value = "S3 bucket URL", required = true)
  public String s3Bucket;

  // Number of task nodes for the EMR job. Optional.
  @ApiModelProperty(value = "Instance count")
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
