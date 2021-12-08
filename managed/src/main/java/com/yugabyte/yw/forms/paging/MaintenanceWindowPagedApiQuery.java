// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms.paging;

import com.yugabyte.yw.forms.filters.MaintenanceWindowApiFilter;
import com.yugabyte.yw.models.MaintenanceWindow;
import com.yugabyte.yw.models.paging.PagedQuery;
import lombok.Data;
import lombok.EqualsAndHashCode;

@Data
@EqualsAndHashCode(callSuper = true)
public class MaintenanceWindowPagedApiQuery
    extends PagedQuery<MaintenanceWindowApiFilter, MaintenanceWindow.SortBy> {}
