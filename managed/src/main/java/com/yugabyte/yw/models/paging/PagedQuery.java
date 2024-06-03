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

    // Typically id or uuid. Is used to get consistent sort order in case sort field is equal.
    SortByIF getOrderField();
  }

  @Constraints.Required() F filter;

  @Constraints.Required() S sortBy;

  @Constraints.Required() SortDirection direction;

  @Constraints.Required() int offset = 0;

  @Constraints.Required() int limit = 10;

  @Constraints.Required() boolean needTotalCount;

  public <NF, T extends PagedQuery<NF, S>> T copyWithFilter(NF newFilter, Class<T> queryClass) {
    T newQuery;
    try {
      newQuery = queryClass.newInstance();
    } catch (Exception e) {
      throw new RuntimeException("Failed to instantiate " + queryClass.getSimpleName());
    }
    newQuery.setFilter(newFilter);
    newQuery.setSortBy(sortBy);
    newQuery.setDirection(direction);
    newQuery.setOffset(offset);
    newQuery.setLimit(limit);
    newQuery.setNeedTotalCount(needTotalCount);
    return newQuery;
  }
}
