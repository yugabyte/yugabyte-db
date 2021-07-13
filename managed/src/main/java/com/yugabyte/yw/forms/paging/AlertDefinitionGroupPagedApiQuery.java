// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms.paging;

import com.yugabyte.yw.forms.filters.AlertDefinitionGroupApiFilter;
import com.yugabyte.yw.models.AlertDefinitionGroup;
import com.yugabyte.yw.models.paging.PagedQuery;
import lombok.Data;
import lombok.EqualsAndHashCode;

@Data
@EqualsAndHashCode(callSuper = true)
public class AlertDefinitionGroupPagedApiQuery
    extends PagedQuery<AlertDefinitionGroupApiFilter, AlertDefinitionGroup.SortBy> {}
