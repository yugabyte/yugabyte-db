// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.paging;

import com.yugabyte.yw.models.MaintenanceWindow;
import lombok.Data;
import lombok.EqualsAndHashCode;

@Data
@EqualsAndHashCode(callSuper = true)
public class MaintenanceWindowPagedResponse extends PagedResponse<MaintenanceWindow> {}
