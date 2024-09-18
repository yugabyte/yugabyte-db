// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms.paging;

import com.yugabyte.yw.forms.JobInstanceResp;
import com.yugabyte.yw.models.paging.PagedResponse;
import lombok.Data;
import lombok.EqualsAndHashCode;

@Data
@EqualsAndHashCode(callSuper = true)
public class JobInstancePagedApiResponse extends PagedResponse<JobInstanceResp> {}
