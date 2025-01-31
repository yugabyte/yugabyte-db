// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.paging;

import com.yugabyte.yw.models.JobSchedule;
import com.yugabyte.yw.models.filters.JobScheduleFilter;
import lombok.Data;
import lombok.EqualsAndHashCode;

@Data
@EqualsAndHashCode(callSuper = true)
public class JobSchedulePagedQuery extends PagedQuery<JobScheduleFilter, JobSchedule.SortBy> {}
