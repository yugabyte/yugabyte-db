// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.paging;

import com.yugabyte.yw.forms.JobScheduleResp;
import com.yugabyte.yw.forms.paging.JobSchedulePagedApiResponse;
import com.yugabyte.yw.models.JobSchedule;
import java.util.stream.Collectors;
import lombok.Data;
import lombok.EqualsAndHashCode;

@Data
@EqualsAndHashCode(callSuper = true)
public class JobSchedulePagedResponse extends PagedResponse<JobSchedule> {

  public JobSchedulePagedApiResponse convertToApiResponse() {
    return setData(
        getEntities().stream().map(e -> new JobScheduleResp(e)).collect(Collectors.toList()),
        new JobSchedulePagedApiResponse());
  }
}
