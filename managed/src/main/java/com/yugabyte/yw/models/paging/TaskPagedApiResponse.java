// Copyright (c) YugabyteDB, Inc

package com.yugabyte.yw.models.paging;

import com.yugabyte.yw.forms.CustomerTaskFormData;
import lombok.Data;
import lombok.EqualsAndHashCode;

@Data
@EqualsAndHashCode(callSuper = true)
public class TaskPagedApiResponse extends PagedResponse<CustomerTaskFormData> {}
