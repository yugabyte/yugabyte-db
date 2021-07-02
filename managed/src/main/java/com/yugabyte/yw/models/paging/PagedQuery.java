// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.paging;

import lombok.Data;
import play.data.validation.Constraints;

@Data
public abstract class PagedQuery<F, S extends PagedQuery.SortByIF> {

  public enum SortDirection {
    ASC,
    DESC
  }

  public interface SortByIF {
    String getSortField();
  }

  @Constraints.Required() F filter;

  @Constraints.Required() S sortBy;

  @Constraints.Required() SortDirection direction;

  @Constraints.Required() int offset;

  @Constraints.Required() int limit;

  @Constraints.Required() boolean needTotalCount;
}
