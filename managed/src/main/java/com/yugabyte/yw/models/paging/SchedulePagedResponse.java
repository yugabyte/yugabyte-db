// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.paging;

import com.yugabyte.yw.models.Schedule;
import lombok.Data;
import lombok.EqualsAndHashCode;

@Data
@EqualsAndHashCode(callSuper = true)
public class SchedulePagedResponse extends PagedResponse<Schedule> {}
