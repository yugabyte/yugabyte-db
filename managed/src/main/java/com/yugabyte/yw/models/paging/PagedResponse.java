// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.paging;

import java.util.List;
import lombok.Data;

@Data
public class PagedResponse<E> {
  List<E> entities;
  boolean hasNext;
  boolean hasPrev;
  Integer totalCount;

  public <D, T extends PagedResponse<D>> T setData(List<D> data, T response) {
    response.setEntities(data);
    response.setHasNext(hasNext);
    response.setHasPrev(hasPrev);
    response.setTotalCount(totalCount);
    return response;
  }
}
