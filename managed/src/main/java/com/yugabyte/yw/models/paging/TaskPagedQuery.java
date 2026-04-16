// Copyright (c) YugabyteDB, Inc

package com.yugabyte.yw.models.paging;

import com.yugabyte.yw.controllers.CustomerTaskController;
import com.yugabyte.yw.models.filters.TaskFilter;
import lombok.Data;
import lombok.EqualsAndHashCode;

@Data
@EqualsAndHashCode(callSuper = true)
public class TaskPagedQuery extends PagedQuery<TaskFilter, CustomerTaskController.SortBy> {}
