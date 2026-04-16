// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.forms.paging;

import com.yugabyte.yw.controllers.CustomerTaskController;
import com.yugabyte.yw.forms.filters.TaskApiFilter;
import com.yugabyte.yw.models.paging.PagedQuery;
import lombok.Data;
import lombok.EqualsAndHashCode;

@Data
@EqualsAndHashCode(callSuper = true)
public class TaskPagedApiQuery extends PagedQuery<TaskApiFilter, CustomerTaskController.SortBy> {}
