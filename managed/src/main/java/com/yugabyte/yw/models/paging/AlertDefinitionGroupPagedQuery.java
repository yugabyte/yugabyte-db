// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.paging;

import com.yugabyte.yw.models.AlertDefinitionGroup;
import com.yugabyte.yw.models.filters.AlertDefinitionGroupFilter;
import lombok.Data;
import lombok.EqualsAndHashCode;

@Data
@EqualsAndHashCode(callSuper = true)
public class AlertDefinitionGroupPagedQuery
    extends PagedQuery<AlertDefinitionGroupFilter, AlertDefinitionGroup.SortBy> {}
