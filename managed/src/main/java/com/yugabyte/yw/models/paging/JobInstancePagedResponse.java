// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.paging;

import com.yugabyte.yw.forms.JobInstanceResp;
import com.yugabyte.yw.forms.paging.JobInstancePagedApiResponse;
import com.yugabyte.yw.models.JobInstance;
import java.util.stream.Collectors;
import lombok.Data;
import lombok.EqualsAndHashCode;

@Data
@EqualsAndHashCode(callSuper = true)
public class JobInstancePagedResponse extends PagedResponse<JobInstance> {

  public JobInstancePagedApiResponse convertToApiResponse() {
    return setData(
        getEntities().stream().map(e -> new JobInstanceResp(e)).collect(Collectors.toList()),
        new JobInstancePagedApiResponse());
  }
}
