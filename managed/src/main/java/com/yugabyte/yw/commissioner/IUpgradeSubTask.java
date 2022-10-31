// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.List;
import java.util.Set;

/**
 * This is a sub task creator interface. This will be called at appropriate moment, when the tasks
 * are to be added
 */
@FunctionalInterface
public interface IUpgradeSubTask {
  void run(List<NodeDetails> nodes, Set<ServerType> processTypes);
}
