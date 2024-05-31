// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.paging;

import com.yugabyte.yw.models.MaintenanceWindow;
import com.yugabyte.yw.models.filters.MaintenanceWindowFilter;
import lombok.Data;
import lombok.EqualsAndHashCode;

@Data
@EqualsAndHashCode(callSuper = true)
public class MaintenanceWindowPagedQuery
    extends PagedQuery<MaintenanceWindowFilter, MaintenanceWindow.SortBy> {}
