// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.paging;

import lombok.Data;

import java.util.List;

@Data
public class PagedResponse<E> {
  List<E> entities;
  boolean hasNext;
  boolean hasPrev;
  Integer totalCount;
}
