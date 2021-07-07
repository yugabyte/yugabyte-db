// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.paging;

import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.filters.AlertFilter;
import lombok.Data;
import lombok.EqualsAndHashCode;

@Data
@EqualsAndHashCode(callSuper = true)
public class AlertPagedQuery extends PagedQuery<AlertFilter, Alert.SortBy> {}
