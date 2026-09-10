// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.rollback;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.TaskInfo;
import lombok.Getter;
import lombok.RequiredArgsConstructor;

/** Inputs available to a {@link TaskRollbackComputer} when computing a rollback submission. */
@Getter
@RequiredArgsConstructor
public class RollbackContext {
  private final Customer customer;
  private final CustomerTask customerTask;
  private final TaskInfo taskInfo;
  private final JsonNode oldTaskParams;
}
