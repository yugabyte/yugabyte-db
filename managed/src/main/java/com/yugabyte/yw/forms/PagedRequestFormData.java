// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import play.data.validation.Constraints;

public class PagedRequestFormData<F, S> {
  @Constraints.Required() public F filter;

  @Constraints.Required() public S sortBy;

  @Constraints.Required() public int offset;

  @Constraints.Required() public int limit;
}
