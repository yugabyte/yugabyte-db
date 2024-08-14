// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.paging;

import com.yugabyte.yw.models.JobInstance;
import com.yugabyte.yw.models.filters.JobInstanceFilter;
import lombok.Data;
import lombok.EqualsAndHashCode;

@Data
@EqualsAndHashCode(callSuper = true)
public class JobInstancePagedQuery extends PagedQuery<JobInstanceFilter, JobInstance.SortBy> {}
