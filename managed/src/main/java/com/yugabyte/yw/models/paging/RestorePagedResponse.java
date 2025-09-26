// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models.paging;

import com.yugabyte.yw.models.Restore;
import lombok.Data;
import lombok.EqualsAndHashCode;

@Data
@EqualsAndHashCode(callSuper = true)
public class RestorePagedResponse extends PagedResponse<Restore> {}
