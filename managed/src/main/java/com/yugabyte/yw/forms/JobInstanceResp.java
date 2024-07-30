// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonCreator;
import com.fasterxml.jackson.annotation.JsonUnwrapped;
import com.yugabyte.yw.models.JobInstance;
import lombok.Getter;
import lombok.Setter;

@Getter
@Setter
public class JobInstanceResp {
  @JsonUnwrapped private final JobInstance jobInstance;

  @JsonCreator
  public JobInstanceResp(JobInstance jobInstance) {
    this.jobInstance = jobInstance;
  }
}
