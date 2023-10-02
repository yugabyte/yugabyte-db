// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.paging;

import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.filters.NodeAgentFilter;
import lombok.Data;
import lombok.EqualsAndHashCode;

@Data
@EqualsAndHashCode(callSuper = true)
public class NodeAgentPagedQuery extends PagedQuery<NodeAgentFilter, NodeAgent.SortBy> {}
