// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms.paging;

import com.yugabyte.yw.forms.filters.AlertConfigurationApiFilter;
import com.yugabyte.yw.models.AlertConfiguration;
import com.yugabyte.yw.models.paging.PagedQuery;
import lombok.Data;
import lombok.EqualsAndHashCode;

@Data
@EqualsAndHashCode(callSuper = true)
public class AlertConfigurationPagedApiQuery
    extends PagedQuery<AlertConfigurationApiFilter, AlertConfiguration.SortBy> {}
