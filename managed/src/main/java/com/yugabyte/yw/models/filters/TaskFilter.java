// Copyright (c) YugaByte, Inc
package com.yugabyte.yw.models.filters;

import com.yugabyte.yw.models.CustomerTask;
import java.util.Date;
import java.util.Set;
import java.util.UUID;
import lombok.Builder;
import lombok.Value;

@Value
@Builder(toBuilder = true)
public class TaskFilter {
  Date dateRangeStart;
  Date dateRangeEnd;
  Set<CustomerTask.TargetType> targetList;
  Set<UUID> targetUUIDList;
  Set<CustomerTask.TaskType> typeList;
  Set<String> typeNameList;
  Set<String> status;
  UUID customerUUID;
}
