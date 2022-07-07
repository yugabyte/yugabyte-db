// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms.paging;

import com.yugabyte.yw.forms.filters.ScheduleApiFilter;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.paging.PagedQuery;
import lombok.Data;
import lombok.EqualsAndHashCode;

@Data
@EqualsAndHashCode(callSuper = true)
public class SchedulePagedApiQuery extends PagedQuery<ScheduleApiFilter, Schedule.SortBy> {}
