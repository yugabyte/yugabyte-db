// Copyright (c) YugaByte, Inc

package com.yugabyte.yw.models.paging;

import com.yugabyte.yw.models.RestoreResp;
import lombok.Data;
import lombok.EqualsAndHashCode;

@Data
@EqualsAndHashCode(callSuper = true)
public class RestorePagedApiResponse extends PagedResponse<RestoreResp> {}
