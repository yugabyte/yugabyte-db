// Copyright (c) YugaByte, Inc

package com.yugabyte.yw.models.paging;

import com.yugabyte.yw.models.Restore;
import com.yugabyte.yw.models.filters.RestoreFilter;
import lombok.Data;
import lombok.EqualsAndHashCode;

@Data
@EqualsAndHashCode(callSuper = true)
public class RestorePagedQuery extends PagedQuery<RestoreFilter, Restore.SortBy> {}
