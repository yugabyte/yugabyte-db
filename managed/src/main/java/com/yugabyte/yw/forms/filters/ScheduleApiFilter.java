/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.forms.filters;

import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.filters.ScheduleFilter;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.Set;
import java.util.UUID;
import lombok.Data;
import lombok.NoArgsConstructor;
import org.apache.commons.collections4.CollectionUtils;

@Data
@NoArgsConstructor
public class ScheduleApiFilter {

  private Set<TaskType> taskTypes;
  private Set<Schedule.State> status;
  private Set<UUID> universeUUIDList;

  public ScheduleFilter toFilter() {
    ScheduleFilter.ScheduleFilterBuilder builder = ScheduleFilter.builder();
    if (!CollectionUtils.isEmpty(taskTypes)) {
      builder.taskTypes(taskTypes);
    }
    if (!CollectionUtils.isEmpty(status)) {
      builder.status(status);
    }
    if (!CollectionUtils.isEmpty(universeUUIDList)) {
      builder.universeUUIDList(universeUUIDList);
    }
    return builder.build();
  }
}
