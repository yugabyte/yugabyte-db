// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms.paging;

import com.yugabyte.yw.forms.JobScheduleResp;
import com.yugabyte.yw.models.paging.PagedResponse;
import lombok.Data;
import lombok.EqualsAndHashCode;

@Data
@EqualsAndHashCode(callSuper = true)
public class JobSchedulePagedApiResponse extends PagedResponse<JobScheduleResp> {}
