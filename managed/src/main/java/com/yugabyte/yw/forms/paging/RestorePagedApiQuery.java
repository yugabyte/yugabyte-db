// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms.paging;

import com.yugabyte.yw.forms.filters.RestoreApiFilter;
import com.yugabyte.yw.models.Restore;
import com.yugabyte.yw.models.paging.PagedQuery;
import lombok.Data;
import lombok.EqualsAndHashCode;

@Data
@EqualsAndHashCode(callSuper = true)
public class RestorePagedApiQuery extends PagedQuery<RestoreApiFilter, Restore.SortBy> {}
