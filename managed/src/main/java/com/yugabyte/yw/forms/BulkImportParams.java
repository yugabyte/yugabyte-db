// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import play.data.validation.Constraints;

import java.util.UUID;

public class BulkImportParams extends UniverseTaskParams {
  // S3 bucket containing data to be bulk imported (should be of format s3://<path>/)
  @Constraints.Required
  public String s3Bucket;

  // Keyspace of the table to bulk import data into
  @Constraints.Required
  public String keyspace;

  // Name of the table to bulk import data into
  @Constraints.Required
  public String tableName;
}
