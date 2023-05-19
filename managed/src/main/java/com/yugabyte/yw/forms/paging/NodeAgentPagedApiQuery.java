// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms.paging;

import com.yugabyte.yw.forms.filters.NodeAgentApiFilter;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.paging.PagedQuery;
import lombok.Data;
import lombok.EqualsAndHashCode;

@Data
@EqualsAndHashCode(callSuper = true)
public class NodeAgentPagedApiQuery extends PagedQuery<NodeAgentApiFilter, NodeAgent.SortBy> {}
