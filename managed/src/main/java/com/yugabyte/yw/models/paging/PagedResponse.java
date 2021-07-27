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
}
