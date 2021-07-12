// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms.paging;

import com.yugabyte.yw.forms.filters.AlertApiFilter;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.paging.PagedQuery;
import lombok.Data;
import lombok.EqualsAndHashCode;

@Data
@EqualsAndHashCode(callSuper = true)
public class AlertPagedApiQuery extends PagedQuery<AlertApiFilter, Alert.SortBy> {}
