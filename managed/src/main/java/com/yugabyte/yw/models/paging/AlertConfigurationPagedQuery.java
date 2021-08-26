// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.paging;

import com.yugabyte.yw.models.AlertConfiguration;
import com.yugabyte.yw.models.filters.AlertConfigurationFilter;
import lombok.Data;
import lombok.EqualsAndHashCode;

@Data
@EqualsAndHashCode(callSuper = true)
public class AlertConfigurationPagedQuery
    extends PagedQuery<AlertConfigurationFilter, AlertConfiguration.SortBy> {}
