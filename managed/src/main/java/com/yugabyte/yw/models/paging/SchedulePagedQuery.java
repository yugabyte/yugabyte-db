// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.paging;

import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.filters.ScheduleFilter;
import lombok.Data;
import lombok.EqualsAndHashCode;

@Data
@EqualsAndHashCode(callSuper = true)
public class SchedulePagedQuery extends PagedQuery<ScheduleFilter, Schedule.SortBy> {}
